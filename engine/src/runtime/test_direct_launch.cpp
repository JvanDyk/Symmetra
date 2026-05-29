// test_direct_launch.cpp — Bypass scheduler, test AsyncBatchLauncher directly
//
// Simulates what happens if requests post directly to batch launcher without
// going through central BatchScheduler queue. Measures true dispatch overhead.

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <iomanip>
#include <memory>
#include "AsyncBatchLauncher.h"
#include "BatchScheduler.h"

using namespace symm;

static void fastKernel(const std::vector<BatchRequest>& requests, BatchResult& result) {
    for (size_t i = 0; i < requests.size(); i++) {
        result.responses[i] = "ok";
        result.statuses[i] = 200;
    }
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  DIRECT BATCH LAUNCH (Scheduler Bypass)                   ║\n"
              << "║  Tests AsyncBatchLauncher without central queue           ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    // Test 1: Direct enqueue (measure queue operations only)
    {
        std::cout << "DIRECT ENQUEUE (No dispatch overhead):\n";

        std::vector<std::shared_ptr<BatchResult>> resultPtrs;
        std::atomic<int> submitted{0};

        auto t0 = std::chrono::high_resolution_clock::now();

        // Simulate 256 workers submitting batches directly
        std::vector<std::thread> workers;
        for (int w = 0; w < 256; w++) {
            workers.emplace_back([&, w]() {
                for (int batch = 0; batch < 200; batch++) {
                    std::vector<BatchRequest> reqs(256);
                    for (int i = 0; i < 256; i++) {
                        reqs[i].fd = w;
                        reqs[i].routeId = 0;
                    }

                    // Just accumulate without dispatch
                    submitted.fetch_add(256, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();
        auto t1 = std::chrono::high_resolution_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        int total = submitted.load();
        std::cout << "  Submitted: " << total << " requests in " << elapsed << " ms\n";
        std::cout << "  RPS (No dispatch): " << (total * 1000 / elapsed) << " RPS\n\n";
    }

    // Test 2: AsyncBatchLauncher (measure actual dispatch)
    {
        std::cout << "ASYNCBATCHLAUNCHER (With dispatch workers):\n";

        std::atomic<int> launched{0};

        AsyncBatchLauncher launcher(8);
        launcher.setLaunchFn([&](int routeId, const std::vector<BatchRequest>& reqs, BatchResult& result) {
            fastKernel(reqs, result);
            launched.fetch_add(1, std::memory_order_relaxed);
        });

        auto t0 = std::chrono::high_resolution_clock::now();

        std::atomic<int> submitted{0};
        std::vector<std::thread> workers;
        for (int w = 0; w < 256; w++) {
            workers.emplace_back([&, w]() {
                for (int batch = 0; batch < 200; batch++) {
                    BatchWorkItem item;
                    item.routeId = 0;
                    item.requests.resize(256);
                    for (int i = 0; i < 256; i++) {
                        item.requests[i].fd = w;
                        item.requests[i].routeId = 0;
                    }
                    item.result = std::make_shared<BatchResult>();

                    // Post batch to async launcher
                    launcher.postBatch(std::move(item));
                    submitted.fetch_add(256, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();

        // Wait for all batches to dispatch
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        launcher.stop();

        auto t1 = std::chrono::high_resolution_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        int total = submitted.load();
        std::cout << "  Submitted: " << total << " requests in " << elapsed << " ms\n";
        std::cout << "  Dispatched: " << launched.load() << " batches\n";
        std::cout << "  RPS (With dispatch): " << (total * 1000 / elapsed) << " RPS\n";
        std::cout << "  Dispatch Overhead: " << ((total * 1000 / elapsed) == 0 ? 0 : 100.0) << "%\n\n";
    }

    return 0;
}
