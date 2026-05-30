/**
 * SSP HTTP Dispatcher Implementation — Phase 3
 *
 * Integrates batch collection with HTTP request/response encryption
 */

#include "runtime/SSPHTTPDispatcher.h"
#include <chrono>
#include <algorithm>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ────────────────────────────────────────────────────────────────────────────

SSPHTTPDispatcher::SSPHTTPDispatcher(uint32_t batch_size, uint32_t timeout_ms)
    : batch_size_(batch_size),
      timeout_ms_(timeout_ms),
      batch_start_ms_(0),
      total_requests_encrypted_(0),
      total_batches_processed_(0),
      total_messages_in_batches_(0)
{
    ssp_ = std::make_unique<SSPLayer>();
    pending_requests_.reserve(batch_size);
    batch_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

SSPHTTPDispatcher::~SSPHTTPDispatcher() {
    flush();
    if (ssp_) {
        ssp_->shutdown();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Initialization
// ────────────────────────────────────────────────────────────────────────────

bool SSPHTTPDispatcher::initialize() {
    if (!ssp_) return false;
    return ssp_->initialize();
}

// ────────────────────────────────────────────────────────────────────────────
// Encrypt Request with Batch Collection
// ────────────────────────────────────────────────────────────────────────────

bool SSPHTTPDispatcher::encryptRequest(
    const SSPSession& session,
    const uint8_t* plaintext,
    uint32_t plaintext_len,
    uint8_t* ciphertext,
    uint32_t& ciphertext_len,
    uint8_t* tag,
    int client_fd,
    std::function<void(const SSPHTTPResponse&)> on_complete)
{
    if (!plaintext || !ciphertext || !tag) return false;
    if (plaintext_len > SSP_MAX_MESSAGE_SIZE) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Create batch request (use pointer to avoid copying SSPSession with atomic members)
    BatchEncryptionRequest req;
    req.plaintext = plaintext;
    req.plaintext_len = plaintext_len;
    req.ciphertext = ciphertext;
    req.tag = tag;
    req.session = &session;  // Use pointer
    req.client_fd = client_fd;
    req.on_complete = on_complete;

    // Add to pending batch
    pending_requests_.push_back(req);

    // Check if batch is full
    if (pending_requests_.size() >= batch_size_) {
        processPendingBatch();
    } else {
        // Check timeout
        uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t elapsed_ms = now_ms - batch_start_ms_;

        if (elapsed_ms > timeout_ms_) {
            // Timeout — flush batch even if not full
            processPendingBatch();
        }
    }

    total_requests_encrypted_++;
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Decrypt Request with Verification
// ────────────────────────────────────────────────────────────────────────────

bool SSPHTTPDispatcher::decryptRequest(
    const SSPSession& session,
    const uint8_t* ciphertext,
    uint32_t ciphertext_len,
    const uint8_t* tag,
    uint8_t* plaintext,
    uint32_t& plaintext_len)
{
    if (!ssp_) return false;
    return ssp_->decryptMessage(session, ciphertext, ciphertext_len, tag, plaintext, plaintext_len);
}

// ────────────────────────────────────────────────────────────────────────────
// Encrypt Response
// ────────────────────────────────────────────────────────────────────────────

bool SSPHTTPDispatcher::encryptResponse(
    const SSPSession& session,
    const uint8_t* plaintext,
    uint32_t plaintext_len,
    uint8_t* ciphertext,
    uint32_t& ciphertext_len,
    uint8_t* tag)
{
    if (!ssp_) return false;
    return ssp_->encryptMessage(session, plaintext, plaintext_len, ciphertext, ciphertext_len, tag);
}

// ────────────────────────────────────────────────────────────────────────────
// Process Pending Batch
// ────────────────────────────────────────────────────────────────────────────

void SSPHTTPDispatcher::processPendingBatch() {
    if (pending_requests_.empty()) return;

    // Launch GPU batch
    launchGPUBatch(pending_requests_);

    // Reset for next batch
    pending_requests_.clear();
    total_batches_processed_++;
    batch_start_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ────────────────────────────────────────────────────────────────────────────
// Launch GPU Batch
// ────────────────────────────────────────────────────────────────────────────

void SSPHTTPDispatcher::launchGPUBatch(const std::vector<BatchEncryptionRequest>& batch) {
    if (batch.empty() || !ssp_) return;

    // Phase 3 MVP: Batch collection infrastructure working
    // Requests are collected and processed together
    // Full Phase 3: Would launch single GPU kernel for entire batch
    //
    // Limitation: SSPSession has atomic members preventing copies for batch GPU kernel
    // Solution: Refactor SSPLayer to support batch with copyable session keys
    // Next step: Implement SSPLayerBatch with SSPSessionKeys for true batch GPU processing

    // Process the batch of requests
    // Each request flows through GPU encryption (currently per-message)
    // Infrastructure: batch collection, timeout-based flushing, callback-based responses

    size_t processed = 0;
    for (const auto& req : batch) {
        if (!req.session) continue;

        // Encrypt request through GPU
        uint32_t encrypted_len = 0;
        if (ssp_->encryptMessage(*req.session, req.plaintext, req.plaintext_len,
                                req.ciphertext, encrypted_len, req.tag)) {
            // Create response
            SSPHTTPResponse resp;
            resp.encrypted_body = req.ciphertext;
            resp.encrypted_len = encrypted_len;
            resp.status_code = 200;
            resp.sequence_number = 0;
            std::memcpy(resp.auth_tag, req.tag, SSP_TAG_SIZE);

            // Send response via callback
            if (req.on_complete) {
                req.on_complete(resp);
            }

            processed++;
        }
    }

    total_messages_in_batches_ += processed;
}

// ────────────────────────────────────────────────────────────────────────────
// Flush Pending Batches
// ────────────────────────────────────────────────────────────────────────────

void SSPHTTPDispatcher::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    processPendingBatch();
}

// ────────────────────────────────────────────────────────────────────────────
// Shutdown
// ────────────────────────────────────────────────────────────────────────────

void SSPHTTPDispatcher::shutdown() {
    flush();
    if (ssp_) {
        ssp_->shutdown();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Statistics
// ────────────────────────────────────────────────────────────────────────────

double SSPHTTPDispatcher::getAverageBatchSize() const {
    if (total_batches_processed_ == 0) return 0.0;
    return static_cast<double>(total_messages_in_batches_) / total_batches_processed_;
}

// ────────────────────────────────────────────────────────────────────────────
// Helper: Should Flush?
// ────────────────────────────────────────────────────────────────────────────

bool SSPHTTPDispatcher::shouldFlush() const {
    if (pending_requests_.empty()) return false;

    // Flush if batch is full
    if (pending_requests_.size() >= batch_size_) return true;

    // Flush if timeout elapsed
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t elapsed_ms = now_ms - batch_start_ms_;

    return elapsed_ms > timeout_ms_;
}

}  // namespace symm
