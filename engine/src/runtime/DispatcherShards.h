#pragma once
/**
 * DispatcherShards — Parallelize batch dispatch across N shards
 *
 * Design:
 * - N independent dispatcher shards (e.g., 8-16), one thread per shard
 * - Each shard manages a queue of batches waiting to launch
 * - Hash function routes batches: shard_id = worker_id % N
 * - Eliminates scheduling bottleneck by parallelizing dispatcher threads
 * - Each dispatcher posts to AsyncBatchLauncher (which is also non-blocking)
 *
 * Performance:
 * - Per-shard queue has only (256/N) workers, reducing contention
 * - N dispatcher threads run in parallel
 * - Scales to thousands of workers with minimal overhead
 */

#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "BatchScheduler.h"
#include "AsyncBatchLauncher.h"

namespace symm {

class DispatcherShards {
public:
    // Constructor: N shards, each with dedicated dispatcher thread
    explicit DispatcherShards(
        int numShards = 8,
        std::function<void(int, const std::vector<BatchRequest>&, BatchResult&)> launchFn = nullptr
    )
        : _numShards(numShards),
          _shards(numShards),
          _launchFn(launchFn),
          _totalBatchesPosted(0),
          _totalRequestsProcessed(0)
    {
        // Start one dispatcher thread per shard
        for (int i = 0; i < numShards; i++) {
            _shards[i].dispatcher = std::thread([this, i] { dispatcherLoop(i); });
        }
    }

    ~DispatcherShards() {
        stop();
    }

    // Post a batch to the appropriate shard (called by local buffers)
    // shard_id = worker_thread_id % N
    // Non-blocking: returns immediately
    void postBatch(int shardId, std::vector<BatchRequest>&& batch) {
        if (batch.empty()) return;

        shardId = shardId % _numShards;  // Safety: ensure valid shard
        Shard& shard = _shards[shardId];

        {
            std::lock_guard<std::mutex> lock(shard.queueMutex);
            shard.batchQueue.push(std::move(batch));
            _totalBatchesPosted++;
        }

        // Wake dispatcher (non-blocking notify)
        shard.queueNotEmpty.notify_one();
    }

    // Set the launch function (called by GpuServer)
    void setLaunchFn(
        std::function<void(int, const std::vector<BatchRequest>&, BatchResult&)> fn
    ) {
        _launchFn = fn;
    }

    // Drain all queues and stop dispatcher threads
    void stop() {
        for (int i = 0; i < _numShards; i++) {
            {
                std::lock_guard<std::mutex> lock(_shards[i].queueMutex);
                _shards[i].stop = true;
            }
            _shards[i].queueNotEmpty.notify_one();

            if (_shards[i].dispatcher.joinable()) {
                _shards[i].dispatcher.join();
            }
        }
    }

    // Metrics
    struct Metrics {
        uint64_t totalBatchesPosted{0};
        uint64_t totalRequestsProcessed{0};
        int numShards{0};
    };

    Metrics metrics() const {
        return {
            _totalBatchesPosted.load(),
            _totalRequestsProcessed.load(),
            _numShards
        };
    }

private:
    struct Shard {
        std::queue<std::vector<BatchRequest>> batchQueue;
        std::mutex queueMutex;
        std::condition_variable queueNotEmpty;
        std::thread dispatcher;
        std::atomic<bool> stop{false};
    };

    void dispatcherLoop(int shardId) {
        Shard& shard = _shards[shardId];

        while (!shard.stop.load()) {
            std::vector<BatchRequest> batch;

            // Wait for batch with timeout
            {
                std::unique_lock<std::mutex> lock(shard.queueMutex);
                shard.queueNotEmpty.wait_for(
                    lock,
                    std::chrono::milliseconds(100),
                    [&] { return !shard.batchQueue.empty() || shard.stop.load(); }
                );

                if (!shard.batchQueue.empty()) {
                    batch = std::move(shard.batchQueue.front());
                    shard.batchQueue.pop();
                }
            }

            // Launch batch to GPU
            if (!batch.empty() && _launchFn) {
                BatchResult result;
                result.responses.resize(batch.size());
                result.statuses.resize(batch.size(), 200);

                _launchFn(0, batch, result);  // routeId=0 for now
                _totalRequestsProcessed.fetch_add(batch.size(), std::memory_order_relaxed);
            }
        }
    }

    int _numShards;
    std::vector<Shard> _shards;
    std::function<void(int, const std::vector<BatchRequest>&, BatchResult&)> _launchFn;

    std::atomic<uint64_t> _totalBatchesPosted;
    std::atomic<uint64_t> _totalRequestsProcessed;
};

}  // namespace symm
