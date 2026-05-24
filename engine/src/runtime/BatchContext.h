#pragma once
/**
 * BatchContext — VRAM layout for a single warp-aligned batch.
 *
 * The CPU allocates one BatchContext per CUDA stream (reused across requests).
 * Before each kernel launch the CPU fills the input arrays, then after sync
 * reads the output arrays.
 *
 * Memory layout (all pointers are device pointers in VRAM):
 *
 *   d_bodies[i]    — pointer to request body i (in a single contiguous body buffer)
 *   d_body_lens[i] — byte length of body i
 *   d_paths[i]     — pointer to URL path i
 *   d_methods[i]   — pointer to HTTP method string i
 *   d_resp_bufs[i] — pointer to response output buffer i
 *   d_resp_lens[i] — response byte length written by kernel i
 *   d_statuses[i]  — HTTP status written by kernel i (200, 404, 500 …)
 *   d_active       — bitmask: bit i set = thread i has real work to do
 *
 * All arrays are sized BATCH_CAPACITY.  Threads with tid >= actual_count
 * are masked off in the kernel using the d_active bitmask.
 *
 * The body buffer and response buffers are sized conservatively:
 *   body   : MAX_BODY_BYTES per slot
 *   response: MAX_RESP_BYTES per slot
 *
 * For a 256-slot batch:
 *   bodies  : 256 × 64 KB  =  16 MB
 *   responses: 256 × 64 KB  =  16 MB
 *   overhead  (pointers, lengths, flags): ~10 KB
 *   Total VRAM per stream: ~32 MB  (acceptable: A100 has 80 GB VRAM)
 */

#include <cstdint>
#include <cstring>
#include <cuda.h>

namespace symm {

struct BatchContext {
    // ── Sizes / constants ────────────────────────────────────────────────────
    static constexpr int WARP_WIDTH       = 32;
    static constexpr int BATCH_CAPACITY   = 256;       // 8 warps per block
    static constexpr int MAX_BODY_BYTES   = 65536;     // 64 KB per request body
    static constexpr int MAX_RESP_BYTES   = 65536;     // 64 KB per response
    static constexpr int MAX_PATH_BYTES   = 2048;
    static constexpr int MAX_METHOD_BYTES = 8;

    // ── Device pointers (all allocated in a single cuMemAlloc call) ──────────
    // Input:
    char**        d_bodies;      // [BATCH_CAPACITY] pointers into d_bodyBuf
    int*          d_body_lens;   // [BATCH_CAPACITY]
    char**        d_paths;       // [BATCH_CAPACITY] pointers into d_pathBuf
    char**        d_methods;     // [BATCH_CAPACITY] pointers into d_methodBuf
    uint32_t*     d_active;      // bitmask: 256 bits → 8 uint32_t words

    // Output (written by GPU kernel):
    char**         d_resp_bufs;  // [BATCH_CAPACITY] pointers into d_respBuf
    unsigned int*  d_resp_lens;  // [BATCH_CAPACITY]
    int*           d_statuses;   // [BATCH_CAPACITY]

    // Data buffers (contiguous, indexed by slot):
    char*  d_bodyBuf;    // [BATCH_CAPACITY × MAX_BODY_BYTES]
    char*  d_pathBuf;    // [BATCH_CAPACITY × MAX_PATH_BYTES]
    char*  d_methodBuf;  // [BATCH_CAPACITY × MAX_METHOD_BYTES]
    char*  d_respBuf;    // [BATCH_CAPACITY × MAX_RESP_BYTES]

    // ── Host-side metadata (CPU only) ────────────────────────────────────────
    int          actualCount = 0;   // real requests in this batch (≤ BATCH_CAPACITY)
    CUdeviceptr  d_base      = 0;   // base of the single VRAM allocation
    size_t       allocBytes  = 0;   // total bytes in d_base allocation

    // ── Lifecycle ────────────────────────────────────────────────────────────

    // Allocate all VRAM for this BatchContext.
    // Call once at startup, reuse across requests.
    CUresult allocate() {
        // Layout computation — all arrays packed into one allocation.
        size_t offPtr    = 0;
        size_t szPtrArr  = BATCH_CAPACITY * sizeof(void*);
        size_t szIntArr  = BATCH_CAPACITY * sizeof(int);
        size_t szUintArr = BATCH_CAPACITY * sizeof(unsigned int);
        size_t szU32Arr  = 8 * sizeof(uint32_t);   // 256-bit bitmask
        size_t szBodyBuf = (size_t)BATCH_CAPACITY * MAX_BODY_BYTES;
        size_t szPathBuf = (size_t)BATCH_CAPACITY * MAX_PATH_BYTES;
        size_t szMethBuf = (size_t)BATCH_CAPACITY * MAX_METHOD_BYTES;
        size_t szRespBuf = (size_t)BATCH_CAPACITY * MAX_RESP_BYTES;

        allocBytes = (szPtrArr * 4)   // d_bodies, d_paths, d_methods, d_resp_bufs
                   + (szIntArr * 1)   // d_body_lens
                   + (szUintArr * 1)  // d_resp_lens
                   + (szIntArr * 1)   // d_statuses
                   + szU32Arr         // d_active
                   + szBodyBuf + szPathBuf + szMethBuf + szRespBuf;

        CUresult r = cuMemAlloc(&d_base, allocBytes);
        if (r != CUDA_SUCCESS) return r;

        // Zero the whole allocation so inactive slots are clean
        cuMemsetD8(d_base, 0, allocBytes);

        // Assign pointer fields into the allocation
        char* p = reinterpret_cast<char*>(d_base);

        auto assign = [&](void*& field, size_t bytes) {
            field = p; p += bytes;
        };

        assign(reinterpret_cast<void*&>(d_bodies),    szPtrArr);
        assign(reinterpret_cast<void*&>(d_body_lens), szIntArr);
        assign(reinterpret_cast<void*&>(d_paths),     szPtrArr);
        assign(reinterpret_cast<void*&>(d_methods),   szPtrArr);
        assign(reinterpret_cast<void*&>(d_active),    szU32Arr);
        assign(reinterpret_cast<void*&>(d_resp_bufs), szPtrArr);
        assign(reinterpret_cast<void*&>(d_resp_lens), szUintArr);
        assign(reinterpret_cast<void*&>(d_statuses),  szIntArr);
        assign(reinterpret_cast<void*&>(d_bodyBuf),   szBodyBuf);
        assign(reinterpret_cast<void*&>(d_pathBuf),   szPathBuf);
        assign(reinterpret_cast<void*&>(d_methodBuf), szMethBuf);
        assign(reinterpret_cast<void*&>(d_respBuf),   szRespBuf);

        return CUDA_SUCCESS;
    }

    // Upload one batch of CPU requests into VRAM.
    // bodies[i], paths[i], methods[i] are host pointers.
    // count is the actual number of requests (≤ BATCH_CAPACITY).
    CUresult uploadBatch(
        const char** h_bodies,     const int* h_body_lens,
        const char** h_paths,      const char** h_methods,
        int count, CUstream stream
    ) {
        actualCount = count;

        // Flat body buffer upload
        for (int i = 0; i < count; i++) {
            char* dst = d_bodyBuf + (size_t)i * MAX_BODY_BYTES;
            int   len = std::min(h_body_lens[i], MAX_BODY_BYTES - 1);
            cuMemcpyHtoDAsync((CUdeviceptr)dst, h_bodies[i],
                              (size_t)(len + 1), stream);
        }

        // Build h_bodyPtrs: device pointers pointing into d_bodyBuf
        std::vector<const char*> h_bodyPtrs(BATCH_CAPACITY, nullptr);
        std::vector<const char*> h_pathPtrs(BATCH_CAPACITY, nullptr);
        std::vector<const char*> h_methPtrs(BATCH_CAPACITY, nullptr);
        std::vector<char*>       h_respPtrs(BATCH_CAPACITY, nullptr);
        std::vector<int>         h_lens(BATCH_CAPACITY, 0);

        for (int i = 0; i < count; i++) {
            h_bodyPtrs[i] = d_bodyBuf + (size_t)i * MAX_BODY_BYTES;
            h_respPtrs[i] = d_respBuf + (size_t)i * MAX_RESP_BYTES;

            // Upload path
            char* dst_path = d_pathBuf + (size_t)i * MAX_PATH_BYTES;
            int   plen = (int)strnlen(h_paths[i], MAX_PATH_BYTES - 1);
            cuMemcpyHtoDAsync((CUdeviceptr)dst_path, h_paths[i],
                              (size_t)(plen + 1), stream);
            h_pathPtrs[i] = dst_path;

            // Upload method
            char* dst_meth = d_methodBuf + (size_t)i * MAX_METHOD_BYTES;
            int   mlen = (int)strnlen(h_methods[i], MAX_METHOD_BYTES - 1);
            cuMemcpyHtoDAsync((CUdeviceptr)dst_meth, h_methods[i],
                              (size_t)(mlen + 1), stream);
            h_methPtrs[i] = dst_meth;

            h_lens[i] = h_body_lens[i];
        }

        // Upload the pointer arrays themselves
        cuMemcpyHtoDAsync((CUdeviceptr)d_bodies,
                          h_bodyPtrs.data(), BATCH_CAPACITY * sizeof(void*), stream);
        cuMemcpyHtoDAsync((CUdeviceptr)d_body_lens,
                          h_lens.data(),     BATCH_CAPACITY * sizeof(int),   stream);
        cuMemcpyHtoDAsync((CUdeviceptr)d_paths,
                          h_pathPtrs.data(), BATCH_CAPACITY * sizeof(void*), stream);
        cuMemcpyHtoDAsync((CUdeviceptr)d_methods,
                          h_methPtrs.data(), BATCH_CAPACITY * sizeof(void*), stream);
        cuMemcpyHtoDAsync((CUdeviceptr)d_resp_bufs,
                          h_respPtrs.data(), BATCH_CAPACITY * sizeof(void*), stream);

        // Active bitmask: bits 0..(count-1) set
        uint32_t h_active[8] = {0};
        for (int i = 0; i < count && i < BATCH_CAPACITY; i++)
            h_active[i / 32] |= (1u << (i % 32));
        cuMemcpyHtoDAsync((CUdeviceptr)d_active,
                          h_active, 8 * sizeof(uint32_t), stream);

        return CUDA_SUCCESS;
    }

    // Download response lengths and status codes after kernel completion.
    CUresult downloadResults(
        std::vector<unsigned int>& out_lens,
        std::vector<int>&          out_statuses,
        std::vector<std::string>&  out_bodies,
        CUstream stream
    ) {
        int n = actualCount;
        out_lens.resize(n);
        out_statuses.resize(n);
        out_bodies.resize(n);

        // Download lengths and statuses (small — sync is OK)
        std::vector<unsigned int> h_lens(n);
        std::vector<int>          h_stat(n);
        cuMemcpyDtoHAsync(h_lens.data(), (CUdeviceptr)d_resp_lens,
                          n * sizeof(unsigned int), stream);
        cuMemcpyDtoHAsync(h_stat.data(), (CUdeviceptr)d_statuses,
                          n * sizeof(int), stream);
        cuStreamSynchronize(stream);

        for (int i = 0; i < n; i++) {
            out_lens[i]     = h_lens[i];
            out_statuses[i] = h_stat[i];

            // Download response body
            int len = (int)std::min((unsigned int)MAX_RESP_BYTES, h_lens[i]);
            if (len > 0) {
                out_bodies[i].resize(len);
                cuMemcpyDtoH(out_bodies[i].data(),
                             (CUdeviceptr)(d_respBuf + (size_t)i * MAX_RESP_BYTES),
                             len);
            }
        }
        return CUDA_SUCCESS;
    }

    void free() {
        if (d_base) { cuMemFree(d_base); d_base = 0; }
    }
};

} // namespace symm
