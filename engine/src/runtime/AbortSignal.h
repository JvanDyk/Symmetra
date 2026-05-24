#pragma once
#include <cuda.h>
#include <atomic>
#include <functional>
#include <vector>
#include <mutex>

namespace symm {

// AbortSignal — a GPU-visible cancellation flag.
//
// The flag lives in VRAM as a single uint8_t (d_flag_).
// GPU kernels check it via: if (*abort_flag) return;
// CPU calls abort() which sets it via cuMemsetD8 — instant, no kernel launch.
//
// This maps directly to the Web AbortController / AbortSignal API.
//
class AbortSignal {
public:
    AbortSignal();
    ~AbortSignal();

    // True if abort() has been called.
    bool aborted() const { return aborted_.load(std::memory_order_acquire); }

    // VRAM pointer to the uint8_t flag — pass to GPU kernels that should honour cancellation.
    CUdeviceptr devicePtr() const { return d_flag_; }

    // Register a callback to run when abort() is called (runs on the calling thread).
    void addEventListener(std::function<void()> cb);

private:
    friend class AbortController;
    void doAbort();

    CUdeviceptr         d_flag_  = 0;
    std::atomic<bool>   aborted_{ false };
    std::vector<std::function<void()>> listeners_;
    std::mutex          mu_;
};

// AbortController — creates and controls an AbortSignal.
//
// Usage:
//   AbortController ctrl;
//   auto fut = http.get(url, ctrl.signal().devicePtr());
//   ctrl.abort();   // immediately sets the GPU flag; running kernel exits early
//
class AbortController {
public:
    AbortController() = default;

    void abort() { signal_.doAbort(); }

    AbortSignal&       signal()       { return signal_; }
    const AbortSignal& signal() const { return signal_; }

private:
    AbortSignal signal_;
};

} // namespace symm
