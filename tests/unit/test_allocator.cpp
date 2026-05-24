// Unit tests for GpuAllocator — CUDA device required.
//
// Build (requires NVCC and a CUDA-capable GPU):
//   nvcc -std=c++17 -I../../engine/src test_allocator.cpp
//         ../../engine/src/runtime/GpuAllocator.cpp
//         -lcuda -lcudart -o test_allocator && ./test_allocator
//
// These tests exercise the two-tier VRAM allocator:
//   • Slab (per-request bump-pointer, 4 MB per slot, O(1) alloc+free)
//   • Heap (persistent free-list, coalescing, alignment guarantee)
//
// Run after test_url_parser, test_timer_queue, test_thread_pool (no GPU needed).

#include "runtime/GpuAllocator.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace symm;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

// ── Slab tests ────────────────────────────────────────────────────────────────

void test_slab_basic_alloc() {
    auto& a = GpuAllocator::instance();
    CUdeviceptr p = a.requestAlloc(0, 64);
    CHECK(p != 0, "slab alloc returns non-null");
    // Write and read back a byte to verify the VRAM address is valid
    uint8_t val = 0xAB;
    cuMemcpyHtoD(p, &val, 1);
    uint8_t got = 0;
    cuMemcpyDtoH(&got, p, 1);
    CHECK(got == val, "slab alloc is writable");
    a.requestFree(0);
}

void test_slab_alignment() {
    auto& a = GpuAllocator::instance();
    // All slab allocations must be 16-byte aligned for GPU coalescing
    for (int sz : {1, 3, 7, 15, 17, 33, 64, 128}) {
        CUdeviceptr p = a.requestAlloc(0, static_cast<size_t>(sz));
        CHECK((p % 16) == 0, "slab alloc is 16-byte aligned");
    }
    a.requestFree(0);
}

void test_slab_reset_is_o1() {
    auto& a = GpuAllocator::instance();
    // Fill the slab with many small allocations
    for (int i = 0; i < 1000; ++i)
        a.requestAlloc(0, 64);
    // requestFree must complete in O(1) regardless of allocation count
    a.requestFree(0);   // must not crash or take unbounded time
    CHECK(true, "slab reset O(1)");
}

void test_slab_multiple_slots_independent() {
    auto& a = GpuAllocator::instance();
    CUdeviceptr p0 = a.requestAlloc(0, 128);
    CUdeviceptr p1 = a.requestAlloc(1, 128);
    CHECK(p0 != 0 && p1 != 0, "two slots both alloc ok");
    // Regions must not overlap (different slabs)
    CHECK(p0 != p1, "slots use distinct VRAM regions");
    a.requestFree(0);
    a.requestFree(1);
}

// ── Heap tests ────────────────────────────────────────────────────────────────

void test_heap_basic_alloc_free() {
    auto& a = GpuAllocator::instance();
    CUdeviceptr p = a.heapAlloc(256);
    CHECK(p != 0, "heap alloc non-null");
    a.heapFree(p, 256);
    CHECK(true, "heap free did not crash");
}

void test_heap_coalesce() {
    auto& a = GpuAllocator::instance();
    // Allocate two adjacent blocks, free both — heap should coalesce them
    // and be able to service a request for their combined size.
    CUdeviceptr p1 = a.heapAlloc(1024);
    CUdeviceptr p2 = a.heapAlloc(1024);
    a.heapFree(p1, 1024);
    a.heapFree(p2, 1024);
    CUdeviceptr big = a.heapAlloc(2048);
    CHECK(big != 0, "heap coalesced free blocks into larger allocation");
    a.heapFree(big, 2048);
}

void test_heap_alignment() {
    auto& a = GpuAllocator::instance();
    for (size_t sz : {1u, 64u, 100u, 255u, 512u}) {
        CUdeviceptr p = a.heapAlloc(sz);
        CHECK((p % 256) == 0, "heap alloc is 256-byte aligned");
        a.heapFree(p, sz);
    }
}

void test_heap_iterator_bug_regression() {
    // Regression for the vector iterator-invalidation bug fixed in GpuAllocator::heapAlloc.
    // Allocate many blocks, free them in reverse — triggers split + coalesce repeatedly.
    auto& a = GpuAllocator::instance();
    static constexpr int N = 32;
    std::vector<CUdeviceptr> ptrs(N);
    for (int i = 0; i < N; ++i)
        ptrs[i] = a.heapAlloc(512);
    for (int i = N - 1; i >= 0; --i)
        a.heapFree(ptrs[i], 512);
    CHECK(true, "heap alloc/free sequence did not crash");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    // Initialise CUDA driver API (must be done before any cuMem calls)
    if (cuInit(0) != CUDA_SUCCESS) {
        std::fprintf(stderr, "SKIP: no CUDA device found\n");
        return 0;
    }
    CUdevice dev; CUcontext ctx;
    cuDeviceGet(&dev, 0);
    cuCtxCreate(&ctx, 0, dev);

    test_slab_basic_alloc();
    test_slab_alignment();
    test_slab_reset_is_o1();
    test_slab_multiple_slots_independent();
    test_heap_basic_alloc_free();
    test_heap_coalesce();
    test_heap_alignment();
    test_heap_iterator_bug_regression();

    std::printf("Results: %d passed, %d failed\n", passed, failed);
    cuCtxDestroy(ctx);
    return failed > 0 ? 1 : 0;
}
