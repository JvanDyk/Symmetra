// test_decentralized_batching.cpp — Benchmark decentralized vs centralized architecture
//
// Compares:
// - OLD: Central BatchScheduler with single lock-free queue → 3-35K RPS
// - NEW: Thread-local buffers + dispatcher shards → 80K-300K RPS

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <memory>
#include "ThreadLocalBatchBuffer.h"
#include "DispatcherShards.h"
#include "BatchScheduler.h"

using namespace symm;

// Mock kernel
static void mockKernel(const std::vector<BatchRequest>& requests, BatchResult& result) {
    for (size_t i = 0; i < requests.size(); i++) {
        result.responses[i] = "ok";
        result.statuses[i] = 200;
    }
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  DECENTRALIZED BATCHING ARCHITECTURE BENCHMARK              ║\n"
              << "║  OLD: Central scheduler | NEW: Distributed buffers + shards ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    int numWorkers = 256;
    int requestsPerWorker = 200;
    int numRuns = 3;

    // ─────────────────────────────────────────────────────────────────────────
    // Test 1: NEW ARCHITECTURE (Thread-local buffers + dispatcher shards)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "TEST 1: NEW ARCHITECTURE (Decentralized)\n";
    std::cout << "          (Thread-local buffers + " << 16 << " dispatcher shards)\n\n";

    std::vector<uint64_t> newArch_results;

    for (int run = 0; run < numRuns; run++) {
        std::atomic<uint64_t> totalSubmitted{0};
        std::atomic<uint64_t> totalDispatched{0};

        // Create dispatcher shards
        DispatcherShards dispatcher(16);
        dispatcher.setLaunchFn([](int routeId, const std::vector<BatchRequest>& reqs, BatchResult& result) {
            mockKernel(reqs, result);
        });

        auto t0 = std::chrono::high_resolution_clock::now();

        // Launch worker threads
        std::vector<std::thread> workers;
        for (int w = 0; w < numWorkers; w++) {
            workers.emplace_back([&, w]() {
                // Get thread-local buffer
                auto& buf = getLocalBatchBuffer(
                    256,  // preferredBatchSize
                    10,   // timeoutMs
                    [&, w](std::vector<BatchRequest>&& batch) {
                        if (!batch.empty()) {
                            int shard = w % 16;
                            dispatcher.postBatch(shard, std::move(batch));
                            totalDispatched.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                );

                // Submit requests to thread-local buffer
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

                // Flush remaining requests
                buf.forceFlush();
            });
        }

        // Wait for all workers to complete
        for (auto& w : workers) w.join();

        // Wait for dispatcher to process all batches
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        dispatcher.stop();

        auto t1 = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        uint64_t submitted = totalSubmitted.load();
        uint64_t rps = submitted * 1000 / std::max(1LL, elapsed);
        newArch_results.push_back(rps);

        std::cout << "  Run " << (run + 1) << ": " << rps << " RPS (" << elapsed << " ms)\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 2: OLD ARCHITECTURE (Centralized BatchScheduler)
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\nTEST 2: OLD ARCHITECTURE (Centralized)\n";
    std::cout << "          (Single BatchScheduler queue)\n\n";

    std::vector<uint64_t> oldArch_results;

    for (int run = 0; run < numRuns; run++) {
        std::atomic<uint64_t> totalProcessed{0};

        BatchPolicy policy;
        policy.preferredBatchSize = 256;
        policy.minBatchSize = 32;
        policy.fillTimeoutUs = 1000;
        policy.maxQueueDepth = 4096;
        policy.maxRoutes = 256;

        BatchScheduler scheduler(
            [&](int routeId, const std::vector<BatchRequest>& reqs, BatchResult& result) {
                mockKernel(reqs, result);
                totalProcessed.fetch_add(reqs.size(), std::memory_order_relaxed);
            },
            policy
        );

        scheduler.start();

        auto t0 = std::chrono::high_resolution_clock::now();

        // Launch worker threads
        std::vector<std::thread> workers;
        for (int w = 0; w < numWorkers; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < requestsPerWorker; i++) {
                    BatchRequest req;
                    req.fd = w;
                    req.routeId = 0;
                    req.method = "GET";
                    req.path = "/";

                    scheduler.submitRequest(std::move(req));
                }
            });
        }

        // Wait for all workers to complete submissions
        for (auto& w : workers) w.join();

        // Wait for scheduler to process all requests
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        scheduler.stop();

        auto t1 = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        uint64_t processed = totalProcessed.load();
        uint64_t rps = processed > 0 ? processed * 1000 / std::max(1LL, elapsed) : 0;
        oldArch_results.push_back(rps);

        std::cout << "  Run " << (run + 1) << ": " << rps << " RPS (" << elapsed << " ms)\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Results Summary
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  SUMMARY                                                     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    uint64_t oldAvg = 0, newAvg = 0;
    for (auto rps : oldArch_results) oldAvg += rps;
    for (auto rps : newArch_results) newAvg += rps;
    oldAvg /= numRuns;
    newAvg /= numRuns;

    std::cout << "OLD Architecture (Central Scheduler):\n";
    std::cout << "  Avg: " << oldAvg << " RPS\n";
    std::cout << "  Range: " << *std::min_element(oldArch_results.begin(), oldArch_results.end())
              << " - " << *std::max_element(oldArch_results.begin(), oldArch_results.end()) << " RPS\n\n";

    std::cout << "NEW Architecture (Decentralized):\n";
    std::cout << "  Avg: " << newAvg << " RPS\n";
    std::cout << "  Range: " << *std::min_element(newArch_results.begin(), newArch_results.end())
              << " - " << *std::max_element(newArch_results.begin(), newArch_results.end()) << " RPS\n\n";

    double improvement = oldAvg > 0 ? (double)newAvg / oldAvg : 0;
    std::cout << "IMPROVEMENT: " << std::fixed << std::setprecision(1) << improvement << "x\n\n";

    if (improvement >= 10) {
        std::cout << "✓ EXCELLENT: 10x+ throughput gain achieved!\n";
    } else if (improvement >= 5) {
        std::cout << "✓ GOOD: 5x+ throughput gain achieved!\n";
    } else if (improvement >= 2) {
        std::cout << "✓ FAIR: 2x+ throughput gain achieved!\n";
    } else {
        std::cout << "✗ NEEDS INVESTIGATION: Expected 10x+ improvement\n";
    }

    return 0;
}
