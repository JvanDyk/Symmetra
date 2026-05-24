#pragma once
#include <functional>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace symm {

// TimerQueue — background CPU timer system for setTimeout / setInterval.
//
// A single background thread sleeps until the next deadline, fires the
// callback, then re-enqueues interval timers. One-shot timers (setTimeout)
// are discarded after firing.
//
// Callbacks run on the timer thread — they should be short or post work
// to a ThreadPool rather than doing heavy computation inline.
//
class TimerQueue {
public:
    using TimerId  = uint64_t;
    using Callback = std::function<void()>;

    static TimerQueue& instance();

    // Fire cb once after delayMs milliseconds.
    TimerId setTimeout(Callback cb, uint32_t delayMs);

    // Fire cb every intervalMs milliseconds until cleared.
    TimerId setInterval(Callback cb, uint32_t intervalMs);

    // Cancel a timer. Safe to call after it has already fired (no-op).
    void clearTimer(TimerId id);

    // Drain all pending timers and stop the background thread.
    void shutdown();

private:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Entry {
        TimerId   id;
        TimePoint deadline;
        uint32_t  intervalMs;  // 0 = one-shot
        Callback  cb;

        bool operator>(const Entry& o) const { return deadline > o.deadline; }
    };

    TimerQueue();
    ~TimerQueue() { shutdown(); }

    void run();

    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap_;
    std::unordered_set<TimerId> cancelled_;
    std::unordered_set<TimerId> live_;      // IDs currently in the heap
    std::mutex              mu_;
    std::condition_variable cv_;
    std::thread             worker_;
    std::atomic<bool>       stop_{ false };
    std::atomic<TimerId>    nextId_{ 1 };
};

} // namespace symm
