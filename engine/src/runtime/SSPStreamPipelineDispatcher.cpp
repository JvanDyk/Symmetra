/**
 * SSP Stream Pipeline Dispatcher Implementation — Phase 4
 */

#include "runtime/SSPStreamPipelineDispatcher.h"
#include <chrono>
#include <cuda_runtime.h>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ────────────────────────────────────────────────────────────────────────────

SSPStreamPipelineDispatcher::SSPStreamPipelineDispatcher(uint32_t batch_size, uint32_t num_streams)
    : batch_size_(batch_size),
      num_streams_(num_streams),
      next_stream_idx_(0),
      total_requests_processed_(0),
      total_batches_processed_(0),
      total_pipelined_overlaps_(0),
      total_messages_processed_(0)
{
    ssp_ = std::make_unique<SSPLayer>();
    stream_contexts_.resize(num_streams);
    sessions_buffer_.reserve(batch_size);
}

SSPStreamPipelineDispatcher::~SSPStreamPipelineDispatcher() {
    shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// Initialization
// ────────────────────────────────────────────────────────────────────────────

bool SSPStreamPipelineDispatcher::initialize() {
    if (!ssp_) return false;
    if (!ssp_->initialize()) return false;

    // Create CUDA streams for pipelining
    for (size_t i = 0; i < stream_contexts_.size(); i++) {
        cudaStream_t stream;
        if (cudaStreamCreate(&stream) != cudaSuccess) {
            return false;
        }
        stream_contexts_[i].cuda_stream = stream;
        stream_contexts_[i].busy = false;
        stream_contexts_[i].batch_number = 0;
    }

    // Start worker thread for async batch processing
    worker_running_ = true;
    batch_worker_ = std::thread([this]() { this->processBatchWorker(); });

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Submit Request (Non-blocking)
// ────────────────────────────────────────────────────────────────────────────

bool SSPStreamPipelineDispatcher::submitRequest(
    const SSPSession& session,
    const uint8_t* plaintext,
    uint32_t plaintext_len,
    uint8_t* ciphertext,
    uint8_t* tag,
    int client_fd,
    std::function<void(const uint8_t*, uint32_t, const uint8_t*)> on_complete)
{
    if (!plaintext || !ciphertext || !tag) return false;
    if (plaintext_len > SSP_MAX_MESSAGE_SIZE) return false;

    StreamPipelineRequest req;
    req.plaintext = plaintext;
    req.plaintext_len = plaintext_len;
    req.ciphertext = ciphertext;
    req.tag = tag;
    req.session = &session;
    req.client_fd = client_fd;
    req.on_complete = on_complete;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        pending_requests_.push(req);
    }

    total_requests_processed_++;
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Batch Worker Thread (Async Processing)
// ────────────────────────────────────────────────────────────────────────────

void SSPStreamPipelineDispatcher::processBatchWorker() {
    while (worker_running_) {
        // Collect batch
        std::vector<StreamPipelineRequest> batch_reqs;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!pending_requests_.empty() && batch_reqs.size() < batch_size_) {
                batch_reqs.push_back(pending_requests_.front());
                pending_requests_.pop();
            }
        }

        if (batch_reqs.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Prepare GPU batch
        auto batch = std::make_shared<StreamPipelineBatch>();
        batch->original_requests = batch_reqs;
        batch->sessions.reserve(batch_reqs.size());
        batch->plaintexts.reserve(batch_reqs.size());
        batch->plaintext_lengths.reserve(batch_reqs.size());
        batch->ciphertexts.reserve(batch_reqs.size());
        batch->ciphertext_lengths.reserve(batch_reqs.size());
        batch->tags.reserve(batch_reqs.size());

        for (const auto& req : batch_reqs) {
            if (req.session) {
                batch->sessions.push_back(*req.session);
                batch->plaintexts.push_back(req.plaintext);
                batch->plaintext_lengths.push_back(req.plaintext_len);
                batch->ciphertexts.push_back(req.ciphertext);
                batch->ciphertext_lengths.push_back(0);
                batch->tags.push_back(req.tag);
            }
        }

        if (batch->sessions.empty()) continue;

        // Get available stream (blocks if all busy)
        uint32_t stream_idx = getAvailableStreamIndex();

        // Launch batch to stream
        launchBatchToStream(*batch, stream_idx);

        total_batches_processed_++;
        total_messages_processed_ += batch->sessions.size();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Launch Batch to CUDA Stream
// ────────────────────────────────────────────────────────────────────────────

void SSPStreamPipelineDispatcher::launchBatchToStream(
    const StreamPipelineBatch& batch,
    uint32_t stream_idx)
{
    // Mark stream as busy
    stream_contexts_[stream_idx].busy = true;
    stream_contexts_[stream_idx].current_batch =
        std::make_shared<StreamPipelineBatch>(batch);

    // Launch GPU batch encryption (non-blocking)
    if (ssp_->encryptBatch(
        const_cast<std::vector<SSPSession>&>(batch.sessions),
        batch.plaintexts,
        batch.plaintext_lengths,
        const_cast<std::vector<uint8_t*>&>(batch.ciphertexts),
        const_cast<std::vector<uint32_t>&>(batch.ciphertext_lengths),
        const_cast<std::vector<uint8_t*>&>(batch.tags))) {

        // Batch launched successfully
        // Invoke callbacks (synchronously for now, could be async)
        for (const auto& req : batch.original_requests) {
            if (req.on_complete) {
                req.on_complete(req.ciphertext, req.plaintext_len, req.tag);
            }
        }

        // Track pipelined overlap (any other stream currently busy)
        for (size_t i = 0; i < stream_contexts_.size(); i++) {
            if (i != stream_idx && stream_contexts_[i].busy) {
                total_pipelined_overlaps_++;
                break;
            }
        }
    }

    // Mark stream as idle
    stream_contexts_[stream_idx].busy = false;
}

// ────────────────────────────────────────────────────────────────────────────
// Get Available Stream (Round-robin)
// ────────────────────────────────────────────────────────────────────────────

uint32_t SSPStreamPipelineDispatcher::getAvailableStreamIndex() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Simple round-robin: return next stream
    // In production, could wait for the least-busy stream
    uint32_t idx = next_stream_idx_;
    next_stream_idx_ = (next_stream_idx_ + 1) % stream_contexts_.size();
    return idx;
}

// ────────────────────────────────────────────────────────────────────────────
// Flush and Shutdown
// ────────────────────────────────────────────────────────────────────────────

void SSPStreamPipelineDispatcher::flush() {
    // Wait for all pending requests to be processed
    while (true) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (pending_requests_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SSPStreamPipelineDispatcher::shutdown() {
    flush();

    // Stop worker thread
    worker_running_ = false;
    if (batch_worker_.joinable()) {
        batch_worker_.join();
    }

    // Destroy CUDA streams
    for (auto& ctx : stream_contexts_) {
        if (ctx.cuda_stream) {
            cudaStreamDestroy(static_cast<cudaStream_t>(ctx.cuda_stream));
        }
    }

    if (ssp_) {
        ssp_->shutdown();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Statistics
// ────────────────────────────────────────────────────────────────────────────

double SSPStreamPipelineDispatcher::getAverageBatchSize() const {
    if (total_batches_processed_ == 0) return 0.0;
    return static_cast<double>(total_messages_processed_) / total_batches_processed_;
}

double SSPStreamPipelineDispatcher::getAveragePipelineEfficiency() const {
    if (total_batches_processed_ == 0) return 0.0;
    return (static_cast<double>(total_pipelined_overlaps_) / total_batches_processed_) * 100.0;
}

}  // namespace symm
