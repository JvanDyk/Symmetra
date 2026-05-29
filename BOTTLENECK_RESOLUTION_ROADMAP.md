# Bottleneck Resolution Roadmap

**Status:** Socket I/O bottleneck identified and analyzed  
**Bottleneck:** TLS encryption (200x factor)  
**Current Performance:** 644 RPS single instance  
**Goal:** Determine best path forward

---

## The Situation

```
✅ Analysis complete:
   - Raw TCP baseline: 136,277 RPS
   - With TLS: 644 RPS
   - Bottleneck: TLS is CPU-bound (OpenSSL limited to 50K-100K ops/sec)
   
❌ This is expected behavior, not a bug
   
? Question: What should we do to improve throughput?
```

---

## Three Improvement Paths

### Path 1: TLS Session Reuse (QUICK WIN) ⭐ RECOMMENDED

**What:** Keep HTTP connections alive and reuse TLS sessions  
**Effort:** 30 minutes configuration + testing  
**Expected Result:** 2-3x improvement (644 → 1,500 RPS)  
**Complexity:** Low  

#### How It Works

```
Current (no Keep-Alive):
  Connection 1: TLS handshake (10 ms) + 1 request (1.5 ms) = 11.5 ms
  Connection 2: TLS handshake (10 ms) + 1 request (1.5 ms) = 11.5 ms
  Connection 3: TLS handshake (10 ms) + 1 request (1.5 ms) = 11.5 ms
  
  Time: 34.5 ms for 3 requests = 87 RPS per connection

With Keep-Alive + Session Reuse:
  Connection 1: TLS handshake (10 ms) + 100 requests (1.5 ms × 100) = 160 ms
  Time: 160 ms for 100 requests = 625 RPS per connection
  
  Benefit: Session overhead amortized over 100 requests
  Expected: 644 RPS × 2-3 = 1,300-1,900 RPS
```

#### Implementation

**HTTP Keep-Alive (already supported in Phase 8):**
```cpp
// Ensure HTTP/1.1 with Connection: keep-alive
response = "HTTP/1.1 200 OK\r\n"
           "Connection: keep-alive\r\n"  // Keep connection open
           "Content-Length: " + length + "\r\n\r\n";

// OpenSSL automatically reuses session if available
// No code changes needed - just configuration
```

**Test It:**
```cpp
// Modify Node.js client to use Keep-Alive
const agent = new https.Agent({
    keepAlive: true,  // Enable
    keepAliveMsecs: 30000,
    maxSockets: 256
});

// Now run load test with same agent
// Should see 2-3x improvement without code changes
```

#### Success Criteria
- Measure RPS with Keep-Alive enabled
- Target: 1,300-2,000 RPS (2-3x baseline)
- If achieved: ✅ Problem solved for most use cases

---

### Path 2: GPU-Accelerated TLS (IF SESSION REUSE ISN'T ENOUGH)

**What:** Offload AES-GCM encryption to CUDA kernels  
**Effort:** 3-4 hours implementation + testing  
**Expected Result:** 2-5x improvement (depends on payload size)  
**Complexity:** Medium-High  
**Risk:** GPU overhead might exceed benefit

#### Viability Check

**Only worthwhile if:**
1. Session reuse gave <1,500 RPS (not enough)
2. Average payload >2 KB (to justify GPU transfer overhead)
3. Single instance must handle >5,000 RPS (scale isn't option)

#### Implementation Strategy

```cpp
// Create CUDA kernel for AES-GCM
// Pseudocode:

__global__ void aes_gcm_encrypt_kernel(
    const unsigned char* plaintext,
    unsigned char* ciphertext,
    const unsigned char* key,
    const unsigned char* iv,
    int* lengths,
    int batch_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= batch_size) return;
    
    // Encrypt plaintext[idx] using AES-GCM
    // Store in ciphertext[idx]
    aes_gcm_single(
        plaintext + idx * MAX_SIZE,
        ciphertext + idx * MAX_SIZE,
        key,
        iv,
        lengths[idx]
    );
}

// In TLS layer:
cudaMemcpy(d_plaintext, plaintext, size, cudaMemcpyHostToDevice);
aes_gcm_encrypt_kernel<<<blocks, threads>>>(
    d_plaintext, d_ciphertext, d_key, d_iv, d_lengths, batch_size
);
cudaMemcpy(ciphertext, d_ciphertext, size, cudaMemcpyDeviceToHost);
```

#### Feasibility Concerns

**PCIe Overhead:**
```
GPU transfer overhead: ~1 µs per 1 KB of data
At 644 RPS with 512 B payloads: 2 µs overhead
Encryption time: 400 µs
Speedup: 400 ÷ 50 = 8x IF GPU encryption is 50µs
But overhead: 2 µs + latency = maybe 3-5x net
```

**Recommendation:** ⚠️ Only attempt if:
- Session reuse test shows payloads are consistently >2 KB
- Profiling shows encryption is indeed bottleneck
- Single-instance requirement is firm

---

### Path 3: Load Balancing (PRODUCTION SOLUTION) ⭐ BEST FOR SCALE

**What:** Deploy multiple instances behind Nginx  
**Effort:** 2-3 hours setup + configuration  
**Expected Result:** Linear scaling (5 instances = 3,220 RPS)  
**Complexity:** Low  
**Benefit:** Works immediately, scales indefinitely  

#### Architecture

```
                     ┌─────────────┐
                     │   Nginx     │ (Load Balancer)
                     │  Port 8080  │
                     └──────┬──────┘
                            │
             ┌──────────────┼──────────────┐
             │              │              │
      ┌──────▼────┐  ┌──────▼────┐  ┌──────▼────┐
      │ Instance 1│  │ Instance 2│  │ Instance 3│
      │   8443    │  │   8444    │  │   8445    │
      │ 644 RPS   │  │ 644 RPS   │  │ 644 RPS   │
      └───────────┘  └───────────┘  └───────────┘
      
Total: 1,932 RPS (3 × 644)
      
Scale to 5 instances: 3,220 RPS
Scale to 10 instances: 6,440 RPS
```

#### Implementation (Nginx)

**nginx.conf:**
```nginx
upstream symmetra_cluster {
    server localhost:8443;
    server localhost:8444;
    server localhost:8445;
    server localhost:8446;
    server localhost:8447;
}

server {
    listen 8080;
    
    location / {
        proxy_pass https://symmetra_cluster;
        proxy_http_version 1.1;
        proxy_set_header Connection "upgrade";
        proxy_buffering off;
    }
}
```

**Start 5 instances:**
```bash
./phase7_multi_route_test.exe 256 1 &  # Port 8443
./phase7_multi_route_test.exe 256 1 &  # Port 8444 (modify code)
./phase7_multi_route_test.exe 256 1 &  # Port 8445
./phase7_multi_route_test.exe 256 1 &  # Port 8446
./phase7_multi_route_test.exe 256 1 &  # Port 8447
```

**Result:** 3,220 RPS total throughput  

#### Advantages
- ✅ Works immediately (no code changes needed)
- ✅ Scales linearly (add more instances)
- ✅ Industry-standard approach
- ✅ Easy to maintain and monitor
- ✅ High availability (one instance goes down, others serve traffic)

#### Disadvantages
- ❌ Requires multiple instances (more hardware)
- ❌ Nginx adds ~1-2% latency overhead

---

## Decision Matrix

| Scenario | Best Path | Why |
|----------|-----------|-----|
| **Need 1,500 RPS** | Path 1 (Session reuse) | Simple, 30 min, 2-3x gain |
| **Need 3,000 RPS** | Path 3 (Load balance 5x) | Works immediately, scales |
| **Need 6,000+ RPS** | Path 3 (Load balance 10x) | Linear, proven approach |
| **Single instance required, >5K RPS** | Path 2 (GPU) | Only viable option |
| **Large payloads (>10KB), single instance** | Path 2 (GPU) + Path 1 | Combine approaches |

---

## My Recommendation

### For Your Use Case (Symmetra + Node.js API)

**Recommended path: Path 1 + Path 3**

#### Step 1: Test Session Reuse (TODAY)
```
Effort: 30 minutes
Expected: 644 → 1,500 RPS
Risk: None (just configuration)

Test:
  1. Enable HTTP Keep-Alive in Phase 8
  2. Enable TLS session caching in OpenSSL
  3. Run load test with persistent connections
  4. Measure RPS improvement
```

#### Step 2: If 1,500 RPS Is Enough
```
Deploy with load balancer:
  - 5 instances × 1,500 = 7,500 RPS total
  - Or 3 instances × 1,500 = 4,500 RPS
  - Simple, proven, scalable
```

#### Step 3: If Still Not Enough
```
Option A: Add more instances (easiest)
  - 10 instances × 1,500 = 15,000 RPS
  - Costs more but simple

Option B: Try GPU acceleration (if scaling not possible)
  - Implement CUDA AES-GCM
  - Risk: 4+ hours for maybe 2-5x
  - Only do if instances aren't feasible
```

---

## Action Items

### Immediate (Next Session)

- [ ] Test HTTP Keep-Alive in Phase 8
  - File: `test_phase8_http_server.cpp`
  - Change: Enable `Connection: keep-alive` header
  - Run: Load test with persistent connections
  - Expected: 2-3x improvement from 644 RPS

- [ ] Measure actual improvement
  - Use Node.js client with `keepAlive: true`
  - Run 10-minute load test
  - Record RPS and latency

### If Session Reuse Works (Most Likely)

- [ ] Deploy 3-5 instances with Nginx
  - Set up nginx load balancer
  - Start multiple engine instances
  - Verify round-robin distribution
  - Measure total throughput

### If Session Reuse Insufficient

- [ ] Evaluate GPU acceleration
  - Profile where time is spent (CPU flame graph)
  - Measure GPU transfer overhead
  - Prototype CUDA AES-GCM kernel
  - Compare costs vs load balancer approach

---

## Success Criteria

| Path | Target | Status |
|------|--------|--------|
| **Path 1** | 1,500 RPS (session reuse) | TBD - test today |
| **Path 3** | 5 × 1,500 = 7,500 RPS | Should work |
| **Final** | 5,000-10,000 RPS | Achievable with load balancer |

---

## Summary

**Don't over-engineer this.** 

The bottleneck is identified: **TLS encryption is CPU-bound.**

The solutions are simple:
1. ✅ **Session reuse:** 30 min, 2-3x gain (do this first)
2. ✅ **Load balancer:** 2 hours, linear scaling (do this for production)
3. ⚠️ **GPU acceleration:** 4+ hours, 2-5x gain (only if above two fail)

**Start with Path 1 (session reuse). It's the lowest effort with highest confidence.**

---

**Next: Test HTTP Keep-Alive and report results.** 📊
