#include "StreamPool.h"
#include <stdexcept>

namespace symm {

StreamPool::StreamPool(int count) {
    if (count <= 0 || count > 128)
        throw std::invalid_argument("StreamPool: count must be 1..128");

    streams_.resize(count);
    for (int i = 0; i < count; ++i) {
        // CU_STREAM_NON_BLOCKING: stream does not synchronise implicitly with
        // the default (NULL) stream, so requests are fully independent.
        CUresult r = cuStreamCreate(&streams_[i], CU_STREAM_NON_BLOCKING);
        if (r != CUDA_SUCCESS)
            throw std::runtime_error("StreamPool: cuStreamCreate failed");
        free_.push({ streams_[i], i });
    }
}

StreamPool::~StreamPool() {
    for (auto& s : streams_) {
        cuStreamSynchronize(s);
        cuStreamDestroy(s);
    }
}

StreamPool::Slot StreamPool::acquire() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [this] { return !free_.empty(); });
    auto e = free_.front();
    free_.pop();
    return { e.stream, e.idx };
}

void StreamPool::release(Slot slot) {
    // Synchronise before returning — ensures all kernels on this stream have
    // completed and VRAM slab can safely be reset.
    cuStreamSynchronize(slot.stream);
    {
        std::lock_guard<std::mutex> lk(mu_);
        free_.push({ slot.stream, slot.idx });
    }
    cv_.notify_one();
}

void StreamPool::syncAll() {
    for (auto& s : streams_) cuStreamSynchronize(s);
}

int StreamPool::available() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int>(free_.size());
}

} // namespace symm
