# TLS 1.3 PSK Implementation Security Assessment

## Current Implementation Status

### What's Implemented ✅
- TLS handshake skeleton (ClientHello/ServerHello exchange)
- AES-256-CTR encryption using Aes256.h library
- PSK (Pre-Shared Key) support with hex-decoded keys
- Per-session key derivation
- Integration with HTTP/1.1 record layer

### Critical Security Issues ⚠️

#### 1. **Missing Authentication Tags (CRITICAL)**
- **Problem**: Current code uses AES-256-CTR without GCM (Galois/Counter Mode)
- **TLS Requirement**: TLS 1.3 uses AEAD ciphers (authenticated encryption)
- **Risk**: Ciphertext can be modified undetected; no proof of authenticity
- **Impact**: Not suitable for production authentication/sensitive APIs

```cpp
// Current: CTR-only, no authentication
cipher.ctr(plaintext, ciphertext.data(), len, nonce.data());

// Should be: AEAD with authentication tag
// cipher.gcm_encrypt(plaintext, ciphertext, tag, len, nonce, aad);
```

#### 2. **Weak Key Derivation (HIGH)**
- **Problem**: Uses simple XOR of PSK with nonces
- **TLS Requirement**: HKDF-SHA256 with proper cryptographic binding
- **Current Code**:
```cpp
sess.clientKey[i] = psk[i] ^ serverRandom[i % 16] ^ clientRandom[i % 16];
```
- **Issue**: XOR is deterministic and invertible; not suitable for deriving session keys
- **Fix Needed**: Implement SHA256 HMAC or full HKDF-SHA256

#### 3. **Weak Random Generation (MEDIUM)**
- **Problem**: Uses `rand()` which is not cryptographically secure
- **Issue**: Nonces/salts should be unpredictable
- **Fix Needed**: Use `/dev/urandom` on Unix or `CryptGenRandom` on Windows

```cpp
// Current: weak RNG
serverRandom[i] = static_cast<uint8_t>(rand());

// Should be: secure RNG
// Use system entropy source (getrandom, /dev/urandom, etc.)
```

#### 4. **Incomplete Handshake Protocol (MEDIUM)**
- **Problem**: No proper ClientHello parsing/validation
- **Issue**: `handleTlsClientHello` doesn't wait for client hello or verify PSK
- **Missing**:
  - Client hello parsing
  - PSK selection logic
  - Certificate validation (if using cert mode)
  - Finished message validation

#### 5. **No Perfect Forward Secrecy (PFS)**
- **Problem**: PSK-only mode without (EC)DHE
- **Current**: All sessions encrypted with same key material after derivation
- **Standard**: TLS 1.3 recommends PSK-DHE or ephemeral keys

---

## Recommendations for Production

### MVP Fix (Maintains Current Architecture)
1. ✅ Add GCM authentication tags to Aes256.h
2. ✅ Replace XOR KDF with SHA256 HMAC-based KDF
3. ✅ Use secure random (getrandom/CryptGenRandom)
4. ✅ Add minimal ClientHello validation

### Production-Grade Fix
1. Use OpenSSL or BoringSSL instead of custom TLS
2. Implement full TLS 1.3 PSK-DHE mode
3. Add certificate support (if needed beyond PSK)
4. Proper session resumption support

---

## Current Threat Model

**What's Protected**:
- ✅ Encryption against passive network sniffing (CTR mode works)
- ✅ Replay detection (per-session nonces)
- ✅ Per-client key derivation

**What's NOT Protected**:
- ❌ Active tampering (no authentication tags)
- ❌ Key derivation attacks (weak KDF)
- ❌ Nonce prediction (weak RNG)
- ❌ Handshake tampering (no validation)

---

## Testing Status

- **Implemented Tests**: None
- **Compiled Successfully**: Unknown (build environment CUDA mismatch)
- **Runtime Tested**: No
- **Security Audit**: This assessment

---

## Severity Classification

| Issue | Severity | Scope | Effort |
|-------|----------|-------|--------|
| Missing GCM tags | CRITICAL | All connections | Medium |
| Weak KDF | HIGH | Key security | Medium |
| Weak RNG | MEDIUM | Nonce generation | Low |
| Incomplete handshake | MEDIUM | Initial connection | Low |
| No PFS | LOW | Long-term security | High |

---

## Files Affected

- `engine/src/runtime/GpuServer.h` - TLS session struct, method declarations
- `engine/src/runtime/GpuServer.cpp` - TLS implementation (tlsDeriveKeys, tlsEncryptRecord, tlsDecryptRecord, handleTlsClientHello)
- `engine/src/protect/Aes256.h` - CTR-only, needs GCM extension
- `engine/src/runtime/ServerOptions` - tlsPsk configuration field

---

## Next Steps

1. **Immediate** (if using for internal testing only):
   - Add integration tests to verify encrypt/decrypt round-trips
   - Document that this is NOT secure TLS
   - Disable by default (require explicit opt-in)

2. **Short-term** (for staging/demo):
   - Add GCM authentication to Aes256.h
   - Implement HMAC-SHA256 for KDF
   - Use secure random source

3. **Long-term** (for production):
   - Replace with industry-standard TLS library (OpenSSL)
   - Remove custom crypto implementation
