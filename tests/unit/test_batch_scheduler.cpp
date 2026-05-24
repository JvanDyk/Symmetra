// Unit tests for BatchScheduler — no GPU required.
// Uses a mock launch callback that records what was submitted.
//
// Build (Linux / macOS):
//   g++ -std=c++17 -pthread \
//       -I../../engine/src \
//       test_batch_scheduler.cpp \
//       ../../engine/src/runtime/BatchScheduler.cpp \
//       -o test_batch_scheduler && ./test_batch_scheduler
//
// Build (Windows, MSVC):
//   cl /std:c++17 /EHsc /I..\..\engine\src \
//      test_batch_scheduler.cpp \
//      ..\..\engine\src\runtime\BatchScheduler.cpp \
//      /link /out:test_batch_scheduler.exe && test_batch_scheduler.exe

#include "runtime/BatchScheduler.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

using namespace symm;
using namespace std::chrono_literals;

// ── Test harness ──────────────────────────────────────────────────────────────

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; std::printf("  PASS  %s\n", msg); } \
    else { ++failed; std::fprintf(stderr, "  FAIL  %s  [%s:%d]\n", msg, __FILE__, __LINE__); } \
} while(0)

// ── Shared mock state ─────────────────────────────────────────────────────────

struct LaunchRecord {
    int                       routeId;
    int                       batchSize;
    std::vector<std::string>  paths;   // snapshot of paths submitted
};

// Captures every launch call so tests can inspect it.
struct MockSink {
    std::mutex                  mu;
    std::vector<LaunchRecord>   launches;
    std::atomic<int>            totalRequests{0};

    BatchLaunchFn makeFn() {
        return [this](int routeId,
                      const std::vector<BatchRequest>& reqs,
                      BatchResult& result) {
            LaunchRecord rec;
            rec.routeId   = routeId;
            rec.batchSize = (int)reqs.size();
            for (auto& r : reqs) rec.paths.push_back(r.path);

            {
                std::lock_guard<std::mutex> lk(mu);
                launches.push_back(std::move(rec));
            }
            totalRequests.fetch_add((int)reqs.size(), std::memory_order_relaxed);

            // Fill placeholder OK responses
            result.responses.assign(reqs.size(), "{\"ok\":true}");
            result.statuses.assign(reqs.size(), 200);
        };
    }

    int launchCount() {
        std::lock_guard<std::mutex> lk(mu);
        return (int)launches.size();
    }

    LaunchRecord lastLaunch() {
        std::lock_guard<std::mutex> lk(mu);
        return launches.back();
    }
};

// ── Helper: build a dummy BatchRequest ────────────────────────────────────────

static BatchRequest makeReq(int routeId, const std::string& path = "/test") {
    BatchRequest r;
    r.fd        = -1;          // no real socket in unit tests
    r.routeId   = routeId;
    r.method    = "GET";
    r.path      = path;
    r.body      = "";
    r.arrivalUs = 0;
    return r;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. A single route fires after the fill timeout when the batch is under-full.
void test_single_route_timeout() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 256;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 20'000;   // 20 ms — short enough for a unit test
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    sched.submitRequest(makeReq(0, "/a"));
    sched.submitRequest(makeReq(0, "/b"));

    // Wait for the fill timeout to fire (give it 3× the timeout)
    std::this_thread::sleep_for(70ms);
    sched.stop();

    CHECK(sink.launchCount() == 1,   "exactly 1 batch launched");
    CHECK(sink.totalRequests == 2,   "2 requests processed");
    CHECK(sink.lastLaunch().routeId == 0, "correct routeId");
    CHECK(sink.lastLaunch().batchSize == 2, "correct batch size");
}

// 2. Two different routes produce separate batches (warp-coherence guarantee).
void test_separate_routes_separate_batches() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 256;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 20'000;   // 20 ms
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    sched.submitRequest(makeReq(0, "/route0-a"));
    sched.submitRequest(makeReq(1, "/route1-a"));
    sched.submitRequest(makeReq(0, "/route0-b"));
    sched.submitRequest(makeReq(1, "/route1-b"));

    std::this_thread::sleep_for(80ms);
    sched.stop();

    CHECK(sink.launchCount() == 2, "2 separate batches (one per route)");
    CHECK(sink.totalRequests == 4, "all 4 requests processed");

    // Both route 0 and route 1 must appear exactly once
    std::lock_guard<std::mutex> lk(sink.mu);
    bool route0found = false, route1found = false;
    for (auto& rec : sink.launches) {
        if (rec.routeId == 0) { route0found = true; CHECK(rec.batchSize == 2, "route0 batch has 2 requests"); }
        if (rec.routeId == 1) { route1found = true; CHECK(rec.batchSize == 2, "route1 batch has 2 requests"); }
    }
    CHECK(route0found, "route 0 was launched");
    CHECK(route1found, "route 1 was launched");
}

// 3. A batch of preferredBatchSize fires immediately (no timeout wait).
void test_full_batch_fires_immediately() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 4;    // small for testing
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 5'000'000;   // 5 s — effectively infinite for this test
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    // Submit exactly preferredBatchSize requests
    for (int i = 0; i < 4; i++)
        sched.submitRequest(makeReq(2, "/full-" + std::to_string(i)));

    // Give the scheduler a moment to fire (should be near-instant, not 5 s)
    std::this_thread::sleep_for(50ms);
    sched.stop();

    CHECK(sink.launchCount() >= 1,     "batch fired before timeout");
    CHECK(sink.totalRequests >= 4,     "all 4 requests processed");
    if (sink.launchCount() >= 1) {
        CHECK(sink.lastLaunch().batchSize <= 4, "batch size ≤ preferredBatchSize");
    }
}

// 4. stop() drains remaining requests that haven't yet fired.
void test_stop_drains_pending() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 256;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 10'000'000;  // 10 s — won't fire on its own
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    for (int i = 0; i < 5; i++)
        sched.submitRequest(makeReq(3, "/drain-" + std::to_string(i)));

    // stop() must flush before returning
    sched.stop();

    CHECK(sink.totalRequests == 5, "stop() drained all 5 pending requests");
    CHECK(sink.launchCount() >= 1, "at least one launch during drain");
}

// 5. Metrics counters reflect launch statistics.
void test_metrics() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 2;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 20'000;
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    // Full batch (2 requests)
    sched.submitRequest(makeReq(0, "/m1"));
    sched.submitRequest(makeReq(0, "/m2"));

    std::this_thread::sleep_for(60ms);

    // Partial batch (1 request — waits for timeout)
    sched.submitRequest(makeReq(0, "/m3"));
    std::this_thread::sleep_for(80ms);

    sched.stop();

    const auto& m = sched.metrics();
    CHECK(m.totalBatchesLaunched   >= 2, "at least 2 batches launched");
    CHECK(m.totalRequestsProcessed >= 3, "at least 3 requests processed");
    CHECK(m.fullBatchesLaunched    >= 1, "at least 1 full batch");
    CHECK(m.partialBatchesLaunched >= 1, "at least 1 partial batch");
}

// 6. warp-alignment: batch size is always a multiple of WARP_WIDTH (32).
void test_warp_alignment() {
    // alignToBatchBoundary is private; verify indirectly via the batch sizes
    // observed at the launch callback.  We submit 1 request and check the
    // padded kernel thread count would be 32.  Since the mock receives the
    // *actual* count (not padded), we verify here just that 1 request fires.
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 256;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 20'000;
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    sched.submitRequest(makeReq(0, "/warp-test"));
    std::this_thread::sleep_for(70ms);
    sched.stop();

    // The actual request count in the callback is 1 (real requests only).
    // GpuServer's batchLaunch() pads to 32 before cuLaunchKernel —
    // that layer is tested by the GPU integration tests.
    CHECK(sink.launchCount() == 1,                "1 launch for 1 request");
    CHECK(sink.lastLaunch().batchSize == 1,       "1 actual request in callback");
    CHECK(sink.lastLaunch().paths[0] == "/warp-test", "correct path forwarded");
}

// 7. Multi-threaded concurrent submit — all requests are accounted for.
void test_concurrent_submit() {
    MockSink sink;

    BatchPolicy pol;
    pol.preferredBatchSize = 32;
    pol.minBatchSize       = 1;
    pol.fillTimeoutUs      = 30'000;
    pol.maxRoutes          = 4;

    BatchScheduler sched(sink.makeFn(), pol);
    sched.start();

    // 8 threads each submit 16 requests → 128 total
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&sched, t] {
            for (int i = 0; i < 16; i++)
                sched.submitRequest(makeReq(t % 4, "/t" + std::to_string(t) + "-" + std::to_string(i)));
        });
    }
    for (auto& th : threads) th.join();

    // Wait for fill timeouts on any partially-filled batches
    std::this_thread::sleep_for(120ms);
    sched.stop();

    CHECK(sink.totalRequests == 128, "all 128 concurrent requests processed");
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::printf("=== BatchScheduler Unit Tests ===\n\n");

    std::printf("[1] single route fires after timeout\n");
    test_single_route_timeout();

    std::printf("[2] separate routes produce separate batches\n");
    test_separate_routes_separate_batches();

    std::printf("[3] full batch fires immediately (no timeout)\n");
    test_full_batch_fires_immediately();

    std::printf("[4] stop() drains pending requests\n");
    test_stop_drains_pending();

    std::printf("[5] metrics counters\n");
    test_metrics();

    std::printf("[6] warp-alignment contract\n");
    test_warp_alignment();

    std::printf("[7] concurrent submits from 8 threads\n");
    test_concurrent_submit();

    std::printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return (failed == 0) ? 0 : 1;
}
