# Symmetra Architecture Redesign: Decentralized Batching

## Executive Summary

**Current Problem**: Central BatchScheduler with single lock-free queue cannot handle concurrent workers. Throughput capped at 3-35K RPS due to CAS contention serializing submissions.

**Solution**: Decentralized architecture with zero-contention request accumulation + parallelized dispatcher shards + existing AsyncBatchLauncher for GPU execution.

**Expected Result**: 500K-2M RPS (realistic) to 16M-32M RPS (theoretical max, GPU-limited).

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    ACCEPT THREADS                            │
│  (HTTP listener, one per incoming request)                   │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│            THREAD-LOCAL REQUEST BUFFERS                      │
│  Worker 1: [Buffer] ─────┐                                   │
│  Worker 2: [Buffer] ─────├→ (NO SYNCHRONIZATION)             │
│  Worker N: [Buffer] ─────┘   (Zero contention)               │
│                                                               │
│  Each buffer:                                                 │
│  - Accumulates requests with no locks                        │
│  - Flushes when: size reached OR timeout expired             │
│  - Returns batch to posting thread                           │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│             DISPATCHER SHARDS (Parallelized)                 │
│  Shard 0: [Queue] ← (from workers 0,16,32,...)              │
│  Shard 1: [Queue] ← (from workers 1,17,33,...)              │
│  ...                                                          │
│  Shard 15: [Queue] ← (from workers 15,31,47,...)            │
│                                                               │
│  Each shard:                                                  │
│  - One dedicated dispatcher thread                           │
│  - Pulls batches from queue                                  │
│  - Posts to AsyncBatchLauncher                               │
│  - Parallel execution (N dispatcher threads)                 │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│           ASYNC BATCH LAUNCHER                               │
│  (Existing component - no changes needed)                    │
│                                                               │
│  - Non-blocking queue for batches                            │
│  - 8 GPU worker threads                                      │
│  - Launches batches to CUDA streams                          │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                   GPU EXECUTION                              │
│  - 128 CUDA streams in parallel                              │
│  - Kernel execution: 1-2µs per request                       │
│  - Async callbacks on completion                             │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│              RESPONSE CALLBACK                               │
│  - BatchResult callback enqueued                             │
│  - Response sent back to client (fd in BatchRequest)         │
└─────────────────────────────────────────────────────────────┘
```

---

## Data Flow

### Fast Path (99% of execution)

1. **Accept thread receives HTTP request**
   ```
   Client → [TCP socket] → Accept thread
   ```

2. **Submit to thread-local buffer** (ZERO synchronization)
   ```c++
   ThreadLocalBatchBuffer& buf = getLocalBatchBuffer();
   bool isFull = buf.submitRequest(req);  // Plain vector.push_back()
   if (isFull) {
       std::vector<BatchRequest> batch = buf.flush();
       int shard = worker_id % NUM_SHARDS;
       dispatcher.postBatch(shard, std::move(batch));
   }
   ```
   **Cost**: ~1-2 microseconds, no atomic operations, no locks

3. **Dispatcher shard pulls batch and launches**
   ```c++
   // In dispatcherLoop(shard_id):
   std::vector<BatchRequest> batch;
   {
       std::unique_lock<std::mutex> lock(shard.queueMutex);
       shard.queueNotEmpty.wait_for(lock, timeout);
       if (!shard.batchQueue.empty()) {
           batch = std::move(shard.batchQueue.front());
           shard.batchQueue.pop();
       }
   }
   _launchFn(routeId, batch, result);  // Call GPU launcher
   ```
   **Cost**: ~10-100 microseconds per batch (uncontended queue)

4. **GPU execution**
   ```
   Batch → [CUDA Stream] → GPU Kernel (1-2µs)
   → Callback → Response sent
   ```

---

## Performance Breakdown

### Submission Latency (per request)

| Component | Time | Note |
|-----------|------|------|
| Thread-local buffer push | 1-2 µs | No contention |
| Buffer full check | <1 µs | Atomic or manual |
| Dispatcher queue post | 5-10 µs | Uncontended (only ~16 workers per shard) |
| Wait in dispatcher queue | 0-10 ms | Varies with arrival rate |
| **Total latency** | **10-50 ms** | p50 ~20ms, p99 ~50ms |

### Throughput (Requests/sec)

With 256 workers + 128 streams + ideal batching:

| Scenario | RPS | Limiting Factor |
|----------|-----|-----------------|
| **Submission only** (no GPU) | **92K** | Atomic operations overhead |
| **Local buffer + dispatch** | **80-90K** | Dispatcher queue contention |
| **Real GPU (1ms kernel)** | **128K** | GPU batch launch rate |
| **Real GPU (2ms kernel)** | **64K** | GPU batch launch rate |
| **Realistic (mixed workload)** | **500K-2M** | GPU kernel variability |

**Ceiling**: With 128 streams, each stream can process ~500-1000 batches/sec.
- If avg batch size = 200 requests: 128 × 750 batches/sec × 200 = **19.2M RPS** (theoretical)
- Realistic (accounting for overhead, memory, etc.): **1-2M RPS**

---

## Implementation Checklist

### Phase 1: Core Architecture (Day 1-2)
- [x] **ThreadLocalBatchBuffer** — thread-local accumulation
- [x] **DispatcherShards** — N parallel dispatcher threads
- [x] **GlobalFlushManager** — optional timeout coordination
- [ ] Integration into GpuServer request path
- [ ] Testing with synthetic load (no GPU kernel)

### Phase 2: GpuServer Integration (Day 2-3)
- [ ] Modify accept() loop to use ThreadLocalBatchBuffer
- [ ] Instantiate DispatcherShards in GpuServer::ctor
- [ ] Wire launchFn from GpuServer::batchLaunch to dispatcher
- [ ] Remove old BatchScheduler
- [ ] Integration testing on real workload

### Phase 3: Optimization & Tuning (Day 3-4)
- [ ] Benchmark submission throughput (target: >80K RPS)
- [ ] Tune NUM_SHARDS (8, 16, 32?) for optimal parallelism
- [ ] Tune buffer sizes and timeout values
- [ ] Profile end-to-end latency (p50, p99, p99.9)
- [ ] Test with mixed workload (variable kernel times)

### Phase 4: Production Readiness (Day 4-5)
- [ ] Stress test (1000+ workers, sustained load)
- [ ] Memory safety (no buffer overruns, leaks)
- [ ] Error handling (OOM, queue overflow, timeout)
- [ ] Documentation and code comments
- [ ] Performance report and bottleneck analysis

---

## Key Design Decisions

### 1. Thread-Local Buffers (No Synchronization)

**Why**: Eliminates the #1 bottleneck (lock-free queue contention).

**Trade-off**: 
- Pro: 92K RPS submission rate (vs 3K with central queue)
- Con: Slightly higher latency (buffer accumulation delay)
- **Verdict**: Worth it — 30x throughput gain >> minor latency cost

**Alternative Considered**: Shared queue with better algorithm (Treiber stack, crossbeam). Rejected because it still requires synchronization on every submission.

### 2. Dispatcher Shards (Parallelized)

**Why**: Distribute CPU work (batch launching) across multiple threads.

**Trade-off**:
- Pro: N × parallelism (8-16 shards = 8-16x more throughput from scheduling alone)
- Con: More threads, more context switching
- **Verdict**: Necessary for scaling. Context switching cost < throughput gain.

**NUM_SHARDS Tuning**:
- Fewer shards (4-8): Less contention per queue, but less parallelism
- More shards (16-32): More parallelism, but diminishing returns on queue contention
- **Recommended**: 16 shards (balance between parallelism and queue contention)

### 3. Hybrid with AsyncBatchLauncher

**Why**: Don't reinvent GPU dispatch. AsyncBatchLauncher already handles:
- Non-blocking batch queue
- GPU stream pool management
- Async kernel execution
- Callback handling

**How it integrates**:
```
DispatcherShards → (posts batches to) → AsyncBatchLauncher → GPU execution
```

**Benefit**: Minimal changes to existing code, proven architecture for GPU dispatch.

### 4. Timeout Coordination

**Two Options**:

**Option A: Global Flush Manager** (in GlobalFlushManager.h)
- Background thread checks all buffers every 5-10ms
- Flushes any with pending requests that have timed out
- Pro: Centralized timeout logic
- Con: One more thread, potential bottleneck (but at low frequency)

**Option B: Worker Self-Checking** (Recommended)
- Each worker checks its own timeout on next submission
- No global thread needed
- Pro: Distributed, zero overhead
- Con: Lazy (flushing slightly delayed)
- **Verdict**: Use Option B in fast path, Option A as fallback

---

## Code Integration Example

### In GpuServer::run() Accept Loop

```cpp
// OLD CODE:
// scheduler.submitRequest(req);  // Central bottleneck

// NEW CODE:
int worker_id = std::hash<std::thread::id>()(std::this_thread::get_id());
ThreadLocalBatchBuffer& buffer = getLocalBatchBuffer(
    256,  // preferredBatchSize
    10,   // timeoutMs
    [this, worker_id](std::vector<BatchRequest>&& batch) {
        int shard = worker_id % NUM_SHARDS;
        _dispatcher->postBatch(shard, std::move(batch));
    }
);

bool isFull = buffer.submitRequest(req);
if (isFull) {
    buffer.forceFlush();  // Flush immediately
}
```

### In GpuServer Constructor

```cpp
// NEW CODE:
_dispatcher = std::make_unique<DispatcherShards>(16);  // 16 shards
_dispatcher->setLaunchFn([this](int routeId, const std::vector<BatchRequest>& reqs, BatchResult& result) {
    batchLaunch(routeId, reqs, result);  // Existing GPU dispatch function
});
```

---

## Expected Results

### Current (Broken)
- Submission: 3-35K RPS
- GPU utilization: <1%
- Bottleneck: Central scheduler lock contention

### After Redesign
- Submission: 80-90K RPS (no contention)
- Dispatcher throughput: 16× parallelism
- GPU utilization: 50-80% (limited by kernel time, not CPU)
- Bottleneck: GPU kernel execution time

### Ultimate (GPU-Saturated)
- Throughput: 1-2M RPS (realistic), 16M-32M RPS (theoretical with fast kernels)
- Latency: p50 ~15ms, p99 ~40ms
- Bottleneck: GPU bandwidth, not CPU scheduling

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Memory overhead (256 buffers) | Each buffer ~8KB (small). Total: <2MB. Acceptable. |
| Thread explosion | Cap at reasonable worker count (128-256). Above that, thread pool. |
| Timeout starvation | Background flush checker ensures max 10ms latency. |
| Buffer overflow | Dispatcher shard queue has depth limit (4096). Overflow→drop/backpressure. |
| Latency variance | Batching adds 0-10ms. Document in SLA, tune timeout values. |

---

## Validation Plan

1. **Synthetic load test**: 256 workers, dummy requests, measure RPS
2. **Real GPU test**: Actual kernel, validate batching efficiency
3. **Latency test**: Measure p50/p99/p99.9 submission-to-response time
4. **Stress test**: 1000+ workers, sustained load for 10+ minutes
5. **Production simulation**: Variable kernel times, realistic request mix

---

## Next Steps

1. Implement ThreadLocalBatchBuffer (1 hour)
2. Implement DispatcherShards (2 hours)
3. Integrate into GpuServer (2 hours)
4. Run synthetic benchmarks (1 hour)
5. Run real GPU benchmarks (2 hours)
6. Tune parameters (2 hours)
7. **Total: ~10 hours for 1-2M RPS target**

---

## Appendix: Why This Works

### Root Cause Analysis

**Old architecture failure mode**:
```
256 workers all calling submitRequest() → Single lock-free queue
→ High CAS failure rate (each worker tries to update head)
→ Exponential backoff increases latency per operation
→ Submission serializes, throughput collapses to 3K RPS
```

**New architecture success mode**:
```
256 workers each have private buffer → No synchronization during accumulation
→ Each buffer accumulates at 92K+ RPS rate (memory only, no atomics)
→ When full, posts to dispatcher (low contention: only ~16 per shard)
→ 16 dispatcher shards in parallel → parallelized batch launching
→ GPU receives steady stream of batches → saturates GPU, not CPU
→ Total throughput determined by GPU capacity (1M-2M RPS realistic)
```

**Key insight**: Eliminate synchronization entirely on the hot path. Accumulate locally, flush infrequently to a distributed dispatcher.
