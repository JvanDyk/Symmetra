// Unit tests for VramPipe ring buffer — CUDA device required.
//
// Build (requires NVCC and a CUDA-capable GPU):
//   nvcc -std=c++17 -I../../engine/src test_vram_pipe.cpp
//         ../../engine/src/runtime/GpuStream.cpp
//         -lcuda -lcudart -lpthread -o test_vram_pipe && ./test_vram_pipe
//
// Tests: ordering, backpressure (pipe full), early-close unblocks producer,
//        consumer sees end sentinel, concurrent producer+consumer safety.

#include "runtime/GpuStream.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>
#include <numeric>
#include <atomic>

using namespace symm;
using namespace std::chrono_literals;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

static CUdeviceptr allocChunk(size_t bytes) {
    CUdeviceptr p = 0;
    cuMemAlloc(&p, bytes);
    return p;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_push_pull_ordering() {
    VramPipe pipe(4);  // capacity 4 chunks
    for (int i = 0; i < 4; ++i) {
        CUdeviceptr p = allocChunk(8);
        cuMemsetD8(p, static_cast<uint8_t>(i), 8);
        pipe.push({ p, 8, false });
    }
    for (int i = 0; i < 4; ++i) {
        auto chunk = pipe.pop();
        uint8_t val = 0;
        cuMemcpyDtoH(&val, chunk.ptr, 1);
        CHECK(val == static_cast<uint8_t>(i), "chunks arrive in FIFO order");
        cuMemFree(chunk.ptr);
    }
}

void test_end_sentinel_delivered() {
    VramPipe pipe(4);
    pipe.push({ 0, 0, true });  // end-of-stream sentinel
    auto chunk = pipe.pop();
    CHECK(chunk.eos, "end sentinel is delivered");
}

void test_early_close_unblocks_producer() {
    // Producer will try to push into a full pipe; close() must unblock it.
    VramPipe pipe(2);
    // Fill the pipe
    pipe.push({ allocChunk(8), 8, false });
    pipe.push({ allocChunk(8), 8, false });

    std::atomic<bool> producerDone{ false };
    std::thread producer([&] {
        // This push will block because pipe is full
        pipe.push({ allocChunk(8), 8, false });
        producerDone.store(true);
    });

    std::this_thread::sleep_for(50ms);
    CHECK(!producerDone.load(), "producer is blocked on full pipe");

    pipe.close();   // must unblock the producer
    producer.join();
    CHECK(producerDone.load(), "producer unblocked after close()");

    // Drain remaining chunks
    while (true) {
        auto c = pipe.pop();
        if (c.ptr) cuMemFree(c.ptr);
        if (c.eos || c.ptr == 0) break;
    }
}

void test_concurrent_producer_consumer() {
    static constexpr int N = 100;
    VramPipe pipe(8);
    std::vector<int> received;
    received.reserve(N);

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            CUdeviceptr p = allocChunk(sizeof(int));
            cuMemcpyHtoD(p, &i, sizeof(int));
            pipe.push({ p, sizeof(int), false });
        }
        pipe.push({ 0, 0, true });  // end sentinel
    });

    // Consumer runs on this thread
    while (true) {
        auto chunk = pipe.pop();
        if (chunk.eos) break;
        int val = 0;
        cuMemcpyDtoH(&val, chunk.ptr, sizeof(int));
        received.push_back(val);
        cuMemFree(chunk.ptr);
    }

    producer.join();

    CHECK(static_cast<int>(received.size()) == N, "all items received");
    bool ordered = true;
    for (int i = 0; i < N; ++i)
        if (received[i] != i) { ordered = false; break; }
    CHECK(ordered, "items received in order");
}

void test_backpressure() {
    // Push and pull at the same rate; pipe should never deadlock.
    static constexpr int N = 200;
    VramPipe pipe(4);
    std::atomic<int> consumed{ 0 };

    std::thread consumer([&] {
        for (int i = 0; i < N; ++i) {
            auto c = pipe.pop();
            if (c.ptr) cuMemFree(c.ptr);
            consumed.fetch_add(1);
        }
    });

    for (int i = 0; i < N; ++i) {
        pipe.push({ allocChunk(64), 64, false });
    }
    consumer.join();

    CHECK(consumed.load() == N, "backpressure: all items consumed");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    if (cuInit(0) != CUDA_SUCCESS) {
        std::fprintf(stderr, "SKIP: no CUDA device found\n");
        return 0;
    }
    CUdevice dev; CUcontext ctx;
    cuDeviceGet(&dev, 0);
    cuCtxCreate(&ctx, 0, dev);

    test_push_pull_ordering();
    test_end_sentinel_delivered();
    test_early_close_unblocks_producer();
    test_concurrent_producer_consumer();
    test_backpressure();

    std::printf("Results: %d passed, %d failed\n", passed, failed);
    cuCtxDestroy(ctx);
    return failed > 0 ? 1 : 0;
}
