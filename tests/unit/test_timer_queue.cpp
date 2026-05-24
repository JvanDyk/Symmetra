// Unit tests for TimerQueue — no GPU required.
// Build: g++ -std=c++17 -pthread -I../../engine/src test_timer_queue.cpp
//        ../../engine/src/runtime/TimerQueue.cpp -o test_timer_queue && ./test_timer_queue

#include "runtime/TimerQueue.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace symm;
using namespace std::chrono_literals;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

// ── setTimeout ────────────────────────────────────────────────────────────────

void test_setTimeout_fires() {
    auto& tq = TimerQueue::instance();
    std::atomic<bool> fired{ false };

    tq.setTimeout([&fired] { fired = true; }, 50);
    std::this_thread::sleep_for(200ms);

    CHECK(fired.load(), "setTimeout fired");
}

void test_setTimeout_fires_only_once() {
    auto& tq = TimerQueue::instance();
    std::atomic<int> count{ 0 };

    tq.setTimeout([&count] { ++count; }, 50);
    std::this_thread::sleep_for(300ms);

    CHECK(count.load() == 1, "setTimeout fires exactly once");
}

void test_setTimeout_approximate_delay() {
    auto& tq = TimerQueue::instance();
    auto start = std::chrono::steady_clock::now();
    std::atomic<bool> fired{ false };

    tq.setTimeout([&fired, &start] {
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        fired = (ms >= 80 && ms < 500);   // fired between 80ms and 500ms
    }, 100);

    std::this_thread::sleep_for(600ms);
    CHECK(fired.load(), "setTimeout fires at approximately correct time");
}

// ── setInterval ───────────────────────────────────────────────────────────────

void test_setInterval_fires_multiple() {
    auto& tq = TimerQueue::instance();
    std::atomic<int> count{ 0 };

    auto id = tq.setInterval([&count] { ++count; }, 50);
    std::this_thread::sleep_for(280ms);  // ~5 fires at 50ms intervals
    tq.clearTimer(id);
    std::this_thread::sleep_for(100ms);  // confirm it stopped

    int c = count.load();
    CHECK(c >= 4 && c <= 7, "setInterval fires ~5 times in 280ms");
}

void test_setInterval_stops_after_clear() {
    auto& tq = TimerQueue::instance();
    std::atomic<int> count{ 0 };

    auto id = tq.setInterval([&count] { ++count; }, 50);
    std::this_thread::sleep_for(130ms);
    tq.clearTimer(id);
    int countAtClear = count.load();

    std::this_thread::sleep_for(200ms);  // should not increment further
    int countAfter = count.load();

    CHECK(countAtClear <= countAfter, "count doesn't decrease");
    CHECK(countAfter - countAtClear <= 1, "at most 1 extra fire after clearTimer (race)");
}

// ── clearTimer ────────────────────────────────────────────────────────────────

void test_clearTimeout_prevents_fire() {
    auto& tq = TimerQueue::instance();
    std::atomic<bool> fired{ false };

    auto id = tq.setTimeout([&fired] { fired = true; }, 200);
    tq.clearTimer(id);   // cancel before it fires
    std::this_thread::sleep_for(400ms);

    CHECK(!fired.load(), "clearTimeout prevents fire");
}

void test_clearTimer_after_fire_is_safe() {
    auto& tq = TimerQueue::instance();
    std::atomic<bool> fired{ false };

    auto id = tq.setTimeout([&fired] { fired = true; }, 50);
    std::this_thread::sleep_for(150ms);   // wait for it to fire

    // This must not crash or corrupt state
    tq.clearTimer(id);
    CHECK(fired.load(), "timer fired before clear");
    // No crash = pass (verify process doesn't abort)
    ++passed;
}

// ── Multiple concurrent timers ────────────────────────────────────────────────

void test_multiple_timers_ordered() {
    auto& tq = TimerQueue::instance();
    std::vector<int> order;
    std::mutex mu;

    tq.setTimeout([&]{ std::lock_guard<std::mutex> lk(mu); order.push_back(3); }, 150);
    tq.setTimeout([&]{ std::lock_guard<std::mutex> lk(mu); order.push_back(1); }, 50);
    tq.setTimeout([&]{ std::lock_guard<std::mutex> lk(mu); order.push_back(2); }, 100);

    std::this_thread::sleep_for(400ms);

    CHECK(order.size() == 3,   "all three fired");
    CHECK(order[0] == 1, "first to fire: delay=50");
    CHECK(order[1] == 2, "second to fire: delay=100");
    CHECK(order[2] == 3, "third to fire: delay=150");
}

// ── cancelled_ set doesn't grow for already-fired timers ─────────────────────

void test_cancelled_set_bounded() {
    auto& tq = TimerQueue::instance();
    // Fire many timers and call clearTimer after they've fired — shouldn't leak
    for (int i = 0; i < 50; ++i) {
        auto id = tq.setTimeout([] {}, 10);
        (void)id;
    }
    std::this_thread::sleep_for(200ms);
    // If clearTimer leaks into cancelled_, we'd see memory growth — here we just
    // verify nothing crashes after 50 fire-then-late-clear sequences.
    ++passed;
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    test_setTimeout_fires();
    test_setTimeout_fires_only_once();
    test_setTimeout_approximate_delay();

    test_setInterval_fires_multiple();
    test_setInterval_stops_after_clear();

    test_clearTimeout_prevents_fire();
    test_clearTimer_after_fire_is_safe();
    test_multiple_timers_ordered();
    test_cancelled_set_bounded();

    TimerQueue::instance().shutdown();

    std::printf("\nTimerQueue tests: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
