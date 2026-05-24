#include "GpuAllocator.h"
#include <cuda_runtime.h>
#include <stdexcept>
#include <algorithm>
#include <string>

namespace symm {

GpuAllocator& GpuAllocator::instance() {
    static GpuAllocator inst;
    return inst;
}

GpuAllocator::GpuAllocator() {
    // ── Slab backing store — one large allocation for all request arenas ──────
    // Single cudaMalloc is ~10x faster than MAX_STREAMS individual allocations.
    CUresult r = cuMemAlloc(&slabBase_, static_cast<size_t>(MAX_STREAMS) * SLAB_SIZE);
    if (r != CUDA_SUCCESS)
        throw std::runtime_error("GpuAllocator: slab backing store alloc failed");

    for (int i = 0; i < MAX_STREAMS; ++i) {
        slabs_[i].base     = slabBase_ + static_cast<size_t>(i) * SLAB_SIZE;
        slabs_[i].capacity = SLAB_SIZE;
        slabs_[i].cursor   = 0;
        slabs_[i].peak     = 0;
        slabs_[i].inUse    = false;
    }

    // ── Persistent heap ───────────────────────────────────────────────────────
    r = cuMemAlloc(&heapBase_, HEAP_SIZE);
    if (r != CUDA_SUCCESS) {
        cuMemFree(slabBase_);
        throw std::runtime_error("GpuAllocator: persistent heap alloc failed");
    }
    freeList_.push_back({ heapBase_, HEAP_SIZE });
}

GpuAllocator::~GpuAllocator() {
    if (slabBase_) cuMemFree(slabBase_);
    if (heapBase_) cuMemFree(heapBase_);
}

// ── Request arena ─────────────────────────────────────────────────────────────

CUdeviceptr GpuAllocator::requestAlloc(int idx, size_t bytes, size_t align) {
    if (idx < 0 || idx >= MAX_STREAMS)
        throw std::out_of_range("GpuAllocator::requestAlloc: bad stream index");

    Slab& s = slabs_[idx];
    s.inUse = true;

    // Align the bump pointer (power-of-two align only)
    size_t aligned = (s.cursor + align - 1) & ~(align - 1);
    if (aligned + bytes > s.capacity)
        throw std::runtime_error("GpuAllocator: request slab exhausted ("
            + std::to_string(SLAB_SIZE / (1024*1024)) + " MB)");

    s.cursor = aligned + bytes;
    if (s.cursor > s.peak) s.peak = s.cursor;
    return s.base + aligned;
}

void GpuAllocator::requestFree(int idx) {
    if (idx < 0 || idx >= MAX_STREAMS) return;
    // O(1) GC: resetting the bump pointer logically frees every allocation
    // made during the request in a single instruction.
    slabs_[idx].cursor = 0;
    slabs_[idx].inUse  = false;
}

size_t GpuAllocator::requestUsed(int idx) const {
    if (idx < 0 || idx >= MAX_STREAMS) return 0;
    return slabs_[idx].cursor;
}

size_t GpuAllocator::requestPeak(int idx) const {
    if (idx < 0 || idx >= MAX_STREAMS) return 0;
    return slabs_[idx].peak;
}

// ── Persistent heap ───────────────────────────────────────────────────────────

CUdeviceptr GpuAllocator::heapAlloc(size_t bytes, size_t align) {
    std::lock_guard<std::mutex> lk(heapMu_);

    for (auto it = freeList_.begin(); it != freeList_.end(); ++it) {
        // Align the start of this block
        size_t off     = (align == 0) ? 0 : ((align - (it->ptr % align)) % align);
        CUdeviceptr ap = it->ptr + off;

        if (off + bytes > it->size) continue;   // block too small

        // Carve out aligned allocation
        if (off > 0) {
            // insert() before 'it' can reallocate the vector, invalidating 'it'.
            // Capture the index first, then re-derive the iterator after insert.
            auto insertedIt = freeList_.insert(it, { it->ptr, off });
            it = std::next(insertedIt);   // original block, now shifted right by 1
            it->ptr  += off;
            it->size -= off;
        }
        if (it->size > bytes) {
            it->ptr  += bytes;
            it->size -= bytes;
        } else {
            freeList_.erase(it);
        }
        return ap;
    }
    throw std::runtime_error("GpuAllocator: persistent heap exhausted");
}

void GpuAllocator::heapFree(CUdeviceptr ptr, size_t bytes) {
    std::lock_guard<std::mutex> lk(heapMu_);

    // Insert block in address order
    FreeBlock blk{ ptr, bytes };
    auto it = std::lower_bound(freeList_.begin(), freeList_.end(), blk,
        [](const FreeBlock& a, const FreeBlock& b) { return a.ptr < b.ptr; });
    it = freeList_.insert(it, blk);

    // Coalesce with successor
    auto next = std::next(it);
    if (next != freeList_.end() && it->ptr + it->size == next->ptr) {
        it->size += next->size;
        freeList_.erase(next);
    }
    // Coalesce with predecessor
    if (it != freeList_.begin()) {
        auto prev = std::prev(it);
        if (prev->ptr + prev->size == it->ptr) {
            prev->size += it->size;
            freeList_.erase(it);
        }
    }
}

// ── Stats ─────────────────────────────────────────────────────────────────────

size_t GpuAllocator::heapUsed() const {
    std::lock_guard<std::mutex> lk(heapMu_);
    size_t free = 0;
    for (auto& b : freeList_) free += b.size;
    return HEAP_SIZE - free;
}

int GpuAllocator::slabsActive() const {
    int n = 0;
    for (auto& s : slabs_) if (s.inUse) ++n;
    return n;
}

} // namespace symm
