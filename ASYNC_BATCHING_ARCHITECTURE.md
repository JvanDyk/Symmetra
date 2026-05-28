# Asynchronous Batching Architecture (Zero Blocking)

**Design Goal:** Every operation batches and proceeds without blocking anything. No deadlocks, no waits, no synchronous operations.

---

## Current Problem (Blocking Model)

```
HTTP Thread          Scheduler Thread         GPU              Response Thread
    |                   |                      |                    |
    |-- submit req ----->|                      |                    |
    |                    |-- cuLaunchKernel --->|                    |
    |                    |                      | (executing)        |
    |                    |-- cuMemcpyDtoH ---X  |                    |
    |                    | (BLOCKS HERE!)       |                    |
    |                    |<-- results ------    |                    |
    |                    |-- sendResponses ---X |                    |
    |                    | (BLOCKS HERE!)       |                    |
    |<-- response --------|-X (scheduler stuck) |                    |
    |                    |                      |                    |
```

**Problem:** Scheduler thread blocks on GPU memcopy and socket I/O. New requests queue up waiting. Deadlock occurs when all conditions align.

---

## Correct Architecture (Fully Async, Pipelined)

```
┌─ HTTP Accept Threads (8+) ────────────────────────────────────────┐
│  Non-blocking: parse request → submit to queue → return           │
└──────────────────────────────────────────────────────────────────┘
                           ↓
┌─ Request Queue (lock-free atomic operations) ──────────────────────┐
│  Ring buffer, push/pop without mutex                               │
└──────────────────────────────────────────────────────────────────┘
                           ↓
┌─ Batch Scheduler (dedicated thread) ───────────────────────────────┐
│  Collects requests → batches → queues GPU work (non-blocking)      │
└──────────────────────────────────────────────────────────────────┘
                           ↓
┌─ GPU Command Queue (lock-free) ───────────────────────────────────┐
│  Enqueued GPU operations (upload, kernel, download) async          │
└──────────────────────────────────────────────────────────────────┘
              ↙                ↓                  ↘
    GPU Upload           GPU Execution        GPU Download
    (async copy)         (kernel running)      (async copy)
    (1-2ms)              (1-2ms)               (1-2ms)
              ↘                ↓                  ↙
           All overlapped simultaneously
                           ↓
┌─ Response Completion Queue (lock-free) ─────────────────────────┐
│  Ready results (don't wait, just enqueue)                        │
└────────────────────────────────────────────────────────────────┘
                           ↓
┌─ Response Sender Pool (8+ threads) ────────────────────────────┐
│  Each thread sends responses (non-blocking sockets)             │
└────────────────────────────────────────────────────────────────┘
                           ↓
                    Client Response
```

**Key Properties:**
- ✅ No blocking operations anywhere
- ✅ All I/O async (GPU memcopy, socket send)
- ✅ All queues are lock-free (atomic operations only)
- ✅ Multiple batches in flight simultaneously
- ✅ Pipelined execution (batch N-1 sending while N executes, N+1 being batched)

---

## Concrete Implementation

### 1. Request Submission (HTTP Accept Thread)

```cpp
// Current (blocking):
std::unique_lock<std::mutex> lk(_mu);
_queues[routeId].push_back(request);  // BLOCKS if mutex is held
lk.unlock();

// Correct (async):
// Use lock-free queue (std::atomic with CAS loop)
RequestNode* node = allocNode();
node->request = request;
node->next = nullptr;

// CAS loop: append to tail without blocking
while (true) {
    RequestNode* tail = _tail.load(std::memory_order_acquire);
    RequestNode* next = nullptr;
    if (tail->next.compare_exchange_weak(next, node, 
        std::memory_order_release, std::memory_order_acquire)) {
        _tail.store(node, std::memory_order_release);
        break;  // Successfully added, return immediately
    }
}
// Caller returns immediately - never blocks
```

### 2. Batch Collection (Scheduler Thread)

```cpp
// Current (blocking):
while (cv.wait(lock, []{...})) {  // Waits for queue
    batchRequests();  // Then processes
}

// Correct (async):
while (running) {
    // Non-blocking dequeue (returns immediately)
    std::vector<Request> batch = tryDequeueUpTo(256);
    
    if (batch.empty()) {
        // Nothing ready yet - don't block, just sleep briefly
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        continue;
    }
    
    // Queue GPU work (non-blocking)
    queueGpuBatch(batch);  // Returns immediately
}
```

### 3. GPU Operations (Async Dispatch)

```cpp
// Current (blocking):
void batchLaunch(...) {
    uploadBatch(...);              // Async (good)
    cuLaunchKernel(...);           // Async (good)
    cuMemcpyDtoH(...);             // SYNCHRONOUS - BLOCKS!!!
    sendBatchResponses(...);       // SYNCHRONOUS - BLOCKS!!!
}

// Correct (async):
void submitGpuBatch(...) {
    // All GPU operations queued async - function returns immediately
    
    // 1. Upload (async)
    CUstream stream = getStream();
    cuMemcpyHtoDAsync(d_reqs, h_reqs, size, stream);
    
    // 2. Kernel launch (async)
    cuLaunchKernel(batch_kernel, 1, 1, batch_size, 0, stream, args);
    
    // 3. Download (async) - DO NOT WAIT
    cuMemcpyDtoHAsync(h_results, d_results, size, stream);
    
    // 4. Callback when complete (CUDA callback)
    cuStreamAddCallback(stream, onBatchComplete, batch_id, 0);
    
    // 5. Function returns immediately - no blocking!
    return;
}

// Callback (runs when GPU work finishes)
void onBatchComplete(CUstream stream, CUresult result, void* batch_id) {
    // GPU work is done, results are in h_results
    // Queue response sending (non-blocking)
    queueResponses(results);
}
```

### 4. Response Sending (I/O Thread Pool)

```cpp
// Current (blocking scheduler):
void sendBatchResponses(...) {
    for (const auto& req : requests) {
        ::send(fd, response, len);  // BLOCKS on socket!
    }
}

// Correct (separate thread pool):
// Main scheduler: just queue the responses
queueResponses(batch_id, responses);  // Returns immediately

// Separate I/O thread pool:
void ioWorkerThread() {
    while (running) {
        auto responses = tryDequeueResponses(32);  // Non-blocking
        
        if (responses.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        
        // Send responses (can block on socket, but that's OK - 
        // we're on a dedicated I/O thread, not the scheduler)
        for (const auto& resp : responses) {
            ::send(resp.fd, resp.data, resp.len);  // OK to block here
        }
    }
}
```

---

## Lock-Free Queue Implementation

```cpp
template<typename T, size_t Capacity = 65536>
class LockFreeQueue {
    std::array<T, Capacity> _buffer;
    std::atomic<uint64_t> _head{0};
    std::atomic<uint64_t> _tail{0};

public:
    // Non-blocking enqueue
    bool tryEnqueue(const T& item) {
        uint64_t tail = _tail.load(std::memory_order_relaxed);
        uint64_t next_tail = (tail + 1) % Capacity;
        
        if (next_tail == _head.load(std::memory_order_acquire)) {
            return false;  // Queue full, return immediately
        }
        
        _buffer[tail % Capacity] = item;
        _tail.store(next_tail, std::memory_order_release);
        return true;
    }
    
    // Non-blocking dequeue
    bool tryDequeue(T& item) {
        uint64_t head = _head.load(std::memory_order_relaxed);
        
        if (head == _tail.load(std::memory_order_acquire)) {
            return false;  // Queue empty, return immediately
        }
        
        item = _buffer[head % Capacity];
        _head.store((head + 1) % Capacity, std::memory_order_release);
        return true;
    }
    
    // Dequeue multiple (for batching)
    size_t tryDequeueBatch(std::vector<T>& out, size_t maxCount) {
        size_t count = 0;
        while (count < maxCount && tryDequeue(out[count])) {
            count++;
        }
        return count;
    }
};
```

---

## Execution Timeline (Fully Pipelined)

```
Time    HTTP Thread(s)          Scheduler               GPU                 Response I/O
────────────────────────────────────────────────────────────────────────────────────────
0ms     Accept 1-32 req         Collect batch 1         (idle)              (idle)
        Submit to queue
        Return (non-blocking)
        
1ms     Accept 33-64 req        cuMemcpyHtoD batch 1    (idle)              (idle)
        Submit to queue         cuLaunchKernel batch 1
        Return
        Queue GPU work
        Return (non-blocking)
        
2ms     Accept 65-96 req        cuMemcpyHtoD batch 2    Kernel 1 running    (idle)
        Submit to queue         cuLaunchKernel batch 2  (1-2ms)
        Return                  Queue GPU work
                                Return (non-blocking)
        
3ms     Accept 97-128 req       cuMemcpyHtoD batch 3    Kernel 1 running    (idle)
        Submit to queue         cuLaunchKernel batch 3  Kernel 2 running
        Return                  Return (non-blocking)   (overlapped!)
        
4ms     Accept 129+ req         Check batch 1 results   Kernel 1 done       Send batch 1
        Submit to queue         (already downloaded!)   cuMemcpyDtoH batch 2 responses
        Return                  Queue response send     Kernel 2 done       (returns immed)
                                Collect batch 4        cuMemcpyDtoH batch 3
        
5ms     (continuing...)         Queue GPU work batch 4  Kernel 3 running    Send batch 2
                                Return (non-blocking)   (pipelined!)        responses
```

**Key Points:**
- ✅ Every operation batches immediately
- ✅ No operation blocks any thread
- ✅ Batches N, N+1, N+2 in flight simultaneously
- ✅ GPU fully saturated (kernel always running)
- ✅ Throughput: 32 requests per 2-3ms = ~10,000-15,000 req/s minimum

---

## What Must Change

### 1. RequestQueue (BatchScheduler.cpp)
```cpp
// Remove: std::deque<BatchRequest> pending
// Add: LockFreeQueue<BatchRequest> pending
```

### 2. GPU Launch (GpuServer.cpp)
```cpp
// Remove: All synchronous calls (cuMemcpyDtoH, ::send)
// Add: Async versions with callbacks (cuMemcpyDtoHAsync, CUDA callbacks)
```

### 3. Response Sending (GpuServer.cpp)
```cpp
// Remove: Sending from batchLaunch()
// Add: Queue responses to separate I/O thread pool
```

### 4. Scheduler Thread (BatchScheduler.cpp)
```cpp
// Remove: Condition variable wait (can block)
// Add: Non-blocking loop with sleep (100-500µs intervals)
```

### 5. I/O Thread Pool (NEW)
```cpp
// Add: Dedicated thread pool for response sending
// Each thread handles socket I/O independently
// Never blocks the scheduler thread
```

---

## Expected Performance

### Current (Broken)
- Sequential: 4 req/s
- Parallel: Hangs/deadlock

### After Async Fix
- Sequential: 100-200 req/s
- Batched (32 concurrent): 10,000+ req/s
- Peak (fully pipelined): 1,000,000+ req/s potential

---

## Verification Checklist

- [ ] No mutex.lock() or unique_lock in hot path
- [ ] No blocking I/O on scheduler thread
- [ ] No cuMemcpyDtoH() (only cuMemcpyDtoHAsync)
- [ ] No ::send() on scheduler thread (only from I/O pool)
- [ ] All queues are lock-free (atomic operations only)
- [ ] Condition variables removed (replaced with sleep loops or callbacks)
- [ ] Parallel requests don't cause hangs
- [ ] Sequential requests achieve 100+ req/s
- [ ] Multiple batches in-flight simultaneously (verify with metrics)
- [ ] GPU utilization stays high (kernel always running)

---

## Summary

**Current:** 1 blocking operation per request = 250ms = 4 req/s  
**Target:** 0 blocking operations = pipelined batches = 10,000+ req/s

The difference is **architectural:** move from synchronous request processing to fully asynchronous, lock-free batching with pipelined GPU operations.

Every section should operate in parallel, batching requests and never waiting for anything.
