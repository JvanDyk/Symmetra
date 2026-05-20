// Symmetra HTTP Kernels
// GPU-side HTTP parsing and routing using HYBRID_SPATIAL + RAYK_ADAPTIVE
// (same approach as CUDA_TM_API/src/tools/GpuHttpParser.ts)
#include <cuda_runtime.h>
#include <stdint.h>

// Token table for HTTP method detection (warp-friendly: 32-byte aligned lookup)
__device__ __constant__ uint8_t METHOD_TABLE[256] = {
    // ASCII table: 1 = valid method char, 0 = delimiter
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x00
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x10
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x20
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 0x30
    0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  // 0x40: A-O
    1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,  // 0x50: P-Z
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

// FNV-1a hash for route matching (fast, warp-divergence-free)
__device__ uint64_t symm_fnv1a(const char* s, int len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

// GPU route table (hashes of "METHOD/path")
// Populated at startup from the compiled route table
__device__ __constant__ uint64_t ROUTE_HASHES[64];
__device__ __constant__ int      ROUTE_COUNT = 0;

// Match a request to a route index (-1 = not found)
__device__ int symm_match_route(const char* method, int mlen,
                                 const char* path,   int plen) {
    // Concatenate method + path for hash
    char key[256];
    int klen = 0;
    for (int i = 0; i < mlen && klen < 250; i++) key[klen++] = method[i];
    for (int i = 0; i < plen && klen < 255; i++) key[klen++] = path[i];
    uint64_t h = symm_fnv1a(key, klen);

    for (int i = 0; i < ROUTE_COUNT; i++) {
        if (ROUTE_HASHES[i] == h) return i;
    }
    return -1;
}
