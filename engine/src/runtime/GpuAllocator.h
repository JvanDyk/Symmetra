#pragma once
#include <cuda.h>
#include <array>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace symm {

// Maximum concurrent requests (one slab per CUDA stream)
static constexpr int    MAX_STREAMS = 64;
// Per-request VRAM slab: bump-pointer allocator, freed atomically at request end
static constexpr size_t SLAB_SIZE   = 4  * 1024 * 1024;  // 4 MB
// Persistent heap: survives across requests (KV store, query caches, compiled kernels)
static constexpr size_t HEAP_SIZE   = 512 * 1024 * 1024; // 512 MB

// GpuAllocator — two-tier VRAM memory manager
//
// Tier 1 — request arena (young generation):
//   Each CUDA stream slot gets a fixed VRAM slab.
//   requestAlloc() is a lock-free bump-pointer increment — O(1), no CAS.
//   requestFree() resets the bump pointer — O(1) GC for an entire request's objects.
//
// Tier 2 — persistent heap (old generation):
//   For objects that outlive a request (cache entries, compiled eval kernels, etc.).
//   First-fit free list with immediate coalescing.
//   heapAlloc/heapFree are mutex-protected (expected rare in hot path).
//
class GpuAllocator {
public:
    static GpuAllocator& instance();

    // ── Request arena ─────────────────────────────────────────────────────────
    // Thread-safe: each streamIdx is owned by exactly one thread at a time.
    CUdeviceptr requestAlloc(int streamIdx, size_t bytes, size_t align = 16);
    void        requestFree (int streamIdx);   // O(1) — resets bump pointer

    size_t      requestUsed (int streamIdx) const;
    size_t      requestPeak (int streamIdx) const;

    // ── Persistent heap ───────────────────────────────────────────────────────
    CUdeviceptr heapAlloc(size_t bytes, size_t align = 16);
    void        heapFree (CUdeviceptr ptr, size_t bytes);

    // ── Stats ─────────────────────────────────────────────────────────────────
    size_t heapUsed()     const;
    size_t heapCapacity() const { return HEAP_SIZE; }
    int    slabsActive()  const;

private:
    GpuAllocator();
    ~GpuAllocator();

    struct Slab {
        CUdeviceptr base     = 0;
        size_t      capacity = 0;
        size_t      cursor   = 0;   // bump pointer (monotone within a request)
        size_t      peak     = 0;   // high-water mark for diagnostics
        bool        inUse    = false;
    };

    struct FreeBlock {
        CUdeviceptr ptr;
        size_t      size;
    };

    // All slabs carved from one large allocation — single cudaMalloc
    CUdeviceptr                    slabBase_ = 0;
    std::array<Slab, MAX_STREAMS>  slabs_{};

    // Persistent heap
    CUdeviceptr            heapBase_ = 0;
    std::vector<FreeBlock> freeList_;
    mutable std::mutex     heapMu_;
};

} // namespace symm
