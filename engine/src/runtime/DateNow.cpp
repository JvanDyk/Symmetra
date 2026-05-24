#include "DateNow.h"
#include <cuda.h>
#include <chrono>
#include <thread>
#include <cstring>

namespace symm {

using Clock = std::chrono::system_clock;

// Returns Unix timestamp in milliseconds
static uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()
        ).count()
    );
}

DateNow::DateNow() {
    // Allocate an 8-byte VRAM scalar for the timestamp
    cuMemAlloc(&d_timestamp_, sizeof(uint64_t));

    // Seed with current time before the thread starts
    uint64_t t = nowMs();
    cuMemcpyHtoD(d_timestamp_, &t, sizeof(uint64_t));

    // Start the background refresh thread
    worker_ = std::thread([this]() { refreshLoop(); });
}

void DateNow::refreshLoop() {
    while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(UPDATE_MS)
        );
        if (stop_.load(std::memory_order_relaxed)) break;

        uint64_t t = nowMs();
        // cuMemcpyHtoD is safe to call from any CPU thread that shares the
        // CUDA context (the refresh thread is created after cuCtxCreate).
        cuMemcpyHtoD(d_timestamp_, &t, sizeof(uint64_t));
    }
}

void DateNow::shutdown() {
    bool was_running = !stop_.exchange(true);
    if (was_running && worker_.joinable()) {
        worker_.join();
    }
    if (d_timestamp_) {
        cuMemFree(d_timestamp_);
        d_timestamp_ = 0;
    }
}

DateNow& DateNow::instance() {
    // Constructed once; the CUDA context must already be active.
    static DateNow inst;
    return inst;
}

} // namespace symm
