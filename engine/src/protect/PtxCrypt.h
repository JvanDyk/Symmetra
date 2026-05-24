#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace symm::protect {

// Encrypt PTX at build time and embed as a byte array.
// At runtime, decrypt into a malloc buffer, load into CUDA, then zero+free immediately.
// The decrypted PTX is never written to disk.

// 32-byte key derived at build time (set by build system, never shipped in source)
// In CMakeLists.txt: add_compile_definitions(SYMM_PTX_KEY=...)
// Default fallback key for development builds (CHANGE FOR RELEASE):
#ifndef SYMM_PTX_KEY
#define SYMM_PTX_KEY \
    0x53,0x59,0x4d,0x4d,0x65,0x74,0x72,0x61, \
    0x50,0x72,0x6f,0x74,0x65,0x63,0x74,0x69, \
    0x6f,0x6e,0x4b,0x65,0x79,0x32,0x30,0x32, \
    0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00
#endif

// 12-byte IV (also compile-time; randomise per release build)
#ifndef SYMM_PTX_IV
#define SYMM_PTX_IV 0x53,0x59,0x4d,0x4d,0x49,0x56,0x30,0x30,0x30,0x31,0x00,0x00
#endif

struct PtxCrypt {
    // Decrypt an embedded encrypted PTX blob.
    // Returns decrypted PTX as a string.
    // Caller MUST zero the returned string immediately after loading it into CUDA.
    static std::string decrypt(const uint8_t* blob, size_t blobLen);

    // Encrypt a PTX string (used by the build-time tool encrypt_ptx.cpp).
    static std::vector<uint8_t> encrypt(const std::string& ptx);

    // Securely zero a buffer (not optimized away by compiler).
    static void secureZero(void* buf, size_t n);
};

} // namespace symm::protect
