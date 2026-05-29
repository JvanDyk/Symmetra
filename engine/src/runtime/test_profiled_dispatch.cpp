// test_profiled_dispatch.cpp — Profile BatchScheduler + kernel dispatch overhead
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <cmath>
#include "BatchScheduler.h"

using namespace symm;

static void fastKernel(const std::vector<BatchRequest>& requests, BatchResult& result) {
    // Minimal kernel — just return
    for (size_t i = 0; i < requests.size(); i++) {
        result.responses[i] = "ok";
        result.statuses[i] = 200;
    }
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  BATCH DISPATCH OVERHEAD ANALYSIS                         ║\n"
              << "║  Measures: scheduler loop, queue ops, kernel dispatch     ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    BatchPolicy policy;
    policy.preferredBatchSize = 256;
    policy.minBatchSize = 32;
    policy.fillTimeoutUs = 1000;
    policy.maxQueueDepth = 4096;
    policy.maxRoutes = 256;

    std::atomic<uint64_t> dispatchCount{0};
    std::atomic<uint64_t> queueWaits{0};
    std::atomic<uint64_t> totalDispatchUs{0};

    auto t0_global = std::chrono::high_resolution_clock::now();

    BatchScheduler scheduler([&](int routeId, const std::vector<BatchRequest>& requests, BatchResult& result) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fastKernel(requests, result);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t0
        ).count();
        totalDispatchUs.fetch_add(elapsed, std::memory_order_relaxed);
        dispatchCount.fetch_add(1, std::memory_order_relaxed);
    }, policy);

    scheduler.start();

    // Submit 50K requests with 256 workers, measure how long it takes
    std::atomic<bool> done{false};
    std::atomic<uint64_t> submitted{0};

    auto submitStart = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> workers;
    for (int w = 0; w < 256; w++) {
        workers.emplace_back([&, w]() {
            int count = 0;
            while (!done.load() && count < 200) {
                BatchRequest req;
                req.fd = w;
                req.routeId = 0;
                req.method = "GET";
                req.path = "/";
                scheduler.submitRequest(std::move(req));
                count++;
                submitted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Wait for all submissions to complete
    for (auto& w : workers) w.join();
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Wait for all batches to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    done.store(true);
    scheduler.stop();

    auto t1_global = std::chrono::high_resolution_clock::now();

    uint64_t submitMs = std::chrono::duration_cast<std::chrono::milliseconds>(submitEnd - submitStart).count();
    uint64_t totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1_global - t0_global).count();
    uint64_t totalSubmitted = submitted.load();
    uint64_t totalBatches = dispatchCount.load();
    uint64_t totalDispatchTime = totalDispatchUs.load();

    std::cout << "Results:\n";
    std::cout << "  Total Submitted: " << totalSubmitted << " requests\n";
    std::cout << "  Total Batches: " << totalBatches << "\n";
    std::cout << "  Avg Batch Size: " << (totalSubmitted / std::max(1ULL, totalBatches)) << "\n";
    std::cout << "  Submission Time: " << submitMs << " ms\n";
    std::cout << "  Total Time: " << totalMs << " ms\n";
    std::cout << "  Submission Rate: " << (totalSubmitted * 1000 / submitMs) << " RPS\n";
    std::cout << "\n  Kernel Dispatch:\n";
    std::cout << "    Total Dispatch Time: " << totalDispatchTime << " µs\n";
    std::cout << "    Time Per Batch: " << (totalDispatchTime / std::max(1ULL, totalBatches)) << " µs\n";
    std::cout << "    Time Per Request: " << (totalDispatchTime / std::max(1ULL, totalSubmitted)) << " µs\n";
    std::cout << "\n  Overhead Analysis:\n";
    std::cout << "    Kernel % of Time: " << (100.0 * totalDispatchTime / (totalMs * 1000)) << "%\n";
    std::cout << "    Scheduler % of Time: " << (100.0 * (totalMs * 1000 - totalDispatchTime) / (totalMs * 1000)) << "%\n";

    return 0;
}
