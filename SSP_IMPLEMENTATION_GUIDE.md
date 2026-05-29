# Symmetra Security Protocol (SSP) - Implementation Guide

**Status:** Framework ready, implementation in progress  
**Timeline:** 5 days to 50K RPS encryption  
**Current Progress:** GPU kernel template + SSP layer header created

---

## What's Been Created

### 1. **GPU ChaCha20-Poly1305 Kernel** (`src/gpu/chacha20_poly1305.cu`)

**Status:** Template complete, implementation needed

```
What's there:
  ✅ ChaCha20 quarter-round implementation
  ✅ ChaCha20 block function
  ✅ ChaCha20 encryption/decryption kernel skeleton
  ✅ Poly1305 MAC skeleton
  ✅ Batch encrypt kernel signature
  ✅ Batch decrypt kernel signature
  ✅ Host API stubs
  
What you need to implement:
  - Complete Poly1305 field arithmetic (GF(2^130-5))
  - Host wrapper functions (malloc GPU, copy, launch, sync)
  - Benchmark function
  - Error handling
  
Effort: 4-6 hours
```

### 2. **SSP Layer Header** (`src/runtime/SSPLayer.h`)

**Status:** Interface defined, implementation needed

```
What's there:
  ✅ GPU buffer management class
  ✅ Session management struct
  ✅ SSP protocol header definition
  ✅ SSP layer main class with all methods
  ✅ Batch encryption/decryption interface
  
What you need to implement:
  - src/runtime/SSPLayer.cpp (all methods)
  - ECDHE handshake (or use OpenSSL ECDHE)
  - Session key derivation
  - Per-message encryption (single + batch)
  
Effort: 3-4 hours
```

---

## 5-Day Implementation Plan

### Day 1: GPU Kernel Completion (4-6 hours)

**Objective:** Get GPU ChaCha20-Poly1305 working

```cpp
// 1. Complete Poly1305 MAC
//    - Implement GF(2^130-5) field arithmetic
//    - References: RFC 8439, libsodium poly1305 source
//    - Target: 1-2 hours

// 2. Implement host wrapper
void cudaError_t chacha20_poly1305_encrypt_batch(
    const uint8_t* h_plaintexts,    // Host memory
    uint8_t* h_ciphertexts,
    const uint8_t* h_keys,
    const uint8_t* h_nonces,
    const uint32_t* h_lengths,
    uint32_t batch_size,
    uint8_t* h_tags
) {
    // Allocate device memory
    cudaMalloc(&d_plaintexts, ...);
    
    // Copy to GPU
    cudaMemcpyAsync(d_plaintexts, h_plaintexts, ...);
    
    // Launch kernel
    int blocks = batch_size;  // One message per block
    int threads = 256;        // Threads cooperate per message
    chacha20_poly1305_encrypt_batch_kernel<<<blocks, threads>>>(
        d_plaintexts, d_ciphertexts, d_keys, d_nonces, d_lengths,
        batch_size, d_tags
    );
    
    // Copy results back
    cudaMemcpyAsync(h_ciphertexts, d_ciphertexts, ...);
    
    // Free GPU memory
    cudaFree(d_plaintexts);
    
    return cudaGetLastError();
}
    
//    Target: 2 hours

// 3. Benchmark function
float chacha20_poly1305_benchmark(uint32_t batch_size, uint32_t msg_size) {
    // Allocate test data
    uint8_t* plaintext = malloc(batch_size * msg_size);
    
    // Initialize GPU buffers
    
    // Run kernel 100 times, measure time
    auto start = high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        chacha20_poly1305_encrypt_batch(...);
    }
    cudaDeviceSynchronize();
    auto end = high_resolution_clock::now();
    
    // Calculate throughput: (bytes processed) / time
    float time_sec = duration<float>(end - start).count();
    float throughput_gbs = (batch_size * msg_size * 100) / (1e9 * time_sec);
    
    return throughput_gbs;  // Target: 10+ GB/s
}

//    Target: 1-2 hours
```

**Success Criteria:**
- ✅ Kernel compiles without errors
- ✅ Batch benchmark: 10+ GB/s throughput
- ✅ MAC verification: 100% accuracy

---

### Day 2: SSP Layer Implementation (3-4 hours)

**Objective:** Create src/runtime/SSPLayer.cpp

```cpp
// 1. ECDHE Handshake
bool SSPLayer::establishSession(void* client_fd, SSPSession& session) {
    // Option A: Use OpenSSL ECDHE
    //   EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X25519);
    //   EC_KEY_generate_key(ecdh);
    
    // Option B: Simple X25519 (25519 is simpler than full TLS)
    //   uint8_t client_key[32], server_key[32];
    //   x25519_keypair(server_key, server_secret);
    
    // Exchange keys with client
    // Compute shared secret via ECDHE
    // Derive session key via HKDF
    
    // Store in session struct
    memcpy(session.session_key, derived_key, SSP_KEY_SIZE);
    
    return true;
}

// 2. Single-message encryption (used by HTTP dispatcher)
bool SSPLayer::encryptMessage(
    const SSPSession& session,
    const uint8_t* plaintext, uint32_t plaintext_len,
    uint8_t* ciphertext, uint32_t& ciphertext_len,
    uint8_t* tag
) {
    // For single messages, queue them up for batching
    // Or encrypt immediately with batch size 1
    
    // Call GPU kernel with batch_size=1
    std::vector<const uint8_t*> plaintexts = {plaintext};
    std::vector<uint32_t> lengths = {plaintext_len};
    std::vector<uint8_t*> ciphertexts = {ciphertext};
    std::vector<uint8_t*> tags = {tag};
    
    return encryptBatch({session}, plaintexts, lengths, 
                       ciphertexts, {&ciphertext_len}, tags);
}

// 3. Batch encryption (main path for high throughput)
bool SSPLayer::encryptBatch(
    const std::vector<SSPSession>& sessions,
    const std::vector<const uint8_t*>& plaintexts,
    const std::vector<uint32_t>& plaintext_lengths,
    std::vector<uint8_t*>& ciphertexts,
    std::vector<uint32_t>& ciphertext_lengths,
    std::vector<uint8_t*>& tags
) {
    uint32_t batch_size = sessions.size();
    if (batch_size > SSP_MAX_BATCH_SIZE) return false;
    
    // Prepare GPU batch:
    // 1. Copy plaintexts to GPU buffer
    for (int i = 0; i < batch_size; i++) {
        gpu_buffer_->getPlaintexts()[i * SSP_MAX_MESSAGE_SIZE] = plaintexts[i];
    }
    
    // 2. Copy keys and nonces to GPU buffer
    for (int i = 0; i < batch_size; i++) {
        memcpy(gpu_buffer_->getKeys() + i * SSP_KEY_SIZE,
               sessions[i].session_key, SSP_KEY_SIZE);
        
        // Generate nonce (counter-based or random)
        generateNonce(gpu_buffer_->getNonces() + i * SSP_NONCE_SIZE,
                     sessions[i].sequence_number);
    }
    
    // 3. Launch GPU kernel
    chacha20_poly1305_encrypt_batch(
        gpu_buffer_->getPlaintexts(),
        gpu_buffer_->getCiphertexts(),
        gpu_buffer_->getKeys(),
        gpu_buffer_->getNonces(),
        gpu_buffer_->getLengths(),
        batch_size,
        gpu_buffer_->getTags()
    );
    
    // 4. Copy results back to host
    gpu_buffer_->synchronize();
    for (int i = 0; i < batch_size; i++) {
        memcpy(ciphertexts[i], 
               gpu_buffer_->getCiphertexts() + i * SSP_MAX_MESSAGE_SIZE,
               plaintext_lengths[i]);
        ciphertext_lengths[i] = plaintext_lengths[i];
        
        memcpy(tags[i],
               gpu_buffer_->getTags() + i * SSP_TAG_SIZE,
               SSP_TAG_SIZE);
    }
    
    // Update metrics
    total_messages_encrypted += batch_size;
    for (auto len : plaintext_lengths) {
        total_bytes_encrypted += len;
    }
    
    return true;
}
```

**Success Criteria:**
- ✅ Class compiles
- ✅ Session establishment works (ECDHE or pre-shared key)
- ✅ Single message encryption works
- ✅ Batch encryption works

---

### Day 3: Zero-Copy Optimization (2-3 hours)

**Objective:** Minimize PCIe overhead

```cpp
// Instead of copying message to GPU buffer:
// Let GPU kernel read directly from device memory
// (Using CUDA Unified Virtual Addressing)

// Allocate pinned host memory (can be accessed by GPU)
cudaHostAlloc(&pinned_buffer, size, cudaHostAllocWriteCombined);

// GPU accesses directly without copy overhead
// Or: Copy entire request batch to GPU once,
//     Let GPU process, copy results back

// Optimize: Ring buffer approach
//   CPU writes to buffer[0], GPU reads from buffer[1] concurrently
//   No synchronization overhead
```

---

### Day 4: Integration & Testing (2-3 hours)

**Objective:** Connect SSP to Symmetra dispatcher

```cpp
// Modify Phase 8 HTTP server to use SSP

// In TLS layer:
#include "runtime/SSPLayer.h"

// On connection init:
SSPLayer ssp;
ssp.initialize();
SSPSession session;
ssp.establishSession(client_fd, session);

// On request receive:
uint8_t encrypted_request[8192];
uint8_t tag[16];
// ... read encrypted_request from TCP ...
uint8_t decrypted[8192];
ssp.decryptMessage(session, encrypted_request, ..., decrypted, ...);

// On response send:
uint8_t plaintext_response[8192];
uint8_t encrypted_response[8192];
uint8_t response_tag[16];
// ... build plaintext_response ...
ssp.encryptMessage(session, plaintext_response, ..., 
                   encrypted_response, ..., response_tag);
// ... send encrypted_response + response_tag via TCP ...
```

---

### Day 5: Benchmarking & Optimization (2-3 hours)

**Objective:** Reach 50K RPS

```
Measure:
  1. GPU kernel throughput: target 10+ GB/s ✓
  2. Single message latency: target <500 µs
  3. Batch throughput: target 5K-10K RPS (batch of 64)
  4. Overall with HTTP: target 40K-60K RPS

Profile:
  - Where is time spent? (CPU vs GPU vs I/O)
  - Are all GPU blocks utilized?
  - Is PCIe bandwidth a bottleneck?
  - Can we increase batch size?

Optimize if needed:
  - Kernel tweaks (if <10 GB/s)
  - Batch size tuning
  - Memory access patterns
  - Stream management
```

---

## CMakeLists.txt Changes Required

Add to `engine/CMakeLists.txt`:

```cmake
# Enable CUDA support for GPU encryption
enable_language(CUDA)

# Add GPU source files
list(APPEND ENGINE_SOURCES
    src/gpu/chacha20_poly1305.cu
    src/runtime/SSPLayer.cpp
)

# Link CUDA runtime
target_link_libraries(symmetra PRIVATE cuda)

# Compile with appropriate CUDA arch (RTX 5070 = compute 12.0)
set_property(SOURCE src/gpu/chacha20_poly1305.cu 
             PROPERTY LANGUAGE CUDA)
set_property(TARGET symmetra 
             PROPERTY CUDA_ARCHITECTURES 75 86 87 89 90)
```

---

## Files to Create

| File | Effort | Status |
|------|--------|--------|
| `src/gpu/chacha20_poly1305.cu` | 4-6h | Template ready, needs Poly1305 + host wrapper |
| `src/runtime/SSPLayer.cpp` | 3-4h | Header ready, needs implementation |
| `src/runtime/SSPLayer.h` | ✅ | Done |
| `test_ssp_performance.cpp` | 1-2h | Simple benchmark test |
| CMakeLists.txt | 0.5h | Add CUDA config |

**Total:** ~15 hours of focused development for 50K RPS

---

## Testing Plan

### Phase 1: Kernel Validation
```bash
./test_ssp_performance --benchmark
Expected: 10+ GB/s
```

### Phase 2: End-to-End
```bash
# Run modified Phase 8 with SSP instead of OpenSSL
./phase8_http_server_ssp_test --workers 256 --duration 3
Expected: 40K-60K RPS
```

### Phase 3: Stress Test
```bash
# Load test with socket_io_bottleneck_test modified for SSP
./socket_io_bottleneck_test_ssp 30 9000
Expected: 50K+ RPS sustained
```

---

## Success Metrics

| Metric | Target | Status |
|--------|--------|--------|
| GPU ChaCha20 throughput | 10+ GB/s | TBD |
| Single message latency | <500 µs | TBD |
| Batch throughput | 5K-10K RPS | TBD |
| HTTP with encryption | 40K-60K RPS | TBD |

---

## Next Immediate Steps

1. ✅ **Create GPU kernel template** (DONE)
2. ✅ **Create SSP layer header** (DONE)
3. **Complete Poly1305 MAC implementation** (Start Day 1)
4. **Write host GPU wrapper** (Start Day 1)
5. **Implement SSPLayer.cpp** (Start Day 2)

---

## Resources

### Poly1305 Implementation Reference
- RFC 8439: https://tools.ietf.org/html/rfc8439
- libsodium poly1305: https://github.com/jedisct1/libsodium/blob/master/src/libsodium/crypto_onetimeauth/poly1305/ref/poly1305-ref.c
- NVIDIA crypto samples: https://developer.nvidia.com/cuda-toolkit-archive

### ChaCha20-Poly1305 CUDA Implementations
- https://github.com/kudelskisecurity/easy-ecc/blob/master/GPU_ECC/chacha20.cu
- NVIDIA cuDNN crypto examples

### X25519 (if implementing ECDHE)
- RFC 7748: https://tools.ietf.org/html/rfc7748
- libsodium x25519: https://github.com/jedisct1/libsodium

---

**This is a realistic, achievable plan to build custom high-performance encryption on your RTX 5070.**

**50K RPS single machine = futureistic! 🚀**
