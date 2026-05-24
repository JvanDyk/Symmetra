#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>
#include <atomic>
#include <stdexcept>

namespace symm {

// ThreadPool — fixed-size CPU thread pool for I/O-bound work.
//
// TCP accept() / recv() / send() are CPU operations that cannot run on GPU.
// The thread pool runs N workers that each independently accept connections,
// copy data to pinned memory, and feed a GPU stream from StreamPool.
//
// With N CPU threads and M GPU streams (N <= M), all M streams can be kept
// busy as long as the CPU can produce requests faster than the GPU consumes
// them — which is always true for GPU-accelerated handlers.
//
// threads=0 uses std::thread::hardware_concurrency() (one per CPU core).
//
class ThreadPool {
public:
    explicit ThreadPool(int threads = 0);
    ~ThreadPool();

    // Submit a callable. Returns a future for the result.
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Submit a fire-and-forget task (no future).
    void post(std::function<void()> task);

    int  size()       const { return static_cast<int>(workers_.size()); }
    int  queueDepth() const { return pending_.load(std::memory_order_relaxed); }

private:
    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    std::mutex                         mu_;
    std::condition_variable            cv_;
    bool                               stop_ = false;
    std::atomic<int>                   pending_{ 0 };

    void workerLoop();
};

// ── Template implementation ───────────────────────────────────────────────────

template<typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<R()>>(
        [f   = std::forward<F>(f),
         args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            return std::apply(std::move(f), std::move(args));
        }
    );

    auto fut = task->get_future();
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stop_) throw std::runtime_error("ThreadPool: submit after shutdown");
        tasks_.emplace([task]() mutable { (*task)(); });
        pending_.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
    return fut;
}

} // namespace symm
