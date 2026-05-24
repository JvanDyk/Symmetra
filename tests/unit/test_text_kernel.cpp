// Unit tests for TextKernel (symm_text_encode / symm_text_decode) — CUDA device required.
//
// Build:
//   nvcc -std=c++17 -I../../engine/src test_text_kernel.cpp
//         ../../engine/src/kernels/TextKernel.cu
//         -lcuda -lcudart -o test_text_kernel
//   ./test_text_kernel
//
// Tests:
//   1. ASCII round-trip (1-byte UTF-8 code points)
//   2. 2-byte UTF-8 sequences (U+0080 – U+07FF)
//   3. 3-byte UTF-8 sequences (U+0800 – U+FFFF, e.g. CJK, emoji base chars)
//   4. 4-byte UTF-8 sequences (U+10000 – U+10FFFF, supplementary planes)
//   5. Mixed multi-byte string round-trip
//   6. Edge case: empty input
//   7. Edge case: single code point
//   8. byteLength equals the sum of individual UTF-8 byte widths
//   9. Decode correctly identifies start bytes vs continuation bytes

#include <cuda.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// Forward declarations of the kernel signatures (from TextKernel.cu)
extern "C" __global__ void symm_text_encode(const uint32_t* codepoints, uint32_t n,
                                              uint8_t* out, uint32_t* out_len_ptr);
extern "C" __global__ void symm_text_decode(const uint8_t* utf8, uint32_t byteLen,
                                              uint32_t* out, uint32_t* n_ptr);

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; std::printf("  [PASS] %s\n", msg); } \
    else { ++failed; std::fprintf(stderr, "  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

// Encode a UTF-8 string to code points on CPU (reference implementation)
static std::vector<uint32_t> utf8_to_codepoints(const std::string& s) {
    std::vector<uint32_t> cps;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t b = (uint8_t)s[i];
        uint32_t cp = 0;
        if ((b & 0x80) == 0)      { cp = b;              i += 1; }
        else if ((b & 0xE0) == 0xC0) { cp = (b & 0x1F) << 6  | ((uint8_t)s[i+1] & 0x3F); i += 2; }
        else if ((b & 0xF0) == 0xE0) { cp = (b & 0x0F) << 12 | ((uint8_t)s[i+1] & 0x3F) << 6 | ((uint8_t)s[i+2] & 0x3F); i += 3; }
        else                         { cp = (b & 0x07) << 18 | ((uint8_t)s[i+1] & 0x3F) << 12 | ((uint8_t)s[i+2] & 0x3F) << 6 | ((uint8_t)s[i+3] & 0x3F); i += 4; }
        cps.push_back(cp);
    }
    return cps;
}

// GPU encode: takes CPU code points → returns GPU-encoded UTF-8 bytes
static std::vector<uint8_t> gpu_encode(const std::vector<uint32_t>& cps) {
    if (cps.empty()) return {};

    uint32_t n = (uint32_t)cps.size();
    // Max 4 bytes per code point
    const size_t MAX_OUT = n * 4 + 4;
    const size_t SMEM    = n * sizeof(uint32_t);

    CUdeviceptr d_cp = 0; cuMemAlloc(&d_cp, n * sizeof(uint32_t));
    CUdeviceptr d_out = 0; cuMemAlloc(&d_out, MAX_OUT);
    CUdeviceptr d_len = 0; cuMemAlloc(&d_len, sizeof(uint32_t));
    cuMemcpyHtoD(d_cp, cps.data(), n * sizeof(uint32_t));
    cuMemsetD8(d_out, 0, MAX_OUT);
    cuMemsetD32(d_len, 0, 1);

    const uint32_t* d_cp_p   = (const uint32_t*)d_cp;
    uint8_t*        d_out_p  = (uint8_t*)d_out;
    uint32_t*       d_len_p  = (uint32_t*)d_len;
    symm_text_encode<<<1, n, SMEM>>>(d_cp_p, n, d_out_p, d_len_p);
    cudaDeviceSynchronize();

    uint32_t outLen = 0;
    cuMemcpyDtoH(&outLen, d_len, sizeof(uint32_t));

    std::vector<uint8_t> result(outLen);
    cuMemcpyDtoH(result.data(), d_out, outLen);

    cuMemFree(d_cp); cuMemFree(d_out); cuMemFree(d_len);
    return result;
}

// GPU decode: takes UTF-8 bytes → returns code points
static std::vector<uint32_t> gpu_decode(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return {};

    uint32_t byteLen = (uint32_t)bytes.size();
    const size_t MAX_CP = byteLen + 1;
    const size_t SMEM   = byteLen * sizeof(uint32_t);

    CUdeviceptr d_in = 0; cuMemAlloc(&d_in, byteLen);
    CUdeviceptr d_cp = 0; cuMemAlloc(&d_cp, MAX_CP * sizeof(uint32_t));
    CUdeviceptr d_n  = 0; cuMemAlloc(&d_n,  sizeof(uint32_t));
    cuMemcpyHtoD(d_in, bytes.data(), byteLen);
    cuMemsetD8(d_cp, 0, MAX_CP * sizeof(uint32_t));
    cuMemsetD32(d_n,  0, 1);

    const uint8_t*  d_in_p  = (const uint8_t*)d_in;
    uint32_t*       d_cp_p  = (uint32_t*)d_cp;
    uint32_t*       d_n_p   = (uint32_t*)d_n;
    symm_text_decode<<<1, byteLen, SMEM>>>(d_in_p, byteLen, d_cp_p, d_n_p);
    cudaDeviceSynchronize();

    uint32_t cpCount = 0;
    cuMemcpyDtoH(&cpCount, d_n, sizeof(uint32_t));

    std::vector<uint32_t> result(cpCount);
    cuMemcpyDtoH(result.data(), d_cp, cpCount * sizeof(uint32_t));

    cuMemFree(d_in); cuMemFree(d_cp); cuMemFree(d_n);
    return result;
}

// Full round-trip: CPU string → code points → GPU encode → GPU decode → compare
static bool round_trip(const std::string& s, const char* label) {
    auto cpRef = utf8_to_codepoints(s);
    auto gpuEncoded = gpu_encode(cpRef);

    // GPU-encoded bytes must match original UTF-8
    bool bytesMatch = (gpuEncoded.size() == s.size()) &&
                      std::memcmp(gpuEncoded.data(), s.data(), s.size()) == 0;
    if (!bytesMatch) {
        std::fprintf(stderr, "  [encode mismatch] %s: expected %zu bytes, got %zu\n",
                     label, s.size(), gpuEncoded.size());
        return false;
    }

    // Decode the encoded bytes back to code points
    auto gpuDecoded = gpu_decode(gpuEncoded);
    if (gpuDecoded != cpRef) {
        std::fprintf(stderr, "  [decode mismatch] %s: expected %zu codepoints, got %zu\n",
                     label, cpRef.size(), gpuDecoded.size());
        return false;
    }
    return true;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_ascii_roundtrip() {
    CHECK(round_trip("Hello, World!", "ASCII basic"), "ASCII round-trip");
    CHECK(round_trip("0123456789",    "ASCII digits"), "ASCII digits round-trip");
    CHECK(round_trip("!@#$%^&*()",    "ASCII symbols"), "ASCII symbols round-trip");
}

void test_2byte_sequences() {
    // U+00E9 = é (LATIN SMALL LETTER E WITH ACUTE)  → 0xC3 0xA9
    // U+00FC = ü (LATIN SMALL LETTER U WITH DIAERESIS) → 0xC3 0xBC
    CHECK(round_trip("\xC3\xA9\xC3\xBC", "2-byte: éü"), "2-byte UTF-8 round-trip (é, ü)");
    // Full range: U+0080
    std::string s2; s2 += (char)0xC2; s2 += (char)0x80;  // U+0080
    CHECK(round_trip(s2, "2-byte: U+0080"), "2-byte U+0080 round-trip");
    // U+07FF — last 2-byte code point
    s2.clear(); s2 += (char)0xDF; s2 += (char)0xBF;
    CHECK(round_trip(s2, "2-byte: U+07FF"), "2-byte U+07FF round-trip");
}

void test_3byte_sequences() {
    // U+4E2D = 中 (CJK UNIFIED IDEOGRAPH)    → 0xE4 0xB8 0xAD
    // U+6587 = 文                              → 0xE6 0x96 0x87
    CHECK(round_trip("\xE4\xB8\xAD\xE6\x96\x87", "3-byte: 中文"), "3-byte CJK round-trip");
    // U+0800 — first 3-byte code point
    std::string s3; s3 += (char)0xE0; s3 += (char)0xA0; s3 += (char)0x80;
    CHECK(round_trip(s3, "3-byte: U+0800"), "3-byte U+0800 round-trip");
}

void test_4byte_sequences() {
    // U+1F600 = 😀 (GRINNING FACE)           → 0xF0 0x9F 0x98 0x80
    CHECK(round_trip("\xF0\x9F\x98\x80", "4-byte: 😀"), "4-byte emoji round-trip");
    // U+10000 — first 4-byte code point (Linear B Syllabary Sign)
    std::string s4; s4 += (char)0xF0; s4 += (char)0x90; s4 += (char)0x80; s4 += (char)0x80;
    CHECK(round_trip(s4, "4-byte: U+10000"), "4-byte U+10000 round-trip");
    // U+10FFFF — highest valid Unicode code point
    s4.clear(); s4 += (char)0xF4; s4 += (char)0x8F; s4 += (char)0xBF; s4 += (char)0xBF;
    CHECK(round_trip(s4, "4-byte: U+10FFFF"), "4-byte U+10FFFF round-trip");
}

void test_mixed_string() {
    // Mix of ASCII, 2-byte, 3-byte, 4-byte
    // "Héllo 中文 😀" — h=1B, é=2B, llo=3×1B, space=1B, 中=3B, 文=3B, space=1B, 😀=4B
    std::string mixed = "H\xC3\xA9llo \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80";
    CHECK(round_trip(mixed, "mixed"), "mixed multi-byte round-trip");
}

void test_empty_input() {
    auto enc = gpu_encode({});
    CHECK(enc.empty(), "empty encode → empty output");
    auto dec = gpu_decode({});
    CHECK(dec.empty(), "empty decode → empty output");
}

void test_single_codepoint() {
    // Single ASCII
    CHECK(round_trip("A", "single ASCII"), "single ASCII code point round-trip");
    // Single emoji
    CHECK(round_trip("\xF0\x9F\x91\x8D", "single emoji"), "single emoji (👍) round-trip");
}

void test_byte_length_correct() {
    // "ABC" → 3 bytes, all ASCII
    auto cps = utf8_to_codepoints("ABC");
    auto enc = gpu_encode(cps);
    CHECK(enc.size() == 3, "byteLength(\"ABC\") == 3");

    // "é" (U+00E9) → 2 bytes
    cps = utf8_to_codepoints("\xC3\xA9");
    enc = gpu_encode(cps);
    CHECK(enc.size() == 2, "byteLength(é) == 2");

    // "中" (U+4E2D) → 3 bytes
    cps = utf8_to_codepoints("\xE4\xB8\xAD");
    enc = gpu_encode(cps);
    CHECK(enc.size() == 3, "byteLength(中) == 3");

    // "😀" (U+1F600) → 4 bytes
    cps = utf8_to_codepoints("\xF0\x9F\x98\x80");
    enc = gpu_encode(cps);
    CHECK(enc.size() == 4, "byteLength(😀) == 4");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    if (cuInit(0) != CUDA_SUCCESS) {
        std::fprintf(stderr, "SKIP: no CUDA device\n");
        return 0;
    }
    CUdevice dev; CUcontext ctx;
    cuDeviceGet(&dev, 0);
    cuCtxCreate(&ctx, 0, dev);

    std::printf("── TextKernel (UTF-8 encode/decode) ────────────────────────────\n");
    test_ascii_roundtrip();
    test_2byte_sequences();
    test_3byte_sequences();
    test_4byte_sequences();
    test_mixed_string();
    test_empty_input();
    test_single_codepoint();
    test_byte_length_correct();

    std::printf("\nResults: %d passed, %d failed\n", passed, failed);
    cuCtxDestroy(ctx);
    return failed > 0 ? 1 : 0;
}
