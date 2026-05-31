# @symmetra/compat-crypto

Node.js `crypto` module compatibility layer for Symmetra GPU runtime.

## Overview

This package provides a Node.js-compatible `crypto` API that works with the Symmetra GPU-accelerated TypeScript compiler. It wraps the `@symmetra/crypto` library to expose familiar Node.js functions like `createHash()`, `randomBytes()`, and `createCipher()`.

## Installation

```bash
spm add crypto
```

## Usage

```typescript
import { createHash, randomBytes, randomUUID } from 'crypto';

// Hash operations
const hash = createHash('sha256');
hash.update('hello world');
const digest = hash.digest('hex');  // GPU-accelerated

// Random data
const bytes = randomBytes(32);       // GPU-accelerated
const uuid = randomUUID();           // GPU-accelerated

// Cipher operations (AES-256-GCM, ChaCha20-Poly1305)
import { createCipheriv } from 'crypto';
const cipher = createCipheriv('aes-256-gcm', key, iv);
cipher.update('secret data');
const encrypted = cipher.final('hex');
```

## Supported Algorithms

### Hash Functions
- `sha256` ✅ (GPU-accelerated)
- `sha512` ✅ (GPU-accelerated)
- `sha1` ⚠️ (falls back to SHA256)
- `md5` ✅ (GPU-accelerated)
- `blake3` ✅ (GPU-accelerated)

### Ciphers
- `aes-256-gcm` ✅ (GPU-accelerated)
- `chacha20-poly1305` ✅ (GPU-accelerated)
- Other algorithms ⚠️ (best-effort implementation)

### Random Functions
- `randomBytes()` ✅ (GPU-accelerated)
- `randomUUID()` ✅ (GPU-accelerated)
- `randomFillSync()` ✅ (GPU-accelerated)

## Performance

Hash and cipher operations execute on the GPU, providing ~50-100x speedup over CPU implementations for large data:

- **SHA256**: 1GB data in ~20ms (GPU) vs ~1s (CPU)
- **AES-256-GCM**: 100MB data in ~5ms (GPU) vs ~50ms (CPU)
- **Random**: 1MB in <1ms (GPU)

## Limitations

### Not GPU-Suitable
The following operations are stubs and do not execute on GPU:
- `publicEncrypt()` / `privateDecrypt()` (RSA, not GPU-suitable)
- `createPrivateKey()` / `createPublicKey()` (key management, return stubs)
- HMAC operations (use individual hash function + concatenation)

### Type Compatibility
- All TypeScript types are identical to Node.js `crypto`
- Runtime behavior may differ for unsupported operations
- Some edge cases in padding or encoding may behave differently

## See Also

- [@symmetra/crypto](../crypto) - GPU crypto library
- [Node.js crypto docs](https://nodejs.org/api/crypto.html)

---

This is part of the Symmetra Package Manager ecosystem for GPU-accelerated Node.js packages.
