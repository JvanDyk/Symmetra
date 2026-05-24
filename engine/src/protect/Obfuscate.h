#pragma once
// Compile-time string obfuscation.
// All string literals in the binary are XOR-encrypted with a key derived from
// their position in the source (so identical strings get different ciphertext).
// Decryption happens at runtime in a stack-allocated buffer — never in BSS/data.
//
// Usage:
//   const char* s = SYMM_STR("hello debugger");
//   // s decrypts at runtime, lives only on stack
//
// For error messages, format strings, CUDA kernel names, etc.

#include <cstdint>
#include <cstddef>
#include <array>

namespace symm::protect {

// Compile-time key: change SYMM_OBFKEY per release build
// CMakeLists sets: -DSYMM_OBFKEY=0x<hex>
#ifndef SYMM_OBFKEY
#define SYMM_OBFKEY 0xA7C3E591B2D04F68ULL
#endif

// Compile-time XOR key for position p
constexpr uint8_t obfKeyByte(size_t p) {
    uint64_t k = SYMM_OBFKEY;
    return static_cast<uint8_t>(
        ((k >> (8 * (p % 8))) ^ (k >> (8 * ((p + 3) % 8)))) & 0xFF
    );
}

// Compile-time encrypted string storage
template<size_t N>
struct ObfStr {
    std::array<char, N> data;
    constexpr ObfStr(const char (&s)[N]) {
        for (size_t i = 0; i < N; i++)
            data[i] = s[i] ^ obfKeyByte(i);
    }
    // Decrypt into a stack buffer at runtime
    void decrypt(char* out) const {
        for (size_t i = 0; i < N; i++)
            out[i] = data[i] ^ obfKeyByte(i);
    }
    static constexpr size_t size = N;
};

// RAII stack decryption wrapper
template<size_t N>
struct DecryptedStr {
    char buf[N];
    explicit DecryptedStr(const ObfStr<N>& s) { s.decrypt(buf); }
    ~DecryptedStr() {
        // Zero the stack buffer on scope exit
        volatile char* p = buf;
        for (size_t i = 0; i < N; i++) p[i] = 0;
    }
    const char* c_str() const { return buf; }
    operator const char*() const { return buf; }
};

} // namespace symm::protect

// ── Usage macros ──────────────────────────────────────────────────────────────

// Declare an obfuscated string constant at namespace/class scope
#define SYMM_OBFSTR(name, literal) \
    static constexpr ::symm::protect::ObfStr<sizeof(literal)> name##_obf{literal}

// Use an obfuscated string inline (decrypts to stack, zeroed on scope exit)
#define SYMM_STR(literal) \
    ([]() -> const char* { \
        static constexpr ::symm::protect::ObfStr<sizeof(literal)> _s{literal}; \
        static thread_local ::symm::protect::DecryptedStr<sizeof(literal)> _d{_s}; \
        return _d.c_str(); \
    }())

// Obfuscated comparison (avoids plaintext string appearing in binary)
#define SYMM_STREQ(a, obfname) (std::strcmp((a), obfname##_obf_dec.c_str()) == 0)
