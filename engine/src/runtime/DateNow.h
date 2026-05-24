#pragma once
#include <cuda.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>

namespace symm {

// DateNow — GPU-visible real-time millisecond clock.
//
// A background CPU thread refreshes a pinned VRAM uint64_t every UPDATE_MS
// milliseconds using cuMemcpyHtoD. GPU kernels read the device scalar via
// symm_date_now(), which is injected into the NVRTC prelude.
//
// Design choices:
//   • UPDATE_MS = 10 ms → 10 ms accuracy, negligible CPU load (<0.01%)
//   • Pinned host memory: zero-copy path on integrated GPUs; explicit D2H copy
//     on discrete GPUs (one 8-byte DMA per tick).
//   • The GPU scalar is __device__ __constant__ so all warps see a cached copy.
//   • Startup: the scalar is initialised on the first call to instance(), so
//     time is available before the first HTTP request.
//
class DateNow {
public:
    static constexpr uint32_t UPDATE_MS = 10;  // refresh interval

    // Singleton — initialised on first call.
    static DateNow& instance();

    // VRAM pointer to the uint64_t millisecond timestamp.
    // Pass this to NVRTC so it can bind to the __device__ __constant__.
    CUdeviceptr devicePtr() const { return d_timestamp_; }

    // Stop the background refresh thread (called from GpuServer destructor).
    void shutdown();

    DateNow(const DateNow&) = delete;
    DateNow& operator=(const DateNow&) = delete;

private:
    DateNow();
    ~DateNow() { shutdown(); }

    void refreshLoop();

    CUdeviceptr          d_timestamp_ = 0;   // VRAM uint64_t
    std::atomic<bool>    stop_{ false };
    std::thread          worker_;
};

} // namespace symm
