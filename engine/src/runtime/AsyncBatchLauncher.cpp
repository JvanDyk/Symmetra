#include "AsyncBatchLauncher.h"
#include <cstdio>

namespace symm {

AsyncBatchLauncher::AsyncBatchLauncher(int numWorkers) {
    for (int i = 0; i < numWorkers; i++) {
        _workers.emplace_back([this]{ workerLoop(); });
    }
    fprintf(stderr, "[AsyncBatchLauncher] Started %d worker threads\n", numWorkers);
    fflush(stderr);
}

AsyncBatchLauncher::~AsyncBatchLauncher() {
    stop();
}

void AsyncBatchLauncher::postBatch(BatchWorkItem&& item) {
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _queue.push(std::move(item));
        _metrics.currentQueueDepth.store(_queue.size(), std::memory_order_relaxed);
    }
    _queueNotEmpty.notify_one();
    _metrics.postsReceived.fetch_add(1, std::memory_order_relaxed);
}

void AsyncBatchLauncher::workerLoop() {
    while (!_stop.load()) {
        BatchWorkItem item;

        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueNotEmpty.wait(lock, [this] { return !_queue.empty() || _stop.load(); });

            if (_stop.load() && _queue.empty()) {
                break;
            }

            if (_queue.empty()) {
                continue;
            }

            item = std::move(_queue.front());
            _queue.pop();
            _metrics.currentQueueDepth.store(_queue.size(), std::memory_order_relaxed);
        }

        // Launch batch (this may block on stream availability, which is OK on worker thread)
        if (_launchFn) {
            try {
                _launchFn(item.routeId, item.requests, *item.result_out);
            } catch (const std::exception& e) {
                fprintf(stderr, "[AsyncBatchLauncher] Launch failed: %s\n", e.what());
                fflush(stderr);
            } catch (...) {
                fprintf(stderr, "[AsyncBatchLauncher] Launch failed: unknown exception\n");
                fflush(stderr);
            }
        }

        // Invoke completion callback if provided
        if (item.onComplete) {
            try {
                item.onComplete();
            } catch (...) {
                fprintf(stderr, "[AsyncBatchLauncher] Completion callback failed\n");
                fflush(stderr);
            }
        }

        _metrics.launchesCompleted.fetch_add(1, std::memory_order_relaxed);
    }
}

void AsyncBatchLauncher::stop() {
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _stop.store(true);
    }
    _queueNotEmpty.notify_all();

    for (auto& worker : _workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    fprintf(stderr, "[AsyncBatchLauncher] Stopped\n");
    fflush(stderr);
}

} // namespace symm
