// TextKernel.cu — GPU UTF-8 encode / decode
// Loaded via KernelStore; never ships as plaintext PTX.

#include <stdint.h>
#include <stddef.h>

// ── symm_text_encode ──────────────────────────────────────────────────────────
// Pass 1: each thread counts how many UTF-8 bytes its code point needs.
// Pass 2: exclusive prefix-sum gives each thread its write offset.
// Pass 3: each thread writes its bytes.
//
// Kernel is called three times from the runtime (count → scan → write).
// For simplicity we fuse all three into one kernel using shared memory and a
// single block per chunk — sufficient for strings up to 48k chars (one warp
// per 1k chars is still fast enough).
//
// Input:  uint32_t code_points[n]
// Output: uint8_t  utf8[out_len],  uint32_t* out_len_ptr (device scalar)
//
// Limit: n must be ≤ 65 535 (fits in uint16_t offsets in shared memory).

extern "C" __global__ void symm_text_encode(
        const uint32_t* __restrict__ codepoints,
        uint32_t   n,
        uint8_t*   __restrict__ out,
        uint32_t*  out_len_ptr)
{
    extern __shared__ uint32_t smem[];   // [n] byte-count per code point

    int tid = threadIdx.x + blockIdx.x * blockDim.x;

    // ── Pass 1: how many bytes does codepoint[tid] encode to? ─────────────────
    uint32_t cp    = (tid < (int)n) ? codepoints[tid] : 0;
    uint32_t bytes = 0;
    if      (cp < 0x80)     bytes = 1;
    else if (cp < 0x800)    bytes = 2;
    else if (cp < 0x10000)  bytes = 3;
    else                    bytes = 4;

    if (tid < (int)n) smem[tid] = bytes;
    __syncthreads();

    // ── Pass 2: inclusive prefix-sum within block (only correct for n ≤ blockDim.x) ──
    // For production a multi-block CUB scan replaces this; this version handles
    // strings up to 1024 code points per block which covers most server strings.
    for (uint32_t stride = 1; stride < blockDim.x; stride <<= 1) {
        uint32_t val = (tid >= (int)stride && tid < (int)n) ? smem[tid - stride] : 0;
        __syncthreads();
        if (tid < (int)n) smem[tid] += val;
        __syncthreads();
    }

    // smem[tid] is now the inclusive sum → write offset = smem[tid-1] (exclusive)
    uint32_t writeOffset = (tid > 0 && tid < (int)n) ? smem[tid - 1] : 0;
    if (tid == 0 && out_len_ptr) *out_len_ptr = smem[n - 1];

    // ── Pass 3: write UTF-8 bytes ─────────────────────────────────────────────
    if (tid < (int)n && out != nullptr) {
        uint8_t* dst = out + writeOffset;
        if      (cp < 0x80) {
            dst[0] = (uint8_t)cp;
        } else if (cp < 0x800) {
            dst[0] = (uint8_t)(0xC0 | (cp >> 6));
            dst[1] = (uint8_t)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            dst[0] = (uint8_t)(0xE0 | (cp >> 12));
            dst[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            dst[2] = (uint8_t)(0x80 | (cp & 0x3F));
        } else {
            dst[0] = (uint8_t)(0xF0 | (cp >> 18));
            dst[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
            dst[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            dst[3] = (uint8_t)(0x80 | (cp & 0x3F));
        }
    }
}

// ── symm_text_decode ──────────────────────────────────────────────────────────
// Input:  uint8_t utf8[byteLen]
// Output: uint32_t codepoints[n], uint32_t* n_ptr (device scalar)
//
// Strategy: each thread handles one start byte. Non-start bytes are skipped.
// Continuation bytes (10xxxxxx) write nothing — thread with the matching
// start byte writes the full code point.
//
// This is a two-pass approach:
//   Pass 1: mark start bytes, count code points, compute write offsets.
//   Pass 2: each start-byte thread writes one code point.

extern "C" __global__ void symm_text_decode(
        const uint8_t* __restrict__ utf8,
        uint32_t    byteLen,
        uint32_t*   __restrict__ out,
        uint32_t*   n_ptr)
{
    extern __shared__ uint32_t smem[];  // [byteLen] — 1 if start byte, else 0

    int tid = threadIdx.x + blockIdx.x * blockDim.x;

    // ── Pass 1: tag start bytes ───────────────────────────────────────────────
    uint8_t b = (tid < (int)byteLen) ? utf8[tid] : 0;
    // start byte: anything that is NOT 10xxxxxx (continuation)
    bool isStart = (tid < (int)byteLen) && ((b & 0xC0) != 0x80);
    smem[tid < (int)byteLen ? tid : 0] = isStart ? 1 : 0;
    __syncthreads();

    // ── Pass 2: exclusive prefix-sum → write index ────────────────────────────
    for (uint32_t stride = 1; stride < blockDim.x; stride <<= 1) {
        uint32_t val = (tid >= (int)stride && tid < (int)byteLen) ? smem[tid - stride] : 0;
        __syncthreads();
        if (tid < (int)byteLen) smem[tid] += val;
        __syncthreads();
    }
    // inclusive sum in smem; exclusive offset = smem[tid-1] for start bytes
    if (tid == (int)byteLen - 1 && n_ptr) *n_ptr = smem[tid];

    if (!isStart || out == nullptr) return;

    uint32_t writeIdx = (tid > 0) ? smem[tid - 1] : 0;

    // ── Pass 3: decode code point starting at byte[tid] ───────────────────────
    uint32_t cp = 0;
    if ((b & 0x80) == 0) {
        cp = b;                                        // 1-byte
    } else if ((b & 0xE0) == 0xC0) {
        cp = ((uint32_t)(b & 0x1F) << 6)
           | (tid + 1 < (int)byteLen ? (utf8[tid+1] & 0x3F) : 0);
    } else if ((b & 0xF0) == 0xE0) {
        cp = ((uint32_t)(b & 0x0F) << 12)
           | (tid + 1 < (int)byteLen ? ((uint32_t)(utf8[tid+1] & 0x3F) << 6) : 0)
           | (tid + 2 < (int)byteLen ? (utf8[tid+2] & 0x3F) : 0);
    } else if ((b & 0xF8) == 0xF0) {
        cp = ((uint32_t)(b & 0x07) << 18)
           | (tid + 1 < (int)byteLen ? ((uint32_t)(utf8[tid+1] & 0x3F) << 12) : 0)
           | (tid + 2 < (int)byteLen ? ((uint32_t)(utf8[tid+2] & 0x3F) << 6) : 0)
           | (tid + 3 < (int)byteLen ? (utf8[tid+3] & 0x3F) : 0);
    }
    out[writeIdx] = cp;
}
