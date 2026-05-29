#include "BatchScheduler.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace symm {

// ── Constructor / Destructor ──────────────────────────────────────────────────

BatchScheduler::BatchScheduler(BatchLaunchFn launchFn, BatchPolicy policy)
    : _launchFn(std::move(launchFn))
    , _policy(policy)
    , _queues(policy.maxRoutes)
{}

BatchScheduler::~BatchScheduler() {
    stop();
}

// ── Public API ────────────────────────────────────────────────────────────────

void BatchScheduler::start() {
    _stop.store(false);
    _running.store(true);
    _thread = std::thread([this]{ schedulerLoop(); });
}

void BatchScheduler::stop() {
    if (!_running.exchange(false)) return;
    _stop.store(true);
    if (_thread.joinable()) _thread.join();
}

void BatchScheduler::submitRequest(BatchRequest req) {
    int rid = req.routeId;
    if (rid < 0 || rid >= _policy.maxRoutes) {
        // Unknown route — skip (will produce 404 from default dispatch)
        rid = 0;
    }

    auto& q = _queues[rid];

    // Non-blocking enqueue — if queue is full, drop the request.
    // Back-pressure happens at TCP level (kernel socket buffer), not here.
    if (!q.pending.tryEnqueue(std::move(req))) {
        // Queue overflow — request dropped
        fprintf(stderr, "[submitRequest] QUEUE OVERFLOW on routeId=%d, queueSize=%d\n", rid, q.count());
        fflush(stderr);
        return;
    }

    fprintf(stderr, "[submitRequest] Request submitted to route %d, queueSize=%d\n", rid, q.count());
    fflush(stderr);

    // Record first arrival time (atomic, lock-free)
    if (q.count() == 1) {
        q.firstArrivalUs.store(nowUs(), std::memory_order_relaxed);
        q.timerArmed.store(true, std::memory_order_relaxed);
        fprintf(stderr, "[submitRequest] First request in queue, timer armed\n");
        fflush(stderr);
    }

    // Signal scheduler to wake immediately and evaluate batches
    // No lock needed for notify_one() — it's thread-safe
    // Lock-free signaling eliminates contention bottleneck
    _requestSignal.notify_one();
}

// ── Scheduler loop ────────────────────────────────────────────────────────────

void BatchScheduler::schedulerLoop() {
    while (!_stop.load()) {
        // Non-blocking poll of all route queues.
        // No mutexes, no condition variables — just check deadline and flush.

        int64_t nowMicros = nowUs();
        int64_t nextDeadlineUs = INT64_MAX;
        bool anyWork = false;

        // Scan all routes for work
        for (int rid = 0; rid < _policy.maxRoutes; rid++) {
            auto& q = _queues[rid];
            if (q.empty()) continue;
            anyWork = true;

            // Already at preferred capacity → flush immediately (no wait)
            if (q.count() >= _policy.preferredBatchSize) {
                std::vector<BatchRequest> batch;
                int batchSize = alignToBatchBoundary(_policy.preferredBatchSize);
                batch.reserve(batchSize);
                q.pending.tryDequeueBatch(batch, _policy.preferredBatchSize);
                if (!batch.empty()) {
                    launchBatch(rid, batch);
                }
                continue;
            }

            // Compute when this queue's deadline expires
            int64_t firstArrival = q.firstArrivalUs.load(std::memory_order_relaxed);
            if (firstArrival > 0) {
                int64_t deadlineUs = firstArrival + _policy.fillTimeoutUs;
                nextDeadlineUs = std::min(nextDeadlineUs, deadlineUs);
            }
        }

        // Check for expired timeouts and flush any partial batches
        if (anyWork) {
            nowMicros = nowUs();
            for (int rid = 0; rid < _policy.maxRoutes; rid++) {
                auto& q = _queues[rid];
                if (q.empty()) continue;

                int64_t firstArrival = q.firstArrivalUs.load(std::memory_order_relaxed);
                if (firstArrival > 0) {
                    int64_t deadline = firstArrival + _policy.fillTimeoutUs;
                    if (deadline <= nowMicros) {
                        // Timeout expired — flush whatever we have (warp-aligned)
                        std::vector<BatchRequest> batch;
                        int cnt = q.count();
                        int paddedSize = alignToBatchBoundary(cnt);
                        batch.reserve(paddedSize);
                        q.pending.tryDequeueBatch(batch, cnt);
                        fprintf(stderr, "[schedulerLoop] Route %d: timeout expired, dequeued %zu requests\n", rid, batch.size());
                        fflush(stderr);
                        if (!batch.empty()) {
                            fprintf(stderr, "[schedulerLoop] Launching batch with %zu requests on route %d\n", batch.size(), rid);
                            fflush(stderr);
                            launchBatch(rid, batch);
                        }
                        q.firstArrivalUs.store(0, std::memory_order_relaxed);
                        q.timerArmed.store(false, std::memory_order_relaxed);
                    }
                }
            }
        }

        // Event-based wait: wake immediately on new request, or timeout after 1ms
        // This eliminates the 200µs polling delay and ensures fast response to bursts
        {
            std::unique_lock<std::mutex> lock(_signalMutex);
            _requestSignal.wait_for(lock, std::chrono::microseconds(_policy.fillTimeoutUs));
        }
    }

    // Drain: flush all remaining queued requests before exiting
    for (int rid = 0; rid < _policy.maxRoutes; rid++) {
        auto& q = _queues[rid];
        if (!q.empty()) {
            std::vector<BatchRequest> batch;
            int cnt = q.count();
            batch.reserve(cnt);
            q.pending.tryDequeueBatch(batch, cnt);
            if (!batch.empty()) {
                launchBatch(rid, batch);
            }
        }
    }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void BatchScheduler::tryFlush(int routeId, std::vector<BatchRequest>& batch) {
    // Non-blocking flush — dequeues available items without holding a lock
    auto& q = _queues[routeId];
    if (q.empty()) return;

    int avail = q.count();
    int target = _policy.preferredBatchSize;
    int batchSize = std::min(avail, target);

    if (batchSize < _policy.minBatchSize) {
        // Not enough yet — keep waiting
        return;
    }

    // Dequeue up to batchSize items (lock-free operation)
    batch.clear();
    batch.reserve(batchSize);
    q.pending.tryDequeueBatch(batch, batchSize);

    if (batch.empty()) return;

    // Track whether this was a full or partial batch for metrics
    bool wasFull = (batch.size() >= (size_t)_policy.preferredBatchSize);
    if (wasFull) {
        q.consecutiveFullBatches++;
        _metrics.fullBatchesLaunched.fetch_add(1, std::memory_order_relaxed);
    } else {
        q.consecutiveFullBatches = 0;
        _metrics.partialBatchesLaunched.fetch_add(1, std::memory_order_relaxed);
    }

    // Reset timer for remaining items
    if (q.empty()) {
        q.timerArmed.store(false, std::memory_order_relaxed);
        q.firstArrivalUs.store(0, std::memory_order_relaxed);
    } else {
        q.firstArrivalUs.store(nowUs(), std::memory_order_relaxed);
    }
}

void BatchScheduler::launchBatch(int routeId, std::vector<BatchRequest>& requests) {
    int64_t t0 = nowUs();

    BatchResult result;
    result.responses.resize(requests.size());
    result.statuses.resize(requests.size(), 200);

    // Call the GPU launch callback (provided by GpuServer)
    _launchFn(routeId, requests, result);

    // --- Send responses back to clients ---
    // Each request carries its fd; we write the response directly.
    // In production GpuServer this is done on a pool of sender threads,
    // but the BatchScheduler doesn't need to know that detail.
    for (size_t i = 0; i < requests.size(); i++) {
        if (requests[i].fd >= 0 && i < result.responses.size()) {
            // GpuServer's sender thread pool picks this up.
            // Here we just surface the (fd, response, status) tuple.
            // Actual socket write is in GpuServer::sendBatchResponses().
        }
    }

    // Update metrics
    int64_t elapsed = nowUs() - t0;
    _metrics.totalBatchesLaunched.fetch_add(1, std::memory_order_relaxed);
    _metrics.totalRequestsProcessed.fetch_add(requests.size(), std::memory_order_relaxed);

    // Rolling average of fill time (simple EMA)
    uint64_t prev = _metrics.avgFillUs.load(std::memory_order_relaxed);
    uint64_t next = (prev == 0) ? (uint64_t)elapsed : (prev * 7 + (uint64_t)elapsed) / 8;
    _metrics.avgFillUs.store(next, std::memory_order_relaxed);
}

int BatchScheduler::alignToBatchBoundary(int count) const {
    // Round up to nearest multiple of WARP_WIDTH (32).
    // This ensures the GPU block size is always warp-aligned.
    if (count <= 0) return WARP_WIDTH;
    return ((count + WARP_WIDTH - 1) / WARP_WIDTH) * WARP_WIDTH;
}

int64_t BatchScheduler::nowUs() const {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace symm
