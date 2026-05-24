#include "GpuWorker.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <cuda_runtime.h>

namespace symm {

// ── GpuWorkerPort ─────────────────────────────────────────────────────────────

void GpuWorkerPort::postMessage(const std::string& json,
                                 CUdeviceptr transferBuf,
                                 int transferLen) {
    WorkerMessage msg;
    msg.type        = "message";
    msg.payload     = json;
    msg.transferBuf = transferBuf;
    msg.transferLen = transferLen;
    push(std::move(msg));
}

void GpuWorkerPort::push(WorkerMessage msg) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push(std::move(msg));
    }
    cv_.notify_one();
}

bool GpuWorkerPort::receive(WorkerMessage& out, int timeoutMs) {
    std::unique_lock<std::mutex> lk(mu_);
    if (timeoutMs < 0) {
        cv_.wait(lk, [this] { return !queue_.empty() || closed_; });
    } else {
        cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                     [this] { return !queue_.empty() || closed_; });
    }
    if (queue_.empty()) return false;
    out = std::move(queue_.front());
    queue_.pop();
    return true;
}

void GpuWorkerPort::close() {
    closed_ = true;
    cv_.notify_all();
}

// ── GpuWorker ─────────────────────────────────────────────────────────────────

GpuWorker::GpuWorker(const std::string& scriptPath,
                     NvrtcCompiler& compiler,
                     const Options& opts)
    : compiler_(compiler), opts_(opts), scriptPath_(scriptPath)
{
    // Worker scripts are pre-compiled by the host before constructing GpuWorker;
    // the mod_ is passed in externally (see GpuWorker ctor options extension planned).
    // For now, leave mod_ default — dispatch kernel will be null and startup is skipped.
    (void)scriptPath;

    // Create a dedicated CUDA stream for this worker
    CUresult r = cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING);
    if (r != CUDA_SUCCESS)
        throw std::runtime_error("GpuWorker: cuStreamCreate failed");

    // Allocate a private slab index from the allocator
    // Workers use slots beyond the main StreamPool range — use the persistent heap
    // for now, and track allocation manually.
    slabIdx_ = -1;   // workers use their own heap region, not the main slab

    running_ = true;
    thread_  = std::thread(&GpuWorker::eventLoop, this);
}

GpuWorker::~GpuWorker() {
    terminate();
    if (stream_) cuStreamDestroy(stream_);
}

void GpuWorker::postMessage(const std::string& json,
                             CUdeviceptr transferBuf, int transferLen) {
    inPort_.postMessage(json, transferBuf, transferLen);
}

bool GpuWorker::receiveMessage(WorkerMessage& out, int timeoutMs) {
    return outPort_.receive(out, timeoutMs);
}

void GpuWorker::onMessage(std::function<void(const WorkerMessage&)> cb) {
    std::lock_guard<std::mutex> lk(cbMu_);
    onMessageCb_ = std::move(cb);
}

void GpuWorker::terminate() {
    // call_once guarantees exactly one thread performs the shutdown sequence,
    // eliminating the TOCTOU race between concurrent terminate() callers.
    std::call_once(terminateOnce_, [this] {
        terminate_.store(true, std::memory_order_relaxed);
        inPort_.close();
        outPort_.close();
        if (thread_.joinable()) thread_.join();
        running_.store(false, std::memory_order_release);
    });
}

// ── Worker event loop (runs on dedicated CPU thread) ─────────────────────────

void GpuWorker::eventLoop() {
    // Run the worker's onstart / global scope code first
    if (mod_.dispatch) {
        CUresult r = cuLaunchKernel(
            mod_.dispatch,
            1,1,1, 1,1,1,
            0, stream_, nullptr, nullptr);
        if (r != CUDA_SUCCESS) {
            std::fprintf(stderr, "[worker:%s] startup kernel failed\n",
                         opts_.name.c_str());
        }
        cuStreamSynchronize(stream_);
    }

    // Message loop — block waiting for messages from parent
    while (!terminate_.load()) {
        WorkerMessage msg;
        bool got = inPort_.receive(msg, 50);  // 50ms poll interval
        if (!got) continue;

        if (msg.type == "exit") break;

        try {
            dispatchKernel(msg);
        } catch (const std::exception& e) {
            WorkerMessage err;
            err.type    = "error";
            err.payload = std::string("{\"message\":\"") + e.what() + "\"}";
            outPort_.push(std::move(err));
            continue;
        }
    }

    // Notify parent that the worker has exited
    WorkerMessage exitMsg;
    exitMsg.type    = "exit";
    exitMsg.payload = "{}";
    outPort_.push(std::move(exitMsg));

    running_ = false;
}

void GpuWorker::dispatchKernel(const WorkerMessage& msg) {
    // Allocate a VRAM buffer for the message payload
    size_t payloadSize = msg.payload.size() + 1;
    CUdeviceptr d_payload = 0;
    cuMemAlloc(&d_payload, payloadSize);
    cuMemcpyHtoD(d_payload, msg.payload.c_str(), payloadSize);

    // Allocate response buffer
    static constexpr size_t RESP_SIZE = 64 * 1024;
    CUdeviceptr d_resp = 0;
    cuMemAlloc(&d_resp, RESP_SIZE);
    cuMemsetD8(d_resp, 0, RESP_SIZE);

    // Launch the worker's message handler kernel
    if (mod_.dispatch) {
        void* args[] = { &d_payload, &d_resp };
        cuLaunchKernel(
            mod_.dispatch,
            1,1,1, 1,1,1,
            0, stream_,
            args, nullptr);
    }

    // Sync and read response
    cuStreamSynchronize(stream_);

    std::vector<char> respBuf(RESP_SIZE);
    cuMemcpyDtoH(respBuf.data(), d_resp, RESP_SIZE);

    cuMemFree(d_payload);
    cuMemFree(d_resp);

    // Forward response to parent
    WorkerMessage resp;
    resp.type    = "message";
    resp.payload = std::string(respBuf.data());
    outPort_.push(std::move(resp));

    // Fire onMessage callback if registered
    {
        std::lock_guard<std::mutex> lk(cbMu_);
        if (onMessageCb_) onMessageCb_(resp);
    }
}

} // namespace symm
