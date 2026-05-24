// Unit tests for StreamPool — CUDA device required.
//
// Build:
//   nvcc -std=c++17 -I../../engine/src test_stream_pool.cpp
//         ../../engine/src/runtime/StreamPool.cpp
//         -lcuda -lcudart -lpthread -o test_stream_pool
//   ./test_stream_pool
//
// Tests:
//   1. acquire() returns a valid stream + unique index
//   2. release() returns the slot to the pool
//   3. size() matches the pool capacity
//   4. Pool saturates (acquire blocks) when all slots are taken
//   5. Concurrent acquire/release from multiple threads — no deadlock
//   6. syncAll() blocks until all streams drain
//   7. Slot indices are in range [0, size)

#include "runtime/StreamPool.h"
#include <cuda.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <set>

using namespace symm;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; std::printf("  [PASS] %s\n", msg); } \
    else { ++failed; std::fprintf(stderr, "  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while(0)

// ── Test 1: size() matches capacity ──────────────────────────────────────────

void test_size() {
    StreamPool pool(8);
    CHECK(pool.size() == 8, "pool.size() == 8");
}

// ── Test 2: acquire returns valid stream + unique index ───────────────────────

void test_acquire_basic() {
    StreamPool pool(4);
    auto slot = pool.acquire();
    CHECK(slot.stream != nullptr, "acquire returns non-null stream");
    CHECK(slot.idx >= 0 && slot.idx < 4, "acquire index in [0, size)");
    pool.release(slot);
}

// ── Test 3: release returns slot to pool (can re-acquire) ────────────────────

void test_release_returns_slot() {
    StreamPool pool(2);

    auto s0 = pool.acquire();
    auto s1 = pool.acquire();

    // Both slots are now held — pool should be empty (available == 0)
    // Release one and immediately re-acquire it
    pool.release(s0);

    auto s2 = pool.acquire();    // must not block — one slot was returned
    CHECK(s2.stream != nullptr, "re-acquire after release succeeds");
    CHECK(s2.idx == s0.idx,     "re-acquired slot has the same index");

    pool.release(s1);
    pool.release(s2);
}

// ── Test 4: slot indices cover full [0, size) range ──────────────────────────

void test_all_indices_unique() {
    const int N = 8;
    StreamPool pool(N);

    std::vector<StreamPool::Slot> slots;
    std::set<int> indices;

    for (int i = 0; i < N; i++) {
        auto s = pool.acquire();
        slots.push_back(s);
        indices.insert(s.idx);
    }

    CHECK((int)indices.size() == N, "all slot indices are unique");
    for (int i = 0; i < N; i++) CHECK(indices.count(i), "index i is present");

    for (auto& s : slots) pool.release(s);
}

// ── Test 5: saturation — acquire blocks until a slot is released ──────────────

void test_saturation_blocks_then_unblocks() {
    const int N = 2;
    StreamPool pool(N);

    // Hold all N slots
    auto s0 = pool.acquire();
    auto s1 = pool.acquire();

    std::atomic<bool> acquired{ false };

    // Background thread will block until we release
    std::thread waiter([&pool, &acquired]() {
        auto s = pool.acquire();    // blocks here
        acquired.store(true);
        pool.release(s);
    });

    // Give the thread time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!acquired.load(), "waiter is blocked while pool is full");

    pool.release(s0);   // unblock waiter

    waiter.join();
    CHECK(acquired.load(), "waiter unblocked after release");

    pool.release(s1);
}

// ── Test 6: concurrent acquire/release from N threads ────────────────────────

void test_concurrent_acquire_release() {
    const int POOL   = 4;
    const int THREAD = 16;
    StreamPool pool(POOL);

    std::atomic<int> completed{ 0 };

    auto worker = [&pool, &completed]() {
        auto slot = pool.acquire();
        // Simulate some GPU work
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pool.release(slot);
        completed++;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD; i++) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    CHECK(completed.load() == THREAD, "all threads completed acquire/release");
    CHECK(pool.available() == POOL,   "all slots returned after concurrent run");
}

// ── Test 7: syncAll() blocks until streams drain ─────────────────────────────

void test_sync_all() {
    StreamPool pool(4);

    // Enqueue some trivial CUDA work on the stream
    auto slot = pool.acquire();
    // cuMemsetD8Async on device memory — just to enqueue something
    CUdeviceptr dummy = 0;
    cuMemAlloc(&dummy, 4);
    cuMemsetD8Async(dummy, 0, 4, slot.stream);
    pool.release(slot);

    // syncAll should complete without hanging
    pool.syncAll();
    cuMemFree(dummy);
    CHECK(true, "syncAll() completed without deadlock");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    if (cuInit(0) != CUDA_SUCCESS) {
        std::fprintf(stderr, "SKIP: no CUDA device\n");
        return 0;
    }
    CUdevice dev; CUcontext ctx;
    cuDeviceGet(&dev, 0);
    cuCtxCreate(&ctx, 0, dev);

    std::printf("── StreamPool ───────────────────────────────────────────────────\n");
    test_size();
    test_acquire_basic();
    test_release_returns_slot();
    test_all_indices_unique();
    test_saturation_blocks_then_unblocks();
    test_concurrent_acquire_release();
    test_sync_all();

    std::printf("\nResults: %d passed, %d failed\n", passed, failed);
    cuCtxDestroy(ctx);
    return failed > 0 ? 1 : 0;
}
