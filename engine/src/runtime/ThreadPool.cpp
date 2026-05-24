#include "ThreadPool.h"

namespace symm {

ThreadPool::ThreadPool(int threads) {
    int n = threads > 0 ? threads : static_cast<int>(std::thread::hardware_concurrency());
    if (n <= 0) n = 4;

    for (int i = 0; i < n; ++i)
        workers_.emplace_back(&ThreadPool::workerLoop, this);
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) return;
        tasks_.emplace(std::move(task));
        pending_.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
        pending_.fetch_sub(1, std::memory_order_relaxed);
    }
}

} // namespace symm
