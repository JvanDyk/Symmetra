// Unit tests for ThreadPool — no GPU required.
// Build: g++ -std=c++17 -pthread -I../../engine/src test_thread_pool.cpp
//        ../../engine/src/runtime/ThreadPool.cpp -o test_thread_pool && ./test_thread_pool

#include "runtime/ThreadPool.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

using namespace symm;
using namespace std::chrono_literals;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

void test_basic_post() {
    ThreadPool pool(2);
    std::atomic<int> count{ 0 };

    for (int i = 0; i < 10; ++i)
        pool.post([&count] { ++count; });

    std::this_thread::sleep_for(200ms);
    CHECK(count.load() == 10, "all 10 tasks executed");
}

void test_submit_returns_future() {
    ThreadPool pool(2);

    auto fut = pool.submit([] { return 42; });
    CHECK(fut.get() == 42, "future returns correct value");
}

void test_submit_concurrent() {
    ThreadPool pool(4);
    std::vector<std::future<int>> futs;

    for (int i = 0; i < 20; ++i)
        futs.push_back(pool.submit([i] { return i * i; }));

    int sum = 0;
    for (auto& f : futs) sum += f.get();

    // sum of i^2 for i in [0,19] = 2470
    int expected = 0;
    for (int i = 0; i < 20; ++i) expected += i * i;
    CHECK(sum == expected, "concurrent futures sum correctly");
}

void test_pool_size() {
    ThreadPool pool(6);
    CHECK(pool.size() == 6, "pool has correct size");
}

void test_default_size() {
    ThreadPool pool(0);
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    if (hw <= 0) hw = 4;
    CHECK(pool.size() == hw, "default size matches hardware_concurrency");
}

void test_queue_depth() {
    // Block all threads, then check queue depth
    ThreadPool pool(1);
    std::atomic<bool> release{ false };

    // Post a blocking task to occupy the one thread
    pool.post([&release] { while (!release.load()) std::this_thread::sleep_for(5ms); });
    std::this_thread::sleep_for(20ms);

    // Post 5 more tasks — they queue up
    for (int i = 0; i < 5; ++i)
        pool.post([] {});

    std::this_thread::sleep_for(20ms);
    CHECK(pool.queueDepth() >= 5, "queued tasks counted");

    release = true;
    std::this_thread::sleep_for(200ms);
    CHECK(pool.queueDepth() == 0, "queue drained after release");
}

void test_exception_in_task_doesnt_crash() {
    ThreadPool pool(2);
    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("test error");
        return 0;
    });
    try {
        fut.get();
        CHECK(false, "expected exception");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()) == "test error", "exception propagated correctly");
    }
}

void test_destructor_drains_queue() {
    std::atomic<int> count{ 0 };
    {
        ThreadPool pool(2);
        for (int i = 0; i < 20; ++i)
            pool.post([&count] { count.fetch_add(1); });
    }
    // Destructor should have joined threads after draining
    CHECK(count.load() == 20, "destructor waited for all tasks");
}

int main() {
    test_basic_post();
    test_submit_returns_future();
    test_submit_concurrent();
    test_pool_size();
    test_default_size();
    test_queue_depth();
    test_exception_in_task_doesnt_crash();
    test_destructor_drains_queue();

    std::printf("\nThreadPool tests: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
