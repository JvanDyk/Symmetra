/**
 * SSP Multi-GPU Dispatcher Implementation — Phase 6
 */

#include "runtime/SSPMultiGPUDispatcher.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ────────────────────────────────────────────────────────────────────────────

SSPMultiGPUDispatcher::SSPMultiGPUDispatcher(uint32_t num_gpus, uint32_t batch_size)
    : num_gpus_(num_gpus),
      batch_size_(batch_size),
      next_gpu_idx_(0)
{
    gpu_contexts_.reserve(num_gpus_);
}

SSPMultiGPUDispatcher::~SSPMultiGPUDispatcher() {
    shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// Initialization
// ────────────────────────────────────────────────────────────────────────────

bool SSPMultiGPUDispatcher::initialize() {
    // Query available GPUs
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess) {
        return false;
    }

    // Use minimum of requested and available GPUs
    uint32_t gpus_to_use = std::min(static_cast<uint32_t>(device_count), num_gpus_);
    if (gpus_to_use == 0) {
        return false;
    }

    // Initialize each GPU context
    for (uint32_t i = 0; i < gpus_to_use; i++) {
        auto ctx = std::make_shared<MultiGPUContext>();
        ctx->gpu_id = i;

        // Set device
        if (cudaSetDevice(i) != cudaSuccess) {
            return false;
        }

        // Create stream
        if (cudaStreamCreate(reinterpret_cast<cudaStream_t*>(&ctx->cuda_stream)) != cudaSuccess) {
            return false;
        }

        // Initialize SSP for this GPU
        ctx->gpu_ssp = std::make_unique<SSPLayer>();
        if (!ctx->gpu_ssp->initialize()) {
            return false;
        }

        // Start worker thread
        ctx->worker_running = true;
        ctx->worker_thread = std::thread([this, i]() { this->gpuWorkerThread(i); });

        gpu_contexts_.push_back(ctx);
    }

    num_gpus_ = gpus_to_use;
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// GPU Worker Thread
// ────────────────────────────────────────────────────────────────────────────

void SSPMultiGPUDispatcher::gpuWorkerThread(int gpu_id) {
    if (cudaSetDevice(gpu_id) != cudaSuccess) {
        return;
    }

    auto ctx = gpu_contexts_[gpu_id];

    while (ctx->worker_running) {
        // Collect batch from queue
        std::vector<GPUWorkItem> batch;
        {
            std::lock_guard<std::mutex> lock(ctx->queue_mutex);
            while (!ctx->work_queue.empty() && batch.size() < batch_size_) {
                batch.push_back(ctx->work_queue.front());
                ctx->work_queue.pop();
                ctx->queue_depth--;
            }
        }

        if (!batch.empty()) {
            // Process batch
            processBatchOnGPU(gpu_id, batch);
        } else {
            // No work, sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Process Batch on GPU
// ────────────────────────────────────────────────────────────────────────────

void SSPMultiGPUDispatcher::processBatchOnGPU(int gpu_id, const std::vector<GPUWorkItem>& batch) {
    if (cudaSetDevice(gpu_id) != cudaSuccess) {
        return;
    }

    auto ctx = gpu_contexts_[gpu_id];

    // Prepare GPU batch
    std::vector<SSPSession> sessions;
    std::vector<const uint8_t*> plaintexts;
    std::vector<uint32_t> lengths;
    std::vector<uint8_t*> ciphertexts;
    std::vector<uint32_t> cipher_lens;
    std::vector<uint8_t*> tags;

    for (const auto& item : batch) {
        if (item.session) {
            sessions.push_back(*item.session);
            plaintexts.push_back(item.plaintext);
            lengths.push_back(item.plaintext_len);
            ciphertexts.push_back(item.ciphertext);
            cipher_lens.push_back(0);
            tags.push_back(item.tag);
        }
    }

    if (!sessions.empty()) {
        // Launch GPU batch
        if (ctx->gpu_ssp->encryptBatch(sessions, plaintexts, lengths, ciphertexts, cipher_lens, tags)) {
            // Process completions
            for (size_t i = 0; i < batch.size(); i++) {
                const auto& item = batch[i];
                if (item.on_complete) {
                    item.on_complete(item.request_id, item.ciphertext, cipher_lens[i], item.tag);
                }

                // Track latency
                auto elapsed = std::chrono::high_resolution_clock::now() - item.submit_time;
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
                ctx->total_latency_us += elapsed_us;
                ctx->requests_processed++;
            }

            total_requests_processed_ += batch.size();
            total_messages_processed_ += sessions.size();
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Select Best GPU
// ────────────────────────────────────────────────────────────────────────────

int SSPMultiGPUDispatcher::selectBestGPU() {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);

    // Find GPU with smallest queue depth
    int best_gpu = 0;
    uint32_t min_queue_depth = gpu_contexts_[0]->queue_depth;

    for (size_t i = 1; i < gpu_contexts_.size(); i++) {
        uint32_t depth = gpu_contexts_[i]->queue_depth;
        if (depth < min_queue_depth) {
            min_queue_depth = depth;
            best_gpu = i;
        }
    }

    return best_gpu;
}

// ────────────────────────────────────────────────────────────────────────────
// Submit Request
// ────────────────────────────────────────────────────────────────────────────

bool SSPMultiGPUDispatcher::submitRequest(
    const SSPSession& session,
    const uint8_t* plaintext,
    uint32_t plaintext_len,
    uint8_t* ciphertext,
    uint8_t* tag,
    uint32_t request_id,
    std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete)
{
    if (!plaintext || !ciphertext || !tag) return false;
    if (plaintext_len == 0) return false;

    // Select best GPU (lowest queue depth)
    int gpu_id = selectBestGPU();
    auto ctx = gpu_contexts_[gpu_id];

    // Create work item
    GPUWorkItem item;
    item.plaintext = plaintext;
    item.plaintext_len = plaintext_len;
    item.ciphertext = ciphertext;
    item.tag = tag;
    item.session = &session;
    item.request_id = request_id;
    item.on_complete = on_complete;
    item.submit_time = std::chrono::high_resolution_clock::now();

    // Add to GPU queue
    {
        std::lock_guard<std::mutex> lock(ctx->queue_mutex);
        ctx->work_queue.push(item);
        ctx->queue_depth++;
    }

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Statistics
// ────────────────────────────────────────────────────────────────────────────

std::vector<SSPMultiGPUDispatcher::GPUStats> SSPMultiGPUDispatcher::getGPUStats() const {
    std::vector<GPUStats> stats;

    for (const auto& ctx : gpu_contexts_) {
        GPUStats stat;
        stat.gpu_id = ctx->gpu_id;
        stat.requests_processed = ctx->requests_processed;
        stat.avg_latency_us = ctx->requests_processed > 0
            ? static_cast<double>(ctx->total_latency_us) / ctx->requests_processed
            : 0.0;
        stat.utilization_percent = ctx->utilization_percent;
        stat.queue_depth = ctx->queue_depth;
        stats.push_back(stat);
    }

    return stats;
}

double SSPMultiGPUDispatcher::getAverageLatency() const {
    if (total_requests_processed_ == 0) return 0.0;

    uint64_t total_latency = 0;
    for (const auto& ctx : gpu_contexts_) {
        total_latency += ctx->total_latency_us;
    }

    return static_cast<double>(total_latency) / total_requests_processed_;
}

double SSPMultiGPUDispatcher::getSystemUtilization() const {
    if (gpu_contexts_.empty()) return 0.0;

    double total_util = 0.0;
    for (const auto& ctx : gpu_contexts_) {
        total_util += ctx->utilization_percent;
    }

    return total_util / gpu_contexts_.size();
}

// ────────────────────────────────────────────────────────────────────────────
// Flush and Shutdown
// ────────────────────────────────────────────────────────────────────────────

void SSPMultiGPUDispatcher::flush() {
    // Wait for all queues to be empty
    bool all_empty = false;
    while (!all_empty) {
        all_empty = true;
        for (const auto& ctx : gpu_contexts_) {
            if (ctx->queue_depth > 0) {
                all_empty = false;
                break;
            }
        }
        if (!all_empty) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void SSPMultiGPUDispatcher::shutdown() {
    // Flush pending work
    flush();

    // Stop worker threads
    for (auto& ctx : gpu_contexts_) {
        ctx->worker_running = false;
        if (ctx->worker_thread.joinable()) {
            ctx->worker_thread.join();
        }

        // Cleanup GPU resources
        if (cudaSetDevice(ctx->gpu_id) == cudaSuccess) {
            if (ctx->cuda_stream) {
                cudaStreamDestroy(static_cast<cudaStream_t>(ctx->cuda_stream));
            }
            if (ctx->gpu_ssp) {
                ctx->gpu_ssp->shutdown();
            }
        }
    }

    gpu_contexts_.clear();
}

}  // namespace symm
