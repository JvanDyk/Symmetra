#include "TimerQueue.h"

namespace symm {

TimerQueue& TimerQueue::instance() {
    static TimerQueue inst;
    return inst;
}

TimerQueue::TimerQueue() {
    worker_ = std::thread(&TimerQueue::run, this);
}

TimerQueue::TimerId TimerQueue::setTimeout(Callback cb, uint32_t delayMs) {
    TimerId id = nextId_.fetch_add(1, std::memory_order_relaxed);
    Entry e;
    e.id         = id;
    e.deadline   = Clock::now() + std::chrono::milliseconds(delayMs);
    e.intervalMs = 0;
    e.cb         = std::move(cb);
    {
        std::lock_guard<std::mutex> lk(mu_);
        live_.insert(id);
        heap_.push(std::move(e));
    }
    cv_.notify_one();
    return id;
}

TimerQueue::TimerId TimerQueue::setInterval(Callback cb, uint32_t intervalMs) {
    if (intervalMs == 0) intervalMs = 1;
    TimerId id = nextId_.fetch_add(1, std::memory_order_relaxed);
    Entry e;
    e.id         = id;
    e.deadline   = Clock::now() + std::chrono::milliseconds(intervalMs);
    e.intervalMs = intervalMs;
    e.cb         = std::move(cb);
    {
        std::lock_guard<std::mutex> lk(mu_);
        live_.insert(id);
        heap_.push(std::move(e));
    }
    cv_.notify_one();
    return id;
}

void TimerQueue::clearTimer(TimerId id) {
    std::lock_guard<std::mutex> lk(mu_);
    // Only track the cancellation if the timer is still alive — prevents
    // cancelled_ from accumulating entries for already-fired timers.
    if (live_.count(id)) cancelled_.insert(id);
}

void TimerQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void TimerQueue::run() {
    for (;;) {
        std::unique_lock<std::mutex> lk(mu_);

        if (stop_ && heap_.empty()) return;

        if (heap_.empty()) {
            cv_.wait(lk, [this] { return stop_ || !heap_.empty(); });
            if (stop_ && heap_.empty()) return;
            continue;
        }

        auto now      = Clock::now();
        auto deadline = heap_.top().deadline;

        if (deadline > now) {
            cv_.wait_until(lk, deadline, [this, &deadline] {
                return stop_ || (!heap_.empty() && heap_.top().deadline <= deadline);
            });
            continue;
        }

        // Pop the next entry
        Entry e = heap_.top();
        heap_.pop();
        live_.erase(e.id);

        // Check cancellation
        if (cancelled_.count(e.id)) {
            cancelled_.erase(e.id);
            continue;
        }

        // Re-enqueue interval timers before unlocking so they are visible
        if (e.intervalMs > 0) {
            Entry next  = e;
            next.deadline = Clock::now() + std::chrono::milliseconds(e.intervalMs);
            live_.insert(next.id);
            heap_.push(std::move(next));
        }

        lk.unlock();

        // Fire callback outside the lock
        try { e.cb(); } catch (...) {}
    }
}

} // namespace symm
