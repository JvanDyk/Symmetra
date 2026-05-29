// test_submission_overhead.cpp — Analyze submission path bottlenecks
//
// Test 1: Pure memory copy (baseline)
// Test 2: Lock-free queue enqueue
// Test 3: Condition variable notify
// Test 4: Full BatchScheduler flow

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <iomanip>
#include "LockFreeQueue.h"

using namespace symm;

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n"
              << "║  SUBMISSION PATH OVERHEAD ANALYSIS                         ║\n"
              << "║  Isolates bottleneck in request submission pipeline        ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    struct SimpleRequest {
        int fd;
        int routeId;
        char path[64];
        char body[256];
    };

    // Test 1: Memory copy baseline
    {
        std::cout << "TEST 1: Pure memory copy (Baseline):\n";

        std::atomic<int> count{0};
        SimpleRequest req;
        req.fd = 1;
        req.routeId = 0;

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        for (int w = 0; w < 64; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 800; i++) {
                    SimpleRequest local = req;
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0
        ).count();

        std::cout << "  Time: " << elapsed << " ms\n";
        std::cout << "  RPS: " << (count.load() * 1000 / elapsed) << "\n\n";
    }

    // Test 2: Simple atomic increment (to compare)
    {
        std::cout << "TEST 2: Simple atomic operations:\n";

        std::atomic<int> count{0};

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        for (int w = 0; w < 64; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 800; i++) {
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0
        ).count();

        std::cout << "  Time: " << elapsed << " ms\n";
        std::cout << "  RPS: " << (count.load() * 1000 / elapsed) << "\n\n";
    }

    // Test 3: Condition variable notify
    {
        std::cout << "TEST 3: With condition_variable notify:\n";

        std::atomic<int> count{0};
        std::condition_variable cv;
        std::mutex mtx;

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        for (int w = 0; w < 64; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 800; i++) {
                    // Notify without taking lock (thread-safe)
                    cv.notify_one();
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0
        ).count();

        std::cout << "  Time: " << elapsed << " ms\n";
        std::cout << "  RPS: " << (count.load() * 1000 / elapsed) << "\n\n";
    }

    // Test 4: Multiple condition variables (one per worker)
    {
        std::cout << "TEST 4: With per-worker condition_variable:\n";

        std::atomic<int> count{0};
        std::vector<std::condition_variable> cvs(64);

        auto t0 = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> workers;
        for (int w = 0; w < 64; w++) {
            workers.emplace_back([&, w]() {
                for (int i = 0; i < 800; i++) {
                    cvs[w].notify_one();
                    count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& w : workers) w.join();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0
        ).count();

        std::cout << "  Time: " << elapsed << " ms\n";
        std::cout << "  RPS: " << (count.load() * 1000 / elapsed) << "\n\n";
    }

    return 0;
}
