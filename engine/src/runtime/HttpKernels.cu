// Symmetra HTTP Kernels
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

// ── Serial route match (single-thread path) ───────────────────────────────────
__device__ int symm_match_route(const char* method, int mlen,
                                 const char* path,   int plen) {
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

// ── Warp-parallel route match ─────────────────────────────────────────────────
// All 32 threads in a warp call this together (convergent call site).
// Each lane checks one route simultaneously using __ballot_sync — the entire
// warp produces a 32-bit mask in one clock cycle. For >32 routes we iterate
// in rounds of 32, still O(numRoutes/32) instead of O(numRoutes).
__device__ int symm_warp_match_route(const char* method, int mlen,
                                      const char* path,   int plen) {
    // Build request hash once — all lanes compute the same value
    char key[256];
    int klen = 0;
    for (int i = 0; i < mlen && klen < 250; i++) key[klen++] = method[i];
    for (int i = 0; i < plen && klen < 255; i++) key[klen++] = path[i];
    uint64_t reqHash = symm_fnv1a(key, klen);

    int lane = threadIdx.x & 31;  // lane ID within warp (0..31)

    for (int base = 0; base < ROUTE_COUNT; base += 32) {
        int    idx       = base + lane;
        // Each lane loads its route hash (or 0 if out of range)
        uint64_t routeHash = (idx < ROUTE_COUNT) ? ROUTE_HASHES[idx] : 0;
        bool   matched   = (idx < ROUTE_COUNT) && (routeHash == reqHash);
        // __ballot_sync: all 32 lanes vote simultaneously → 32-bit bitmask
        uint32_t ballot  = __ballot_sync(0xFFFFFFFF, matched);
        if (ballot) return base + __ffs(ballot) - 1;  // __ffs: bit position of first set bit
    }
    return -1;
}

// ── Parallel header parsing kernel ────────────────────────────────────────────
// Each thread parses one HTTP header line from the raw request buffer.
// blockDim.x == number of header slots (up to 32 per warp).
//
// out_keys / out_vals: device pointers to key/value string buffers (pre-allocated in slab)
// out_count: output — number of headers found
__global__ void symm_parse_headers(const char* __restrict__ raw, int rawLen,
                                    char* __restrict__ out_keys,
                                    char* __restrict__ out_vals,
                                    int   maxHeaders,
                                    int   maxFieldLen,
                                    int*  out_count) {
    __shared__ int lineOffsets[64];   // start of each '\r\n'-terminated line
    __shared__ int lineCount;

    // Thread 0 finds all line starts
    if (threadIdx.x == 0) {
        lineCount = 0;
        // Skip request line (first \r\n)
        int pos = 0;
        while (pos < rawLen - 1 && !(raw[pos] == '\r' && raw[pos+1] == '\n')) pos++;
        pos += 2;  // skip \r\n

        // Collect header line start offsets
        while (pos < rawLen - 1 && lineCount < 63) {
            if (raw[pos] == '\r' && raw[pos+1] == '\n') break;  // end of headers
            lineOffsets[lineCount++] = pos;
            // Advance to next line
            while (pos < rawLen - 1 && !(raw[pos] == '\r' && raw[pos+1] == '\n')) pos++;
            pos += 2;
        }
        *out_count = min(lineCount, maxHeaders);
    }
    __syncthreads();

    // Each thread parses its assigned header line
    int tid = threadIdx.x;
    if (tid >= *out_count) return;

    int start = lineOffsets[tid];
    char* keyBuf = out_keys + tid * maxFieldLen;
    char* valBuf = out_vals + tid * maxFieldLen;

    // Find ': ' separator
    int colon = start;
    while (colon < rawLen && raw[colon] != ':') colon++;

    // Copy key (lowercase for case-insensitive match)
    int ki = 0;
    for (int i = start; i < colon && ki < maxFieldLen - 1; i++) {
        char c = raw[i];
        keyBuf[ki++] = (c >= 'A' && c <= 'Z') ? (c | 0x20) : c;  // tolower
    }
    keyBuf[ki] = '\0';

    // Copy value (skip ': ')
    int vi = 0;
    int vs = colon + 2;   // skip ':'  and ' '
    while (vs < rawLen && raw[vs] != '\r' && vi < maxFieldLen - 1)
        valBuf[vi++] = raw[vs++];
    valBuf[vi] = '\0';
}

// ── Parallel response serialiser ──────────────────────────────────────────────
// N threads each write one field of a JSON object into the output buffer.
// Uses a prefix-sum to determine each field's start offset.
__global__ void symm_serialise_json(const char* const* __restrict__ keys,
                                     const char* const* __restrict__ vals,
                                     int*  __restrict__ keyLens,
                                     int*  __restrict__ valLens,
                                     int   fieldCount,
                                     char* __restrict__ out,
                                     int*  __restrict__ outLen) {
    extern __shared__ int offsets[];  // prefix sum scratch

    int tid = threadIdx.x;
    if (tid >= fieldCount) return;

    // Each field contributes: "key":val, (plus quotes, colon, comma, space)
    // Length = 1(") + keyLen + 2(":) + 1(") + valLen + 1(") + 2(, ) (last omits comma)
    int fieldLen = 1 + keyLens[tid] + 3 + valLens[tid] + 1
                 + (tid < fieldCount - 1 ? 2 : 0);

    offsets[tid] = fieldLen;
    __syncthreads();

    // Inclusive prefix sum (Hillis-Steele)
    for (int stride = 1; stride < blockDim.x; stride <<= 1) {
        int val = (tid >= stride) ? offsets[tid - stride] : 0;
        __syncthreads();
        offsets[tid] += val;
        __syncthreads();
    }

    // Write: {"key":"val",...}
    // Header '{' written by thread 0
    if (tid == 0) {
        out[0] = '{';
        *outLen = offsets[fieldCount - 1] + 2;  // +2 for { and }
    }
    __syncthreads();

    int startOff = 1 + (tid == 0 ? 0 : offsets[tid - 1]);
    char* dst = out + startOff;

    *dst++ = '"';
    for (int i = 0; i < keyLens[tid]; i++) *dst++ = keys[tid][i];
    *dst++ = '"'; *dst++ = ':'; *dst++ = '"';
    for (int i = 0; i < valLens[tid]; i++) *dst++ = vals[tid][i];
    *dst++ = '"';
    if (tid < fieldCount - 1) { *dst++ = ','; *dst++ = ' '; }

    // All threads must finish writing their fields before thread 0 writes '}'.
    // Without this sync, thread 0 might write '}' before the last field thread
    // finishes, producing a corrupted JSON object.
    __syncthreads();

    if (tid == 0) out[*outLen - 1] = '}';
}
