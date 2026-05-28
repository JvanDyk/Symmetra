#pragma once
/**
 * BatchScheduler — groups incoming HTTP requests by route into CUDA-warp-aligned batches.
 *
 * Design goals:
 *   1. One batch = one route handler → zero warp divergence at the dispatch switch.
 *   2. Batch size is always a multiple of 32 (one warp). Default target: 256 threads
 *      (8 warps / 1 block) for good SM occupancy.
 *   3. Fill deadline: if a queue hasn't reached its target in FILL_TIMEOUT_US, launch
 *      with whatever is queued (padded with dummy slots so the block is still aligned).
 *   4. Non-blocking: submitRequest() never blocks. Overflow requests are dropped if queue
 *      reaches maxQueueDepth (back-pressure happens at TCP level, not here).
 *   5. Lock-free: multiple accept threads can call submitRequest() concurrently without
 *      any mutex contention using atomic operations.
 */

#include <array>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include "LockFreeQueue.h"

namespace symm {

// ── Per-request data bundled for a batch ─────────────────────────────────────

struct BatchRequest {
    int         fd;                 // client socket (response is written here)
    int         routeId;            // resolved route index
    std::string method;             // "GET", "POST", …
    std::string path;               // URL path
    std::string body;               // raw request body (may be empty)
    int64_t     arrivalUs;          // monotonic arrival timestamp (µs)
    // Request headers (populated from HTTP parse)
    std::string authorization;      // Authorization header value
    std::string contentType;        // Content-Type header value
    std::string userAgent;          // User-Agent header value
};

// ── Tuneable policy ──────────────────────────────────────────────────────────

struct BatchPolicy {
    // Preferred batch size — launch immediately when this many slots are filled.
    // Must be a multiple of 32 (warp width).  256 = 8 warps = good SM occupancy.
    int preferredBatchSize = 256;

    // Minimum useful batch size.  Below this we still fill to a multiple of 32.
    // Inactive threads (tid >= actual_count) are masked off with a single 'return'.
    int minBatchSize = 32;           // 1 warp minimum

    // Maximum wait time before launching an under-filled batch.
    int fillTimeoutUs = 5000;        // 5 ms default

    // Maximum number of queued requests per route before we apply back-pressure
    // to the accept thread (i.e. we stop accepting until some queues drain).
    int maxQueueDepth = 4096;

    // Number of route queues (must be ≥ number of registered routes).
    int maxRoutes = 256;
};

// ── Result of a batch launch, returned to the caller ─────────────────────────

struct BatchResult {
    std::vector<std::string> responses;     // one response body per request
    std::vector<int>         statuses;      // one HTTP status per request
    std::vector<std::string> respHeaders;   // one custom header block per request (may be empty)
};

// ── Launch callback type ─────────────────────────────────────────────────────
// The caller (GpuServer) provides this; BatchScheduler calls it when a batch
// is ready.  The callback must be fast — it runs on the scheduler thread.
//
// Signature:
//   void launch(routeId, requests, results_out)
//
// The callback fills `results_out.responses[i]` and `results_out.statuses[i]`
// for each request, then returns.  BatchScheduler then sends each response
// to its fd on a pool of sender threads.

using BatchLaunchFn = std::function<void(
    int                            routeId,
    const std::vector<BatchRequest>& requests,
    BatchResult&                   result
)>;

// ── BatchScheduler ────────────────────────────────────────────────────────────

class BatchScheduler {
public:
    static constexpr int WARP_WIDTH = 32;  // hardware constant — never change

    explicit BatchScheduler(BatchLaunchFn launchFn, BatchPolicy policy = {});
    ~BatchScheduler();

    // Submit a request.  Assigns it to the correct route queue.
    // May block briefly if that queue is at maxQueueDepth.
    void submitRequest(BatchRequest req);

    // Start the background scheduler thread.
    void start();

    // Drain all queues and stop.  Blocks until all in-flight batches complete.
    void stop();

    // Live metrics (lock-free reads — approximate, for monitoring only).
    struct Metrics {
        std::atomic<uint64_t> totalBatchesLaunched{0};
        std::atomic<uint64_t> totalRequestsProcessed{0};
        std::atomic<uint64_t> partialBatchesLaunched{0};  // launched before full
        std::atomic<uint64_t> fullBatchesLaunched{0};     // launched at capacity
        std::atomic<uint64_t> avgFillUs{0};               // rolling average fill time
    };
    const Metrics& metrics() const { return _metrics; }

private:
    // ── Per-route queue (lock-free) ───────────────────────────────────────────
    struct RouteQueue {
        LockFreeQueue<BatchRequest> pending;
        std::atomic<int64_t> firstArrivalUs{0};    // oldest pending request
        std::atomic<bool>    timerArmed{false};
        int     consecutiveFullBatches = 0;  // used for adaptive timeout (read by scheduler only)

        bool empty() const { return pending.empty(); }
        int  count() const { return (int)pending.size(); }
    };

    void   schedulerLoop();
    void   tryFlush(int routeId, std::vector<BatchRequest>& batch);
    void   launchBatch(int routeId, std::vector<BatchRequest>& requests);
    int    alignToBatchBoundary(int count) const;
    int64_t nowUs() const;

    BatchLaunchFn _launchFn;
    BatchPolicy   _policy;

    std::vector<RouteQueue> _queues;    // one per route slot (lock-free)
    std::thread             _thread;
    std::atomic<bool>       _stop{false};
    std::atomic<bool>       _running{false};

    Metrics _metrics;
};

} // namespace symm
