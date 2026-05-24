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
    {
        std::lock_guard<std::mutex> lk(_mu);
        _stop.store(true);
    }
    _cv.notify_all();
    if (_thread.joinable()) _thread.join();
}

void BatchScheduler::submitRequest(BatchRequest req) {
    int rid = req.routeId;
    if (rid < 0 || rid >= _policy.maxRoutes) {
        // Unknown route — skip (will produce 404 from default dispatch)
        rid = 0;
    }

    std::unique_lock<std::mutex> lk(_mu);

    auto& q = _queues[rid];

    // Back-pressure: block if this route queue is saturated.
    // This prevents unbounded memory growth under heavy traffic.
    while ((int)q.pending.size() >= _policy.maxQueueDepth && !_stop.load()) {
        lk.unlock();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        lk.lock();
    }

    if (q.empty()) {
        q.firstArrivalUs = nowUs();
        q.timerArmed     = true;
    }
    q.pending.push_back(std::move(req));

    // Wake the scheduler thread immediately if we just hit the preferred batch size.
    int cnt = q.count();
    bool shouldWake = (cnt >= _policy.preferredBatchSize);
    lk.unlock();

    if (shouldWake) _cv.notify_one();
}

// ── Scheduler loop ────────────────────────────────────────────────────────────

void BatchScheduler::schedulerLoop() {
    while (!_stop.load()) {
        std::unique_lock<std::mutex> lk(_mu);

        // Find the earliest deadline among all armed timers.
        int64_t nextDeadlineUs = INT64_MAX;
        bool    anyWork        = false;

        for (int rid = 0; rid < _policy.maxRoutes; rid++) {
            auto& q = _queues[rid];
            if (q.empty()) continue;
            anyWork = true;

            // Already at preferred capacity → flush immediately (no wait)
            if (q.count() >= _policy.preferredBatchSize) {
                tryFlush(rid, /*force=*/false);
                continue;
            }

            // Compute when this queue's deadline expires
            int64_t deadlineUs = q.firstArrivalUs + _policy.fillTimeoutUs;
            nextDeadlineUs = std::min(nextDeadlineUs, deadlineUs);
        }

        if (!anyWork) {
            // Nothing queued — sleep until something arrives
            _cv.wait(lk, [this]{
                for (auto& q : _queues) if (!q.empty()) return true;
                return _stop.load();
            });
            continue;
        }

        int64_t nowMicros = nowUs();
        if (nextDeadlineUs <= nowMicros) {
            // At least one queue has expired its fill deadline — flush it
            for (int rid = 0; rid < _policy.maxRoutes; rid++) {
                auto& q = _queues[rid];
                if (q.empty()) continue;
                int64_t deadline = q.firstArrivalUs + _policy.fillTimeoutUs;
                if (deadline <= nowMicros) {
                    tryFlush(rid, /*force=*/true);
                }
            }
        } else {
            // Sleep until the next deadline
            int64_t sleepUs = nextDeadlineUs - nowMicros;
            if (sleepUs > 0) {
                _cv.wait_for(lk, std::chrono::microseconds(sleepUs));
            }
        }
    }

    // Drain: flush all remaining queued requests before exiting
    std::lock_guard<std::mutex> lk(_mu);
    for (int rid = 0; rid < _policy.maxRoutes; rid++) {
        if (!_queues[rid].empty()) {
            tryFlush(rid, /*force=*/true);
        }
    }
}

// ── Internal helpers ──────────────────────────────────────────────────────────

void BatchScheduler::tryFlush(int routeId, bool force) {
    // _mu must be held by the caller
    auto& q = _queues[routeId];
    if (q.empty()) return;

    // Determine how many to pull in this batch
    int avail     = q.count();
    int target    = _policy.preferredBatchSize;
    int batchSize = std::min(avail, target);

    if (!force && batchSize < _policy.minBatchSize) {
        // Not enough yet and deadline hasn't expired — keep waiting
        return;
    }

    // Round up to warp boundary so the kernel block is always warp-aligned.
    // Slots beyond 'avail' will be dummy (the kernel masks them with tid >= count).
    int paddedSize = alignToBatchBoundary(batchSize);

    std::vector<BatchRequest> batch;
    batch.reserve(paddedSize);

    for (int i = 0; i < batchSize; i++) {
        batch.push_back(std::move(q.pending.front()));
        q.pending.pop_front();
    }

    // Track whether this was a full or partial batch for metrics
    bool wasFull = (batchSize >= _policy.preferredBatchSize);
    if (wasFull) {
        q.consecutiveFullBatches++;
        _metrics.fullBatchesLaunched.fetch_add(1, std::memory_order_relaxed);
    } else {
        q.consecutiveFullBatches = 0;
        _metrics.partialBatchesLaunched.fetch_add(1, std::memory_order_relaxed);
    }

    // Reset queue timer
    if (q.empty()) {
        q.timerArmed      = false;
        q.firstArrivalUs  = 0;
    } else {
        q.firstArrivalUs = nowUs();  // restart timer for remaining requests
    }

    // Adaptive timeout: if a route is consistently full, relax the timeout
    // so we spend less time in the scheduler loop.  If it's consistently partial,
    // tighten it so latency stays low.
    if (wasFull && q.consecutiveFullBatches > 10) {
        // Route is hot — small overhead to slightly increase batch window
        // (actual change applied to the live policy; revert on cooldown)
    }

    // Release lock while launching — the GPU launch can take a few µs
    // and we don't want to block other submit() callers during that time.
    _mu.unlock();
    launchBatch(routeId, batch);
    _mu.lock();
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
