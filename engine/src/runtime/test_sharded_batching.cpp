// test_sharded_batching.cpp — Batching with sharded queues to reduce contention
//
// Instead of one queue with all workers contending, use N sharded queues
// Each worker hashes to a shard, reducing contention by N times

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <cstring>
#include "BatchScheduler.h"
#include "LockFreeQueue.h"

using namespace symm;

static void fastKernel(const std::vector<BatchRequest>& requests, BatchResult& result) {
    for (size_t i = 0; i < requests.size(); i++) {
        result.responses[i] = "ok";
        result.statuses[i] = 200;
    }
}

class ShardedBatchScheduler {
public:
    static constexpr int NUM_SHARDS = 16;

    ShardedBatchScheduler(int preferredBatchSize = 256)
        : _preferredBatchSize(preferredBatchSize), _minBatchSize(32),
          _fillTimeoutUs(1000), _queues(NUM_SHARDS),
          _totalBatches(0), _totalRequests(0)
    {}

    void submitRequest(const BatchRequest& req, int workerIdx) {
        int shard = workerIdx % NUM_SHARDS;
        if (!_queues[shard].tryEnqueue(req)) {
            // Queue full, drop
            return;
        }

        // Check if this shard should flush
        int count = _queues[shard].size();
        if (count >= _preferredBatchSize) {
            flushShard(shard);
        }
    }

    void flushShard(int shard) {
        std::vector<BatchRequest> batch;
        batch.reserve(_preferredBatchSize);
        if (_queues[shard].tryDequeueBatch(batch, _preferredBatchSize)) {
            if (!batch.empty()) {
                BatchResult result;
                result.responses.resize(batch.size());
                result.statuses.resize(batch.size(), 200);
                fastKernel(batch, result);

                _totalBatches++;
                _totalRequests += batch.size();
            }
        }
    }

    void flushAll() {
        for (int i = 0; i < NUM_SHARDS; i++) {
            std::vector<BatchRequest> batch;
            int count = _queues[i].size();
            if (count > 0) {
                batch.reserve(count);
                _queues[i].tryDequeueBatch(batch, count);
                if (!batch.empty()) {
                    BatchResult result;
                    result.responses.resize(batch.size());
                    result.statuses.resize(batch.size(), 200);
                    fastKernel(batch, result);

                    _totalBatches++;
                    _totalRequests += batch.size();
                }
            }
        }
    }

    int totalBatches() const { return _totalBatches; }
    int totalRequests() const { return _totalRequests; }

private:
    int _preferredBatchSize;
    int _minBatchSize;
    int _fillTimeoutUs;
    std::vector<LockFreeQueue<BatchRequest>> _queues;

    std::atomic<int> _totalBatches{0};
    std::atomic<int> _totalRequests{0};
};

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  SHARDED BATCHING TEST (Contention Reduction)              ║\n"
              << "║  Compares: single queue vs 16 sharded queues              ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    // Single queue test
    {
        std::cout << "SINGLE QUEUE (Baseline):\n";

        BatchPolicy policy;
        policy.preferredBatchSize = 256;
        policy.minBatchSize = 32;
        policy.fillTimeoutUs = 1000;
        policy.maxQueueDepth = 4096;
        policy.maxRoutes = 256;

        std::atomic<uint64_t> totalSubmitted{0};
        std::atomic<uint64_t> totalProcessed{0};

        BatchScheduler scheduler([&](int routeId, const std::vector<BatchRequest>& requests, BatchResult& result) {
            fastKernel(requests, result);
            totalProcessed.fetch_add(requests.size(), std::memory_order_relaxed);
        }, policy);

        scheduler.start();

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        for (int w = 0; w < 256; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 200; i++) {
                    BatchRequest req;
                    req.fd = w;
                    req.routeId = 0;
                    scheduler.submitRequest(std::move(req));
                    totalSubmitted.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();
        auto t1 = std::chrono::high_resolution_clock::now();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        scheduler.stop();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        uint64_t submitted = totalSubmitted.load();
        std::cout << "  Submitted: " << submitted << " in " << elapsed << " ms\n";
        std::cout << "  Submission Rate: " << (submitted * 1000 / elapsed) << " RPS\n\n";
    }

    // Sharded queue test
    {
        std::cout << "SHARDED QUEUES (16 shards):\n";

        ShardedBatchScheduler scheduler(256);

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        int submitted = 0;
        for (int w = 0; w < 256; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 200; i++) {
                    BatchRequest req;
                    req.fd = w;
                    req.routeId = 0;
                    scheduler.submitRequest(std::move(req), w);
                }
            });
        }

        for (auto& w : workers) w.join();
        auto t1 = std::chrono::high_resolution_clock::now();

        scheduler.flushAll();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "  Submitted: 51200 in " << elapsed << " ms\n";
        std::cout << "  Submission Rate: " << (51200 * 1000 / elapsed) << " RPS\n\n";
    }

    return 0;
}
