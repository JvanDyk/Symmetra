# Performance Baseline - Initial Measurements

**Date**: 2026-05-29
**Hardware**: RTX 5070, CUDA 13.2
**Test Server**: test_simple_server.ts (stateless JSON handler)

## Results

| Metric | Value |
|--------|-------|
| Server Startup Time | 2 seconds |
| Requests Sent | 20 |
| Total Duration | 217 seconds |
| Average Latency per Request | ~10.85 seconds |
| Measured RPS | ~0.09 |

## Analysis

### Current Status: 🔴 Severe Bottleneck Detected

The end-to-end architecture works correctly (verified: GPU execution → HTTP response), but there is a significant per-request overhead that is blocking throughput:

- **Expected**: Millions of RPS with 256-thread GPU batching
- **Observed**: ~0.09 RPS (1 request per ~11 seconds)
- **Gap**: 9+ orders of magnitude slower than theoretical

### Root Cause Investigation Required

The 10+ second latency per request suggests one or more of:

1. **Handler Re-compilation**: Each request may be triggering JIT re-compilation of the handler
   - Hypothesis: The handler is not being cached after initial compilation
   - Test: Check if second/third requests on same server have same latency

2. **GPU Kernel Launch Overhead**: Significant overhead in kernel launch or stream management
   - Hypothesis: Not using stream reuse or batching efficiently
   - Test: Compare single vs. concurrent requests

3. **Memory Transfer Overhead**: GPU→CPU memory transfer is slower than expected
   - Hypothesis: Not using pinned memory or optimal transfer sizes
   - Test: Profile memory bandwidth during transfer

4. **Batching Inefficiency**: Requests not being batched efficiently
   - Hypothesis: Each request treated as separate batch, not accumulated
   - Test: Check batch size allocation and utilization

5. **Request Queuing**: Requests queueing or blocking on resource contention
   - Hypothesis: ThreadPool or I/O handling bottleneck
   - Test: Profile CPU utilization and thread activity

### Next Steps: Performance Profiling

**CRITICAL**: Identify which component is taking ~11 seconds per request:

1. **Instrument code with timing breakpoints**:
   - Request reception time
   - Handler invocation time
   - GPU kernel launch time
   - GPU execution time
   - Result download time
   - Response send time

2. **Profile with NVIDIA Tools**:
   - `nvprof` to measure GPU kernel execution time
   - `nvidia-smi` to monitor GPU utilization and memory

3. **CPU Profiling**:
   - Identify which code paths are taking the most time
   - Check for synchronous waits or blocking operations

4. **Test Variable Latencies**:
   - Simple handler (current): ~11s
   - Empty handler (immediate return): Should show minimum overhead
   - Computation-heavy handler: Should show GPU overhead

## Comparison to Theoretical

| Variant | Theoretical | Current | Gap |
|---------|-----------|---------|-----|
| LIGHTNING | 32.5M RPS | 0.09 RPS | 361M× slower |
| HYPER | 16M RPS | 0.09 RPS | 178M× slower |
| EXTREME | 8M RPS | 0.09 RPS | 89M× slower |

## Implications

- ✅ Architecture is correct (end-to-end works)
- ✅ GPU execution is working
- ❌ Performance is unacceptable for production use
- ⚠️ Critical optimization phase needed before benchmarking variants

## Conclusion

The GPU→HTTP pipeline successfully computes and returns results. However, the per-request overhead (~11 seconds) completely dominates the response time, making the system unsuitable for API use cases. The next session must focus on:

1. **Identifying the bottleneck** (which code path takes 11s?)
2. **Eliminating the bottleneck** (caching? batching? efficiency?)
3. **Measuring improvement** (iterate toward millions of RPS)

Following ToDo.txt: "Keep repeating this step to continuously make improvements nonstop."
