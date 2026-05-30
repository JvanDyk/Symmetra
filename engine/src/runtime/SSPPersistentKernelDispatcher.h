/**
 * SSP Persistent Kernel Dispatcher — Phase 5
 *
 * Long-running GPU kernel with ring buffers for extreme-scale performance
 *
 * Architecture:
 * - Single persistent GPU kernel running continuously
 * - Ring buffer input/output queues (GPU-resident)
 * - Atomic signaling for completion notification
 * - Minimal host/device synchronization
 * - Expected improvement: 30x over Phase 3 (1.6M → 40-60M RPS)
 *
 * Ring Buffer Flow:
 * 1. Host writes requests to input ring buffer
 * 2. GPU kernel continuously processes from ring buffer
 * 3. GPU kernel writes results to output ring buffer
 * 4. Host reads completions from output ring buffer
 * 5. No host sync needed (atomic signals only)
 */

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <thread>
#include <cuda_runtime.h>
#include "runtime/SSPLayer.h"

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Ring Buffer Entry Structures (GPU-resident)
// ────────────────────────────────────────────────────────────────────────────

struct PersistentKernelInputEntry {
    uint8_t plaintext[8192];     // Message data
    uint32_t plaintext_len;       // Actual length
    uint32_t session_key_offset;  // Index into GPU session table
    uint32_t request_id;          // For tracking
    uint8_t nonce[12];           // Per-message nonce
};

struct PersistentKernelOutputEntry {
    uint8_t ciphertext[8192];     // Encrypted output
    uint32_t ciphertext_len;      // Actual length
    uint8_t tag[16];             // Authentication tag
    uint32_t request_id;          // Matches input request_id
    uint32_t status;              // 0=success, non-zero=error
};

struct PersistentKernelRingBuffer {
    static const uint32_t RING_SIZE = 4096;  // 4K entries per ring
    static const uint32_t MAX_CONCURRENT = 65536;  // Max in-flight requests

    PersistentKernelInputEntry* input_ring;
    PersistentKernelOutputEntry* output_ring;
    uint32_t* input_write_pos;   // GPU atomic
    uint32_t* output_read_pos;   // GPU atomic
    uint32_t* kernel_status;     // 0=idle, 1=running, 2=shutdown
    uint32_t* completion_count;  // Atomic counter
};

// ────────────────────────────────────────────────────────────────────────────
// Persistent Kernel Request (Host-side)
// ────────────────────────────────────────────────────────────────────────────

struct PersistentKernelRequest {
    const uint8_t* plaintext;
    uint32_t plaintext_len;
    uint8_t* ciphertext;
    uint8_t* tag;
    const SSPSession* session;
    uint32_t request_id;
    std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete;
};

// ────────────────────────────────────────────────────────────────────────────
// SSP Persistent Kernel Dispatcher (Phase 5)
// ────────────────────────────────────────────────────────────────────────────

class SSPPersistentKernelDispatcher {
public:
    SSPPersistentKernelDispatcher(uint32_t ring_buffer_size = 4096, uint32_t max_in_flight = 65536);
    ~SSPPersistentKernelDispatcher();

    // Initialize GPU with persistent kernel and ring buffers
    bool initialize();

    /**
     * Submit request for persistent kernel processing
     * Returns immediately; processing happens asynchronously on GPU
     */
    bool submitRequest(
        const SSPSession& session,
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint8_t* tag,
        uint32_t request_id,
        std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete
    );

    /**
     * Poll for completed requests (non-blocking)
     */
    uint32_t pollCompletions();

    /**
     * Wait for all pending requests to complete
     */
    void flush();

    /**
     * Graceful shutdown
     */
    void shutdown();

    // Statistics
    uint64_t getTotalRequestsSubmitted() const { return total_requests_submitted_; }
    uint64_t getTotalRequestsCompleted() const { return total_requests_completed_; }
    uint64_t getTotalMessagesProcessed() const { return total_messages_processed_; }
    double getAverageRequestLatency() const;
    double getAverageGPUUtilization() const;

private:
    uint32_t ring_buffer_size_;
    uint32_t max_in_flight_;

    std::unique_ptr<SSPLayer> ssp_;

    // GPU-resident ring buffers
    PersistentKernelRingBuffer* gpu_ring_buffer_;
    void* gpu_input_ring_;
    void* gpu_output_ring_;
    void* gpu_input_write_pos_;
    void* gpu_output_read_pos_;
    void* gpu_kernel_status_;
    void* gpu_completion_count_;

    // CPU-side tracking
    uint32_t cpu_input_write_pos_;
    uint32_t cpu_output_read_pos_;
    std::vector<SSPSession> gpu_session_table_;
    std::vector<PersistentKernelRequest> pending_requests_;

    // Host stream for async processing
    void* cuda_stream_;  // cudaStream_t
    std::thread completion_worker_;
    std::atomic<bool> worker_running_{false};

    // Statistics
    uint64_t total_requests_submitted_;
    uint64_t total_requests_completed_;
    uint64_t total_messages_processed_;

    // Helper methods
    void launchPersistentKernel();
    void completionWorkerThread();
    uint32_t getNextRequestId();
    void processPendingCompletions();
};

}  // namespace symm
