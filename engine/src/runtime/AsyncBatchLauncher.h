#pragma once
/**
 * AsyncBatchLauncher — Non-blocking batch launch queue
 *
 * Decouples batch scheduling from CUDA stream acquisition.
 * Prevents scheduler thread from blocking on stream availability.
 *
 * Architecture:
 * - Scheduler posts batches to queue (lock-free, non-blocking)
 * - Worker threads pull batches and launch to available streams
 * - No head-of-line blocking between requests
 * - Scheduler can post batches 1000s per second without waiting
 */

#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "BatchScheduler.h"

namespace symm {

struct BatchWorkItem {
    int routeId;
    std::vector<BatchRequest> requests;
    std::shared_ptr<BatchResult> result;  // shared ownership of result buffer
    std::function<void(const std::vector<BatchRequest>&, const BatchResult&)> onComplete;
};

class AsyncBatchLauncher {
public:
    explicit AsyncBatchLauncher(int numWorkers = 4);
    ~AsyncBatchLauncher();

    // Post a batch for async launching (non-blocking, never waits)
    // Returns immediately; actual launch happens on worker thread
    void postBatch(BatchWorkItem&& item);

    // Set the actual batch launch function (from GpuServer::batchLaunch)
    void setLaunchFn(std::function<void(int, const std::vector<BatchRequest>&, BatchResult&)> fn) {
        _launchFn = fn;
    }

    // Drain queue and stop workers
    void stop();

    // Live metrics
    struct Metrics {
        std::atomic<uint64_t> postsReceived{0};
        std::atomic<uint64_t> launchesCompleted{0};
        std::atomic<uint64_t> currentQueueDepth{0};
    };

    const Metrics& metrics() const { return _metrics; }

private:
    void workerLoop();

    std::queue<BatchWorkItem> _queue;
    std::mutex _queueMutex;
    std::condition_variable _queueNotEmpty;

    std::vector<std::thread> _workers;
    std::atomic<bool> _stop{false};
    std::function<void(int, const std::vector<BatchRequest>&, BatchResult&)> _launchFn;

    Metrics _metrics;
};

} // namespace symm
