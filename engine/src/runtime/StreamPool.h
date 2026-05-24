#pragma once
#include <cuda.h>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace symm {

// StreamPool — manages a fixed pool of CUstream objects for concurrent request dispatch.
//
// Each CUstream maps to an independent GPU work queue. Requests dispatched on
// different streams execute concurrently — the GPU scheduler interleaves their
// kernels across SMs while each stream's kernels remain ordered within it.
//
// acquire() blocks if no streams are available (all slots busy).
// release() synchronises the stream, then returns it to the pool and wakes
// one waiting thread.
//
// The streamIdx assigned per request is stable for its lifetime and is the
// index used by GpuAllocator to select the correct VRAM slab.
//
class StreamPool {
public:
    explicit StreamPool(int count = 32);
    ~StreamPool();

    // Acquire a stream + its slot index. Blocks if the pool is empty.
    struct Slot { CUstream stream; int idx; };
    Slot acquire();

    // Return slot to pool. Synchronises the stream first (safe to call from any thread).
    void release(Slot slot);

    // Wait for every stream to drain.
    void syncAll();

    int  size()    const { return static_cast<int>(streams_.size()); }
    int  available() const;

private:
    struct Entry { CUstream stream; int idx; };

    std::vector<CUstream>  streams_;   // index == slot index
    std::queue<Entry>      free_;
    mutable std::mutex     mu_;
    std::condition_variable cv_;
};

} // namespace symm
