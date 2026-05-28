# CUDA Callback Implementation Guide

**Objective:** Remove `cuStreamSynchronize()` blocking and achieve 100+ req/s  
**Estimated Effort:** 2-3 hours  
**Risk Level:** Medium (CUDA callbacks are low-level and error-prone)

---

## High-Level Architecture

### Current (Blocking):
```
Scheduler Thread          GPU Stream              I/O Thread Pool
     |                       |                        |
     |-- launch kernel ----->|                        |
     |                       |[executing]             |
     |-- sync (BLOCK) ----X  |                        |
     |  (240ms wait)         |[idle]                  |
     |                       |                        |
     |-- memcopy results --->|                        |
     |   (BLOCK)             |[idle]                  |
     |                       |                        |
     |-- queue response -----+--------> (async send)
     |                       |                        |
     | return                |                        |
     |                       |                        |
```

### Target (Async):
```
Scheduler Thread          GPU Stream              Callback/I/O Pool
     |                       |                        |
     |-- launch kernel ----->|                        |
     |-- async memcpy ------>|                        |
     |-- register callback -->|                        |
     |  return (non-block)    |                        |
     |                       |[executing]             |
     |-- collect batch 2 --->| (parallel)             |
     |-- collect batch 3 --->|                        |
     |                       |[idle]                  |
     |                       |-- fire callback ------>|
     |                       |                        |-- queue response send
     |                       |                        |-- async send responses
```

---

## Step 1: Understand CUDA Stream Callbacks

### Callback Function Signature
```cpp
// CUDA signature: void (*callback)(CUstream stream, CUresult status, void *userData)
static void CUDAAPI gpuResultsReady(CUstream stream, CUresult status, void* userData) {
    // This runs on a worker thread managed by CUDA driver
    // Called when all operations on stream are complete
    // status == CUDA_SUCCESS if no GPU errors
    
    auto* context = static_cast<ResultContext*>(userData);
    // Process results and send responses
}
```

### Key Properties:
- **Called when:** All queued operations on stream complete
- **Runs on:** CUDA driver's internal thread (NOT the calling thread)
- **Must be fast:** Don't do expensive work in callback
- **Must be thread-safe:** May be called while other threads are submitting to same stream

---

## Step 2: Create Result Context Structure

**File:** `engine/src/runtime/BatchContext.h` (add near struct definition)

```cpp
struct AsyncResultContext {
    // Input batch info
    std::vector<BatchRequest> requests;  // Keep requests alive until response sends
    int batch_id;
    int count;
    
    // GPU result buffers (persistent - stay valid until callback completes)
    std::vector<unsigned int> h_resp_lens;     // Response body lengths
    std::vector<int>          h_resp_statuses; // HTTP status codes
    std::vector<std::string>  h_resp_bodies;   // Response bodies
    std::vector<std::string>  h_resp_headers;  // Custom headers
    
    // Callback to invoke when download completes
    std::function<void(const AsyncResultContext&)> onComplete;
    
    // For cleanup
    void* owner;  // Pointer back to GpuServer for cleanup
};
```

---

## Step 3: Modify BatchContext to Support Async Download

**File:** `engine/src/runtime/BatchContext.h` (add method)

```cpp
// Add this method to the BatchContext struct:

CUresult downloadResultsAsync(
    const std::vector<BatchRequest>& requests,
    int batch_id,
    CUstream stream,
    std::function<void(const AsyncResultContext&)> onComplete
) {
    // Create persistent context that will be freed by callback
    auto* ctx = new AsyncResultContext{
        .requests = requests,
        .batch_id = batch_id,
        .count = actualCount,
        .onComplete = onComplete
    };
    
    // Pre-allocate host buffers
    ctx->h_resp_lens.resize(actualCount);
    ctx->h_resp_statuses.resize(actualCount);
    ctx->h_resp_bodies.resize(actualCount);
    ctx->h_resp_headers.resize(actualCount);
    
    // Queue async memcopy of lens and statuses (small, fast)
    CUresult r1 = cuMemcpyDtoHAsync(
        ctx->h_resp_lens.data(),
        (CUdeviceptr)d_resp_lens,
        actualCount * sizeof(unsigned int),
        stream
    );
    
    CUresult r2 = cuMemcpyDtoHAsync(
        ctx->h_resp_statuses.data(),
        (CUdeviceptr)d_statuses,
        actualCount * sizeof(int),
        stream
    );
    
    if (r1 != CUDA_SUCCESS || r2 != CUDA_SUCCESS) {
        delete ctx;
        return r1 != CUDA_SUCCESS ? r1 : r2;
    }
    
    // Queue response body downloads (this will queue a LOT of data for large batches)
    // You MIGHT want to defer this until the callback, but the pattern is:
    for (int i = 0; i < actualCount; i++) {
        unsigned int bodyLen = 0;  // We'll get this from h_resp_lens after sync
        // Can't know bodyLen yet, so queue a small fixed amount
        // OR queue it in the callback after we have lens values
        // For now, skip it - we'll handle in callback
    }
    
    // Register callback to fire when stream is idle
    CUresult r3 = cuStreamAddCallback(stream, asyncDownloadCallback, ctx, 0);
    if (r3 != CUDA_SUCCESS) {
        delete ctx;
        return r3;
    }
    
    return CUDA_SUCCESS;
}
```

---

## Step 4: Implement Callback Function

**File:** `engine/src/runtime/BatchContext.h` or new file (add near top)

```cpp
static void CUDAAPI asyncDownloadCallback(CUstream stream, CUresult status, void* userData) {
    if (!userData) return;
    
    auto* ctx = static_cast<AsyncResultContext*>(userData);
    
    if (status != CUDA_SUCCESS) {
        // GPU error - fill with error responses
        for (auto& s : ctx->h_resp_statuses) s = 500;
        for (auto& b : ctx->h_resp_bodies) 
            b = "{\"error\":\"GPU kernel failed\"}";
    } else {
        // GPU succeeded - now download response bodies
        // This is still blocking, but happens in CUDA driver thread, not scheduler
        // So scheduler can continue queueing batches meanwhile
        
        int n = ctx->count;
        for (int i = 0; i < n; i++) {
            unsigned int bodyLen = ctx->h_resp_lens[i];
            bodyLen = std::min(bodyLen, (unsigned int)BatchContext::RESP_HDR_OFFSET);
            
            if (bodyLen > 0) {
                ctx->h_resp_bodies[i].resize(bodyLen);
                
                // This is still synchronous but: 
                // 1. Happens on CUDA internal thread (not scheduler)
                // 2. Overlaps with scheduler processing next batch
                // 3. Multiple callbacks can run in parallel for different streams
                cuMemcpyDtoH(
                    ctx->h_resp_bodies[i].data(),
                    (CUdeviceptr)(d_respBuf + (size_t)i * MAX_RESP_BYTES),
                    bodyLen
                );
            }
            
            // Download custom headers
            char h_hdrs[256] = {};
            cuMemcpyDtoH(
                h_hdrs,
                (CUdeviceptr)(d_respBuf + (size_t)i * MAX_RESP_BYTES + 
                            BatchContext::RESP_HDR_OFFSET),
                256
            );
            if (h_hdrs[0] != '\0') {
                ctx->h_resp_headers[i] = h_hdrs;
            }
        }
    }
    
    // Now invoke the completion callback
    // This should queue response sending to I/O thread pool
    if (ctx->onComplete) {
        ctx->onComplete(*ctx);
    }
    
    // Cleanup
    delete ctx;
}
```

---

## Step 5: Update GpuServer::batchLaunch

**File:** `engine/src/runtime/GpuServer.cpp` (replace current implementation)

Change from:
```cpp
// Old: blocking download
std::vector<unsigned int> lens;
bctx.downloadResults(lens, result.statuses, result.responses, 
                    result.respHeaders, slot.stream);
_streams->release(slot);
sendBatchResponses(requests, result);
```

To:
```cpp
// New: async download via callback
bctx.downloadResultsAsync(
    requests,
    batch_id,  // You may need to add a batch counter
    slot.stream,
    [this, slot](const AsyncResultContext& ctx) {
        // This lambda runs in CUDA callback thread
        // Convert context back to BatchResult format
        BatchResult result;
        result.statuses = ctx.h_resp_statuses;
        result.responses = ctx.h_resp_bodies;
        result.respHeaders = ctx.h_resp_headers;
        
        // Queue response sending to I/O pool
        _io->post([this, reqs = ctx.requests, res = result]() mutable {
            this->sendBatchResponses(reqs, res);
        });
        
        // Release stream after callback completes
        _streams->release(slot);
    }
);

// Return immediately - no blocking!
// Scheduler can now process next batch while GPU downloads + callback runs
return;
```

---

## Step 6: Testing & Validation

### Compile Test
```bash
cd D:\GITHUB\SYMMETRA\engine\build
cmake --build . --config Release 2>&1 | grep -i error
```

### Functional Test
```bash
# Start server
.\sym.exe turing_lightning.ts --port 9000 &

# Sequential requests (should still be fast)
for i in {1..10}; do curl -s -o /dev/null http://localhost:9000/; done

# Parallel requests (should batch)
parallel -j 32 'curl -s -o /dev/null http://localhost:9000/' ::: {1..32}
```

### Performance Test
```powershell
# Expected: 100-300 req/s (25-75× improvement from 4 req/s)
# Latency: <10ms per request
```

---

## Common Pitfalls & Solutions

### Pitfall 1: Context Goes Out of Scope
**Problem:** Allocating context on stack, it's freed before callback runs

**Solution:** Use `new AsyncResultContext()`, callback must `delete` it

### Pitfall 2: GPU Buffer Reuse Before Callback Fires
**Problem:** BatchContext buffers are reused for next batch while callback still needs them

**Solution:** Callback must read d_respBuf in the callback (as shown), not in main thread

### Pitfall 3: Callback Doesn't Fire
**Problem:** `cuStreamAddCallback` is queued but stream isn't idle

**Solution:** Ensure there's nothing else queued on the stream after callback registration

### Pitfall 4: Race Condition on Stream Release
**Problem:** Releasing stream in callback while scheduler tries to reuse it

**Solution:** Use atomic flag or synchronization in StreamPool to track in-use streams

### Pitfall 5: Lambda Capture Issues
**Problem:** Capturing `&slot` by reference, but it's a local variable

**Solution:** Capture by value: `[this, slot](...)` not `[this, &slot](...)`

---

## Expected Performance Gains

**Before (current):**
```
Sequential requests: 4 req/s
Parallel 32 reqs:    4-8 req/s (no real batching)
```

**After (with callbacks):**
```
Sequential requests: 100+ req/s (32-request batches)
Parallel 32 reqs:    32 req/s per batch (2-3 parallel batches)
Theoretical max:     300+ req/s (full pipeline saturation)
```

---

## Debugging Tips

### Enable CUDA Error Checking
```cpp
#define CUDA_CHECK(expr) do { \
    CUresult _r = (expr); \
    if (_r != CUDA_SUCCESS) { \
        const char* msg = nullptr; \
        cuGetErrorString(_r, &msg); \
        fprintf(stderr, "[CUDA Error] %s\n", msg ? msg : "unknown"); \
        exit(1); \
    } \
} while(0)
```

### Trace Callback Execution
```cpp
static void CUDAAPI asyncDownloadCallback(CUstream stream, CUresult status, void* userData) {
    fprintf(stderr, "[Callback] GPU status=%d, context=%p\n", status, userData);
    // ... rest of implementation
}
```

### Monitor Stream Pool
```cpp
// After callback completes
fprintf(stderr, "[Pool] Stream reuse: %d/%d in use\n", 
        _streams->inUse(), _streams->total());
```

---

## Success Criteria

- [ ] Code compiles without errors
- [ ] Sequential requests: >100 req/s (target: 100-200)
- [ ] Parallel 32 requests: complete in <250ms (5-8x faster)
- [ ] GPU stays saturated (kernel continuously running)
- [ ] No callback races or memory corruption
- [ ] Multiple batches truly in-flight simultaneously (verify with tracing)

---

## Time Breakdown

| Task | Estimated Time |
|------|----------------|
| Understand CUDA callbacks | 15-20 min |
| Implement context & callback | 20-30 min |
| Modify batchLaunch | 10-15 min |
| Fix compilation errors | 15-20 min |
| Functional testing | 10-15 min |
| Performance testing & debugging | 20-30 min |
| **Total** | **90-130 minutes** |

---

## Once This Is Complete

Once callbacks are working and performance jumps to 100+ req/s:

1. Update website benchmarks with new metrics
2. Run comprehensive test suite (706+ tests)
3. Profile with NVIDIA tools (nsys, ncu) to verify GPU utilization
4. Consider further optimizations:
   - Persistent kernels (keep GPU occupied indefinitely)
   - Higher batch sizes (256+ to increase GPU occupancy)
   - Kernel fusion (combine multiple handlers)

