/**
 * SSP Stream Pipeline Dispatcher — Phase 4
 *
 * CUDA stream pipelining for overlapping GPU batch N with CPU batch N+1 preparation
 *
 * Architecture:
 * - Dual CUDA streams (stream1, stream2)
 * - GPU processes batch N while CPU prepares batch N+1
 * - Async batch launching with work queue
 * - Expected improvement: 3-5x over Phase 3 (1.6M → 2-3M RPS)
 */

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <functional>
#include "runtime/SSPLayer.h"

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Stream Pipeline Request
// ────────────────────────────────────────────────────────────────────────────

struct StreamPipelineRequest {
    const uint8_t* plaintext;
    uint32_t plaintext_len;
    uint8_t* ciphertext;
    uint8_t* tag;
    const SSPSession* session;
    int client_fd;
    std::function<void(const uint8_t*, uint32_t, const uint8_t*)> on_complete;
};

// ────────────────────────────────────────────────────────────────────────────
// Stream Pipeline Batch
// ────────────────────────────────────────────────────────────────────────────

struct StreamPipelineBatch {
    std::vector<SSPSession> sessions;
    std::vector<const uint8_t*> plaintexts;
    std::vector<uint32_t> plaintext_lengths;
    std::vector<uint8_t*> ciphertexts;
    std::vector<uint32_t> ciphertext_lengths;
    std::vector<uint8_t*> tags;
    std::vector<StreamPipelineRequest> original_requests;
};

// ────────────────────────────────────────────────────────────────────────────
// SSP Stream Pipeline Dispatcher (Phase 4)
// ────────────────────────────────────────────────────────────────────────────

class SSPStreamPipelineDispatcher {
public:
    SSPStreamPipelineDispatcher(uint32_t batch_size = 256, uint32_t num_streams = 2);
    ~SSPStreamPipelineDispatcher();

    // Initialize GPU acceleration with stream management
    bool initialize();

    /**
     * Submit request for pipelined encryption
     * Returns immediately; actual encryption happens asynchronously
     */
    bool submitRequest(
        const SSPSession& session,
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint8_t* tag,
        int client_fd,
        std::function<void(const uint8_t*, uint32_t, const uint8_t*)> on_complete
    );

    /**
     * Flush pending batches (for timeout or shutdown)
     */
    void flush();

    /**
     * Shutdown and cleanup GPU streams
     */
    void shutdown();

    // Statistics
    uint64_t getTotalRequestsProcessed() const { return total_requests_processed_; }
    uint64_t getTotalBatchesProcessed() const { return total_batches_processed_; }
    uint64_t getTotalPipelinedOverlaps() const { return total_pipelined_overlaps_; }
    double getAverageBatchSize() const;
    double getAveragePipelineEfficiency() const;

private:
    uint32_t batch_size_;
    uint32_t num_streams_;

    std::unique_ptr<SSPLayer> ssp_;
    std::mutex mutex_;
    std::mutex queue_mutex_;

    // Stream management
    struct StreamContext {
        void* cuda_stream;  // cudaStream_t
        bool busy;
        uint32_t batch_number;
        std::shared_ptr<StreamPipelineBatch> current_batch;
    };

    std::vector<StreamContext> stream_contexts_;
    uint32_t next_stream_idx_;

    // Request queue for async processing
    std::queue<StreamPipelineRequest> pending_requests_;
    std::vector<SSPSession> sessions_buffer_;

    // Worker thread for async batch launching
    std::thread batch_worker_;
    std::atomic<bool> worker_running_{false};

    // Statistics
    uint64_t total_requests_processed_;
    uint64_t total_batches_processed_;
    uint64_t total_pipelined_overlaps_;  // Times GPU and CPU worked in parallel
    uint64_t total_messages_processed_;

    // Helper methods
    void processBatchWorker();
    void launchBatchToStream(const StreamPipelineBatch& batch, uint32_t stream_idx);
    uint32_t getAvailableStreamIndex();
    void waitForStreamCompletion(uint32_t stream_idx);
};

}  // namespace symm
