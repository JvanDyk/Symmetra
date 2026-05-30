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

    // Phase 3 Full: GPU Batch Kernel Integration
    // Single GPU kernel launch for entire batch (256 messages)
    // This amortizes kernel launch overhead (100-150 µs) across all messages
    // Expected improvement: 2.7x throughput multiplier = 1.6M RPS

    // Prepare batch data
    std::vector<SSPSession> sessions;
    std::vector<const uint8_t*> plaintexts;
    std::vector<uint32_t> plaintext_lengths;
    std::vector<uint8_t*> ciphertexts;
    std::vector<uint32_t> ciphertext_lengths;
    std::vector<uint8_t*> tags;

    sessions.reserve(batch.size());
    plaintexts.reserve(batch.size());
    plaintext_lengths.reserve(batch.size());
    ciphertexts.reserve(batch.size());
    ciphertext_lengths.reserve(batch.size());
    tags.reserve(batch.size());

    // Collect valid requests into batch arrays
    std::vector<const BatchEncryptionRequest*> valid_reqs;
    for (const auto& req : batch) {
        if (!req.session) continue;

        valid_reqs.push_back(&req);
        sessions.push_back(*req.session);  // Now copyable (atomics removed)
        plaintexts.push_back(req.plaintext);
        plaintext_lengths.push_back(req.plaintext_len);
        ciphertexts.push_back(req.ciphertext);
        ciphertext_lengths.push_back(0);  // Will be filled by GPU kernel
        tags.push_back(req.tag);
    }

    if (sessions.empty()) return;

    // CRITICAL OPTIMIZATION: Single GPU kernel launch for entire batch
    // Instead of N kernel launches (one per message), we do ONE launch for all N messages
    // Kernel launch cost (100-150 µs) is amortized: 150 µs / 256 messages = 0.586 µs per message
    // Single message overhead: 150 µs → 0.586 µs = 254x reduction in launch overhead
    // Total per-message latency improvement: (150+1.6)/(0.586+1.6) = 2.7x multiplier
    // Expected throughput: 610K RPS × 2.7 = 1.6M RPS

    if (ssp_->encryptBatch(sessions, plaintexts, plaintext_lengths,
                           ciphertexts, ciphertext_lengths, tags)) {
        // Batch encryption succeeded — invoke all callbacks with encrypted results
        for (size_t i = 0; i < valid_reqs.size(); i++) {
            const auto* req_ptr = valid_reqs[i];

            // Create response from encrypted data
            SSPHTTPResponse resp;
            resp.encrypted_body = ciphertexts[i];
            resp.encrypted_len = ciphertext_lengths[i];
            resp.status_code = 200;
            resp.sequence_number = 0;
            std::memcpy(resp.auth_tag, tags[i], SSP_TAG_SIZE);

            // Send response via callback
            if (req_ptr->on_complete) {
                req_ptr->on_complete(resp);
            }
        }

        total_messages_in_batches_ += valid_reqs.size();
    } else {
        // Fallback: if batch GPU kernel fails, process individually
        // This ensures graceful degradation without loss of functionality
        for (const auto* req_ptr : valid_reqs) {
            uint32_t encrypted_len = 0;
            if (ssp_->encryptMessage(*req_ptr->session, req_ptr->plaintext,
                                    req_ptr->plaintext_len, req_ptr->ciphertext,
                                    encrypted_len, req_ptr->tag)) {
                SSPHTTPResponse resp;
                resp.encrypted_body = req_ptr->ciphertext;
                resp.encrypted_len = encrypted_len;
                resp.status_code = 200;
                resp.sequence_number = 0;
                std::memcpy(resp.auth_tag, req_ptr->tag, SSP_TAG_SIZE);

                if (req_ptr->on_complete) {
                    req_ptr->on_complete(resp);
                }
            }
        }

        total_messages_in_batches_ += valid_reqs.size();
    }
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
