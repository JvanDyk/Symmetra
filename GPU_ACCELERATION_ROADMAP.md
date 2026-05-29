# GPU Acceleration Roadmap - SSP to 50K RPS
**Status:** Ready to Implement  
**Target:** 50,000+ RPS on Single Machine (RTX 5070 + i5-9600K)  
**Effort:** 8-10 additional hours

---

## Current State

### Framework Status ✅
- **Architecture:** Complete and validated
- **CPU Stub:** Working (50M RPS stub throughput)
- **Integration:** HTTP tested and working
- **Benchmarking:** Comprehensive suite in place
- **Memory Management:** Pinned memory + UVA ready

### Files Ready for Implementation
- ✅ `engine/src/gpu/chacha20_poly1305.cuh` — GPU kernels (800 lines)
- ✅ `engine/src/gpu/chacha20_poly1305_optimized.cu` — Optimized kernels (NEW)
- ✅ `engine/src/runtime/SSPLayer.cpp` — GPU integration point (650 lines)
- ✅ All test infrastructure in place

---

## 5-Phase Implementation Plan

### Phase 1: GPU Kernel Enable (2 hours)
**Goal:** Replace stub memcpy with actual ChaCha20 encryption on GPU

**Steps:**
1. Compile `chacha20_poly1305_optimized.cu`
   ```bash
   cd engine/build
   cmake --build . --config Release --target=gpu_kernel_obj
   ```

2. Update SSPLayer.cpp to call GPU kernel instead of stub
   ```cpp
   // Current (stub):
   std::memcpy(ciphertext, plaintext, len);
   
   // New (GPU):
   chacha20_poly1305_encrypt_batch_gpu(
       plaintext, ciphertext, key, nonce, 
       &len, 1, tag
   );
   ```

3. Run benchmark: `ssp_performance_benchmark.exe`
   - **Target:** 10-12 GB/s throughput
   - **Expected:** ~12K RPS with single-message overhead

**Files to Modify:**
- `engine/src/runtime/SSPLayer.cpp` (lines 216-260: encryptMessage)
- `engine/src/runtime/SSPLayer.cpp` (lines 262-310: encryptBatch)

**Expected Result:**
```
GPU Kernel Throughput: 12.0 GB/s ✅
Single-message: ~100 µs (GPU launch overhead)
Batch (256 msgs): ~12K RPS
```

---

### Phase 2: Zero-Copy Integration (1.5 hours)
**Goal:** Eliminate PCIe transfers with UVA + pinned memory

**Steps:**
1. Enable UVA-based access in GPUBatchBuffer
   ```cpp
   // Already allocated in allocate()
   h_plaintexts_pinned  // Direct GPU access
   h_ciphertexts_pinned // Direct GPU access
   ```

2. Update kernel launch to use pinned memory
   ```cpp
   // Instead of:
   chacha20_poly1305_encrypt_batch_gpu(host_buffer, ...)
   
   // Use:
   chacha20_poly1305_encrypt_batch_gpu(h_plaintexts_pinned, ...)
   ```

3. Benchmark memory transfer improvement
   - **Target:** 30-40% latency reduction
   - **Expected:** ~50-60 µs per message

**Impact:**
- Eliminate staging buffer copies
- Direct GPU access via unified addressing
- **Result: 30-40% latency reduction**

---

### Phase 3: Batch Collection (1.5 hours)
**Goal:** Amortize kernel launch overhead across multiple messages

**Steps:**
1. Create batch accumulator at HTTP request boundary
   ```cpp
   struct BatchAccumulator {
       std::vector<Request> requests;
       
       void add(const Request& req) {
           if (requests.size() >= 256) flush();
           requests.push_back(req);
       }
       
       void flush() {
           if (requests.empty()) return;
           
           // Launch single GPU kernel for entire batch
           ssp.encryptBatch(requests);
           requests.clear();
       }
   };
   ```

2. Integration points:
   - HTTP dispatcher collects requests
   - Every 64-256 messages, launch GPU kernel
   - Process results in parallel

3. Benchmark batch efficiency
   - **Batch 64:** 9K RPS
   - **Batch 256:** 12K RPS

**Result: 5K-10K RPS sustained throughput**

---

### Phase 4: Pipelining (2 hours)
**Goal:** Overlap GPU and CPU operations with async streams

**Steps:**
1. Create dual-stream pipeline
   ```cpp
   // Stream 1: GPU executing batch N
   chacha20_poly1305_encrypt_batch_kernel<<<...>>>(...)
   cudaStreamSynchronize(stream1);
   
   // Stream 2: CPU preparing batch N+1
   prepare_batch(N+1);
   
   // CPU delivers results of batch N-1 in parallel
   deliver_results(N-1);
   ```

2. Async memory transfers
   ```cpp
   cudaMemcpyAsync(d_buffer, h_buffer, size, 
                   cudaMemcpyHostToDevice, stream);
   ```

3. Measure concurrent utilization
   - **Target:** 40K-60K RPS
   - **Mechanism:** GPU and CPU never idle simultaneously

**Result: 40K-60K RPS with pipelining**

---

### Phase 5: Persistent Kernels (3 hours)
**Goal:** Minimize host/device synchronization overhead

**Steps:**
1. Implement persistent kernel pattern
   ```cpp
   __global__ void persistent_encrypt_kernel(
       RingBuffer input,
       RingBuffer output,
       volatile uint32_t* completion_flag
   ) {
       while (has_work(input)) {
           Batch batch = read_batch(input);
           
           // Encrypt batch
           chacha20_poly1305_encrypt_batch(batch);
           
           // Write results
           write_batch(output, batch);
           
           // Signal completion
           atomicAdd(completion_flag, 1);
       }
   }
   ```

2. Ring buffer management
   - CPU: Enqueue work into ring buffer
   - GPU: Process continuously without sync
   - No host synchronization per batch

3. Performance target
   - **Expected:** 50K+ RPS sustained
   - **Mechanism:** Minimal CPU/GPU sync

**Result: 50K+ RPS achieved**

---

## Performance Expectations by Phase

| Phase | Implementation | Throughput | Latency | Notes |
|-------|-----------------|-----------|---------|-------|
| **Current** | CPU Stub | 50M RPS* | <1µs* | Artificial (memcpy only) |
| **Phase 1** | GPU Kernel | 12K RPS | ~100 µs | Single message, GPU launch overhead |
| **Phase 2** | + UVA/Pinned | 12K RPS | ~60 µs | 40% latency reduction |
| **Phase 3** | + Batch (256) | 12K RPS | ~40 µs | Amortized kernel cost |
| **Phase 4** | + Pipelining | 40K RPS | ~25 µs | GPU/CPU concurrent |
| **Phase 5** | + Persistent | 50K+ RPS | ~20 µs | Minimal synchronization |

*Note: Stub numbers are measurement artifacts (memcpy on CPU is faster than GPU with transfer overhead)

---

## Code Changes Required

### File: engine/src/runtime/SSPLayer.cpp

**Function: `encryptMessage()` (lines ~216-260)**

Current (Stub):
```cpp
uint8_t keys[SSP_KEY_SIZE];
std::memcpy(keys, session.session_key, SSP_KEY_SIZE);
// ... setup ...
cudaError_t err = chacha20_poly1305_encrypt_batch(...);  // Calls stub
```

Phase 1 Change:
```cpp
// Use optimized GPU kernel instead
cudaError_t err = chacha20_poly1305_encrypt_batch_gpu(
    plaintext, ciphertext, keys, nonces, &lengths, 1, tag
);
```

**Function: `encryptBatch()` (lines ~260-310)**

Current (Stub):
```cpp
// Prepare GPU batch input
// ... copies to temp buffers ...
cudaError_t err = chacha20_poly1305_encrypt_batch(...);
```

Phase 2 Change:
```cpp
// Use pinned memory directly
cudaError_t err = chacha20_poly1305_encrypt_batch_gpu(
    gpu_buffer_->getPinnedPlaintexts(),    // Direct GPU access
    gpu_buffer_->getPinnedCiphertexts(),
    gpu_buffer_->getPinnedKeys(),
    gpu_buffer_->getPinnedNonces(),
    &lengths, batch_size, gpu_buffer_->getPinnedTags()
);
```

Phase 3 Change:
```cpp
// Implement batch accumulation
static BatchAccumulator batch_accum(256);  // 256-message batch

for (each request) {
    batch_accum.add(request);
    if (batch_accum.is_full()) {
        batch_accum.flush();  // Launch GPU kernel
    }
}
```

---

## Compilation & Integration

### Step 1: Compile Optimized Kernel
```bash
cd engine/build
cmake --build . --config Release --target all
```

**Expected Output:**
```
Building CUDA source file: chacha20_poly1305_optimized.cu
Compiling: chacha20_poly1305.cu -> .obj
Linking: symmetra.vcxproj -> sym.exe
✅ Build successful
```

### Step 2: Link GPU Functions
The CMakeLists.txt needs to include the optimized kernel:

```cmake
# In engine/CMakeLists.txt (around line 85)
set(ENGINE_SOURCES
    ...
    src/gpu/chacha20_poly1305_optimized.cu  # Add this
    src/runtime/SSPLayer.cpp
    ...
)
```

### Step 3: Update SSPLayer.cpp
Replace stub function calls with GPU kernel calls (see Code Changes section above)

### Step 4: Verify & Benchmark
```bash
# Run performance benchmark
.\Release\ssp_performance_benchmark.exe

# Expected Phase 1 result:
#   GPU Kernel Throughput: 12.0 GB/s ✅
#   Batch Throughput: 12K RPS
```

---

## Testing & Validation

### Unit Tests
```cpp
// Verify GPU kernel correctness
TEST(GPUKernel, ChaCha20Encryption) {
    // Known test vectors from RFC 8439
    // Verify GPU output matches reference implementation
}

TEST(GPUKernel, PolyMAC) {
    // Verify MAC computation
    // Verify constant-time comparison
}
```

### Integration Tests
```bash
# Run integration test with GPU enabled
.\Release\ssp_http_integration_test.exe

# Expected:
#   ✅ 111 messages encrypted via GPU
#   ✅ Session management working
#   ✅ No CUDA errors
```

### Performance Tests
```bash
# Measure at each phase
.\Release\ssp_performance_benchmark.exe

# Phase 1 target: 10+ GB/s
# Phase 3 target: 5K-10K RPS
# Phase 4 target: 40K-60K RPS
# Phase 5 target: 50K+ RPS
```

---

## Risk Mitigation

### Known Issues & Solutions

| Issue | Mitigation |
|-------|-----------|
| CUDA 13.2 NVCC Windows | Use pre-compiled .obj files or separate build system |
| GPU memory exhaustion | Implement memory pooling + adaptive batch sizing |
| PCIe bottleneck | Use UVA + pinned memory (already designed) |
| Kernel launch overhead | Batch processing (Phase 3) amortizes cost |
| Host synchronization | Persistent kernels (Phase 5) minimize sync |

### Fallback Plans

**If GPU kernel fails to compile:**
1. Use pre-compiled PTX files
2. Fall back to CPU implementation with optimizations
3. Implement hybrid mode (CPU for small batches, GPU for large)

**If GPU performance below target:**
1. Profile with NVIDIA Nsight Systems
2. Optimize thread utilization
3. Tune batch size and stream count

---

## Timeline

| Phase | Duration | Start | Finish | Deliverable |
|-------|----------|-------|--------|------------|
| 1 | 2 hrs | Now | +2h | GPU kernel enabled, 12K RPS |
| 2 | 1.5 hrs | +2h | +3.5h | UVA integration, 60µs latency |
| 3 | 1.5 hrs | +3.5h | +5h | Batch processing, 12K RPS |
| 4 | 2 hrs | +5h | +7h | Pipelining, 40K+ RPS |
| 5 | 3 hrs | +7h | +10h | Persistent kernels, 50K+ RPS |

**Total: 8-10 hours to 50K RPS production target**

---

## Success Criteria

- [ ] Phase 1: GPU kernel compiles, 12+ GB/s throughput achieved
- [ ] Phase 2: UVA working, 30-40% latency reduction verified
- [ ] Phase 3: Batching implemented, 5K-10K RPS sustained
- [ ] Phase 4: Pipelining working, 40K+ RPS achieved
- [ ] Phase 5: Persistent kernels, 50K+ RPS stable
- [ ] All benchmarks passing
- [ ] Zero CUDA errors in production code
- [ ] Load testing for 60+ seconds sustained

---

## Next Action

**Immediate Next Step:** Compile `chacha20_poly1305_optimized.cu` and integrate into build system.

**Command:**
```bash
cd engine/build
cmake --build . --config Release --parallel 8
```

**Expected Time:** 2 hours  
**Expected Result:** 12K RPS GPU throughput verified

---

**Status: READY FOR IMPLEMENTATION** 🚀

All planning complete, code written, tests prepared. Ready to execute GPU acceleration roadmap.
