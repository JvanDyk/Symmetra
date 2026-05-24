#pragma once
#include <cuda.h>
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include "NvrtcCompiler.h"
#include "StreamPool.h"
#include "GpuAllocator.h"

namespace symm {

// ── Message — VRAM-backed inter-worker message ────────────────────────────────
//
// postMessage() serialises the payload to a VRAM slab buffer.
// The receiving worker deserialises it on its own stream.
// MessagePort semantics: structured clone via JSON round-trip on GPU.
//
struct WorkerMessage {
    std::string type;       // "message", "error", "exit"
    std::string payload;    // JSON-encoded structured data
    int         transferLen = 0;       // byte count of any transferred GpuBuffer
    CUdeviceptr transferBuf = 0;       // VRAM pointer of transferred buffer (zero-copy)
};

// ── GpuWorkerPort — one end of a MessageChannel ──────────────────────────────

class GpuWorkerPort {
public:
    void postMessage(const std::string& jsonPayload,
                     CUdeviceptr transferBuf = 0,
                     int transferLen = 0);

    // Block until a message arrives; returns false if port is closed
    bool receive(WorkerMessage& out, int timeoutMs = -1);

    void close();
    bool closed() const { return closed_.load(); }

private:
    friend class GpuWorker;
    std::queue<WorkerMessage> queue_;
    mutable std::mutex        mu_;
    std::condition_variable   cv_;
    std::atomic<bool>         closed_{ false };

    void push(WorkerMessage msg);
};

// ── GpuWorker — isolated GPU execution context ────────────────────────────────
//
// Maps to the Node.js worker_threads.Worker API.
//
// Each GpuWorker owns:
//   - Its own CUDA stream (from StreamPool, or a dedicated one)
//   - Its own VRAM slab index (isolated from parent's slab)
//   - A compiled NVRTC module for the worker script
//   - A bidirectional message channel (parent ↔ worker)
//   - A CPU thread that runs the worker event loop
//
// The worker runs its handler kernel on its own stream — completely independent
// of the parent stream. Transfers use zero-copy VRAM pointer passing.
//
class GpuWorker {
public:
    struct Options {
        size_t heapBytes      = 4 * 1024 * 1024;   // worker-private slab size
        bool   shareGpuMemory = false;              // allow parent GpuBuffer* access
        std::string name;                           // diagnostic name
    };

    // Compile and start a worker from a TypeScript/JS source path.
    // Returns immediately — worker starts in background.
    GpuWorker(const std::string& scriptPath,
              NvrtcCompiler& compiler,
              const Options& opts = {});

    ~GpuWorker();

    // Send a message to the worker. Can be called from any thread.
    void postMessage(const std::string& jsonPayload,
                     CUdeviceptr transferBuf = 0,
                     int transferLen = 0);

    // Receive a message from the worker.
    bool receiveMessage(WorkerMessage& out, int timeoutMs = -1);

    // Listen for messages from the worker (callback, runs on caller's thread)
    void onMessage(std::function<void(const WorkerMessage&)> cb);

    // Terminate the worker (sends "exit" message, joins the thread).
    void terminate();

    // True if the worker is still running.
    bool running() const { return running_.load(); }

    // Worker slot index in GpuAllocator — for diagnostics
    int slabIdx() const { return slabIdx_; }

private:
    // Runs on the worker's dedicated CPU thread
    void eventLoop();

    // Dispatch a single kernel invocation for one message
    void dispatchKernel(const WorkerMessage& msg);

    NvrtcCompiler&         compiler_;
    Options                opts_;
    std::string            scriptPath_;
    CompiledModule         mod_{};

    // Worker's own stream (not from the main StreamPool — workers get dedicated streams)
    CUstream               stream_ = nullptr;
    int                    slabIdx_ = -1;  // allocated from GpuAllocator persistent heap

    // Message channels (parent→worker and worker→parent)
    GpuWorkerPort          inPort_;   // parent writes, worker reads
    GpuWorkerPort          outPort_;  // worker writes, parent reads

    std::thread            thread_;
    std::atomic<bool>      running_{ false };
    std::atomic<bool>      terminate_{ false };
    std::once_flag         terminateOnce_;

    std::function<void(const WorkerMessage&)> onMessageCb_;
    std::mutex                                 cbMu_;
};

} // namespace symm
