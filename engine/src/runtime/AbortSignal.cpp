#include "AbortSignal.h"
#include <cstdio>

namespace symm {

AbortSignal::AbortSignal() {
    CUresult r = cuMemAlloc(&d_flag_, 1);
    if (r != CUDA_SUCCESS) {
        std::fprintf(stderr, "AbortSignal: cuMemAlloc failed\n");
        d_flag_ = 0;
        return;
    }
    cuMemsetD8(d_flag_, 0, 1);   // initialise: not aborted
}

AbortSignal::~AbortSignal() {
    if (d_flag_) cuMemFree(d_flag_);
}

void AbortSignal::addEventListener(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mu_);
    if (aborted_) {
        cb();   // already aborted — fire immediately
        return;
    }
    listeners_.push_back(std::move(cb));
}

void AbortSignal::doAbort() {
    bool expected = false;
    if (!aborted_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
        return;   // already aborted
    }

    // Set the VRAM flag — GPU kernels polling *d_flag_ will see 1 on the next check
    if (d_flag_) cuMemsetD8(d_flag_, 1, 1);

    // Fire event listeners
    std::vector<std::function<void()>> cbs;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cbs = std::move(listeners_);
    }
    for (auto& cb : cbs) {
        try { cb(); } catch (...) {}
    }
}

} // namespace symm
