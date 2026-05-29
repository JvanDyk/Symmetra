/**
 * SSP HTTP Dispatcher — Phase 3 Integration
 *
 * Encrypts/decrypts HTTP requests and responses using GPU-accelerated
 * ChaCha20-Poly1305 with dynamic batch collection.
 *
 * Architecture:
 * - Collects incoming requests into batches of 256
 * - Single GPU kernel launch per batch (amortizes overhead)
 * - Enables millions of RPS through batch collection
 */

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include "runtime/SSPLayer.h"

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// HTTP Request/Response with SSP Encryption
// ────────────────────────────────────────────────────────────────────────────

struct SSPHTTPRequest {
    uint8_t* encrypted_body;      // Encrypted HTTP body
    uint32_t encrypted_len;        // Length of encrypted data
    uint8_t auth_tag[SSP_TAG_SIZE]; // Poly1305 MAC for verification
    SSPSession session;             // Session context for this request
    int client_fd;                  // Socket for response
    uint32_t sequence_number;       // Request sequence
};

struct SSPHTTPResponse {
    uint8_t* encrypted_body;       // Encrypted HTTP response
    uint32_t encrypted_len;        // Length of encrypted response
    uint8_t auth_tag[SSP_TAG_SIZE]; // Poly1305 MAC for response
    int status_code;               // HTTP status
    uint32_t sequence_number;      // Response sequence
};

// ────────────────────────────────────────────────────────────────────────────
// Batch Encryption Request
// ────────────────────────────────────────────────────────────────────────────

struct BatchEncryptionRequest {
    const uint8_t* plaintext;
    uint32_t plaintext_len;
    uint8_t* ciphertext;
    uint8_t* tag;
    const SSPSession* session;  // Use pointer to avoid copying (SSPSession has atomic members)
    int client_fd;
    std::function<void(const SSPHTTPResponse&)> on_complete;
};

// ────────────────────────────────────────────────────────────────────────────
// SSP HTTP Batch Dispatcher
// ────────────────────────────────────────────────────────────────────────────

class SSPHTTPDispatcher {
public:
    SSPHTTPDispatcher(uint32_t batch_size = 256, uint32_t timeout_ms = 5000);
    ~SSPHTTPDispatcher();

    // Initialize GPU acceleration
    bool initialize();

    /**
     * Encrypt HTTP request body
     * Auto-flushes when batch is full
     */
    bool encryptRequest(
        const SSPSession& session,
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint32_t& ciphertext_len,
        uint8_t* tag,
        int client_fd,
        std::function<void(const SSPHTTPResponse&)> on_complete
    );

    /**
     * Decrypt HTTP request with verification
     */
    bool decryptRequest(
        const SSPSession& session,
        const uint8_t* ciphertext,
        uint32_t ciphertext_len,
        const uint8_t* tag,
        uint8_t* plaintext,
        uint32_t& plaintext_len
    );

    /**
     * Encrypt HTTP response body
     */
    bool encryptResponse(
        const SSPSession& session,
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint32_t& ciphertext_len,
        uint8_t* tag
    );

    /**
     * Flush any pending batches (for timeout or shutdown)
     */
    void flush();

    /**
     * Shutdown and cleanup
     */
    void shutdown();

    // Statistics
    uint64_t getTotalRequestsEncrypted() const { return total_requests_encrypted_; }
    uint64_t getTotalBatchesProcessed() const { return total_batches_processed_; }
    double getAverageBatchSize() const;

private:
    uint32_t batch_size_;
    uint32_t timeout_ms_;

    std::unique_ptr<SSPLayer> ssp_;
    std::mutex mutex_;

    // Batch accumulation
    std::vector<BatchEncryptionRequest> pending_requests_;
    uint64_t batch_start_ms_;  // Timestamp in milliseconds

    // Statistics
    uint64_t total_requests_encrypted_;
    uint64_t total_batches_processed_;
    uint64_t total_messages_in_batches_;

    // Helper methods
    void processPendingBatch();
    void launchGPUBatch(const std::vector<BatchEncryptionRequest>& batch);
    bool shouldFlush() const;
};

}  // namespace symm
