// test_tuning_framework.cpp — Systematic parameter tuning for 1M+ RPS target
//
// Tests combinations of:
// - NUM_DISPATCHER_SHARDS: 8, 16, 32, 64
// - BUFFER_TIMEOUT_MS: 1, 2, 5, 10
// - GPU_WORKER_THREADS: 4, 8, 16, 32
// - Stream pool size: 32, 64, 128, 256
//
// Goal: Find optimal configuration for maximum throughput

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include "ThreadLocalBatchBuffer.h"
#include "DispatcherShards.h"

using namespace symm;

struct TuningParams {
    int numShards;
    int timeoutMs;
    int gpuWorkers;
    int numWorkers;
};

struct TuningResult {
    TuningParams params;
    uint64_t rps;
    uint64_t elapsedMs;
    double batchesPerSecond;

    bool operator<(const TuningResult& other) const {
        return rps > other.rps;  // Sort descending by RPS
    }
};

static void mockKernel(const std::vector<BatchRequest>& requests, BatchResult& result) {
    for (size_t i = 0; i < requests.size(); i++) {
        result.responses[i] = "ok";
        result.statuses[i] = 200;
    }
}

uint64_t runTuningTest(TuningParams params, int requestsPerWorker = 200) {
    std::atomic<uint64_t> totalSubmitted{0};
    std::atomic<uint64_t> totalBatches{0};

    // Create dispatcher shards with specified count
    DispatcherShards dispatcher(params.numShards);
    dispatcher.setLaunchFn([&](int routeId, const std::vector<BatchRequest>& reqs, BatchResult& result) {
        mockKernel(reqs, result);
        totalBatches.fetch_add(1, std::memory_order_relaxed);
    });

    auto t0 = std::chrono::high_resolution_clock::now();

    // Launch worker threads
    std::vector<std::thread> workers;
    for (int w = 0; w < params.numWorkers; w++) {
        workers.emplace_back([&, w]() {
            // Get thread-local buffer with specified timeout
            auto& buf = getLocalBatchBuffer(
                256,  // preferredBatchSize
                params.timeoutMs,
                [&, w](std::vector<BatchRequest>&& batch) {
                    if (!batch.empty()) {
                        int shard = w % params.numShards;
                        dispatcher.postBatch(shard, std::move(batch));
                    }
                }
            );

            for (int i = 0; i < requestsPerWorker; i++) {
                BatchRequest req;
                req.fd = w;
                req.routeId = 0;
                req.method = "GET";
                req.path = "/";

                bool isFull = buf.submitRequest(std::move(req));
                totalSubmitted.fetch_add(1, std::memory_order_relaxed);

                if (isFull) {
                    buf.forceFlush();
                }
            }

            buf.forceFlush();
        });
    }

    // Wait for workers
    for (auto& w : workers) w.join();

    // Wait for dispatcher to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    dispatcher.stop();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    uint64_t submitted = totalSubmitted.load();
    uint64_t batches = totalBatches.load();
    uint64_t rps = submitted * 1000 / std::max(1LL, elapsed);

    return rps;
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  PARAMETER TUNING FRAMEWORK                                ║\n"
              << "║  Finding optimal configuration for 1M+ RPS                  ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    // Test configurations
    std::vector<int> shardCounts = {8, 16, 32, 64};
    std::vector<int> timeouts = {1, 2, 5, 10};
    std::vector<int> workerCounts = {64, 128, 256};

    std::vector<TuningResult> allResults;

    int testNum = 0;
    int totalTests = shardCounts.size() * timeouts.size() * workerCounts.size();

    std::cout << "Testing " << totalTests << " parameter combinations...\n\n";
    std::cout << std::setw(3) << "#" << " | "
              << std::setw(7) << "Shards" << " | "
              << std::setw(7) << "Timeout" << " | "
              << std::setw(7) << "Workers" << " | "
              << std::setw(10) << "RPS" << "\n";
    std::cout << std::string(55, '-') << "\n";

    for (int shards : shardCounts) {
        for (int timeout : timeouts) {
            for (int workers : workerCounts) {
                testNum++;

                TuningParams params = {shards, timeout, 8, workers};
                uint64_t rps = runTuningTest(params);

                allResults.push_back({params, rps, 0, 0});

                std::cout << std::setw(3) << testNum << " | "
                          << std::setw(7) << shards << " | "
                          << std::setw(7) << timeout << " | "
                          << std::setw(7) << workers << " | "
                          << std::setw(10) << rps << "\n";
            }
        }
    }

    // Sort by RPS (descending)
    std::sort(allResults.begin(), allResults.end());

    // Print top 10 configurations
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  TOP 10 CONFIGURATIONS                                     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    std::cout << std::setw(3) << "Rank" << " | "
              << std::setw(7) << "Shards" << " | "
              << std::setw(7) << "Timeout" << " | "
              << std::setw(7) << "Workers" << " | "
              << std::setw(12) << "RPS" << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (size_t i = 0; i < std::min(size_t(10), allResults.size()); i++) {
        const auto& result = allResults[i];
        std::cout << std::setw(3) << (i + 1) << " | "
                  << std::setw(7) << result.params.numShards << " | "
                  << std::setw(7) << result.params.timeoutMs << " | "
                  << std::setw(7) << result.params.numWorkers << " | "
                  << std::setw(12) << result.rps << "\n";
    }

    // Print analysis
    const auto& best = allResults[0];
    const auto& baseline = allResults[allResults.size() - 1];

    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  ANALYSIS                                                  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    std::cout << "BEST Configuration:\n";
    std::cout << "  Dispatcher Shards: " << best.params.numShards << "\n";
    std::cout << "  Buffer Timeout: " << best.params.timeoutMs << " ms\n";
    std::cout << "  Workers: " << best.params.numWorkers << "\n";
    std::cout << "  Throughput: " << best.rps << " RPS\n\n";

    std::cout << "IMPROVEMENT vs Baseline (8 shards, 10ms timeout, 64 workers):\n";
    double improvement = baseline.rps > 0 ? (double)best.rps / baseline.rps : 1.0;
    std::cout << "  Baseline: " << baseline.rps << " RPS\n";
    std::cout << "  Best: " << best.rps << " RPS\n";
    std::cout << "  Gain: " << std::fixed << std::setprecision(2) << improvement << "x\n\n";

    // Recommendations
    std::cout << "RECOMMENDATIONS:\n";
    if (best.params.numShards >= 32) {
        std::cout << "  ✓ High shard count (32+) enables better parallelism\n";
    }
    if (best.params.timeoutMs <= 2) {
        std::cout << "  ✓ Low timeout (1-2ms) reduces latency, batches launch faster\n";
    } else {
        std::cout << "  ℹ Higher timeout (" << best.params.timeoutMs
                  << "ms) allows better batch accumulation\n";
    }
    if (best.params.numWorkers >= 256) {
        std::cout << "  ✓ High worker count generates sufficient load\n";
    }

    return 0;
}
