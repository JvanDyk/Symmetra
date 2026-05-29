/**
 * Symmetra Security Protocol (SSP) Layer
 *
 * High-performance encryption/decryption layer for Symmetra
 * Uses GPU-accelerated ChaCha20-Poly1305 for throughput optimization
 *
 * Design:
 * - Persistent GPU buffers (ring buffer, no PCIe overhead per request)
 * - Batch processing (256+ requests per GPU kernel launch)
 * - Zero-copy architecture (direct GPU memory access)
 * - ChaCha20-Poly1305 (proven, GPU-friendly AEAD cipher)
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constants
// ────────────────────────────────────────────────────────────────────────────

constexpr uint32_t SSP_MAX_BATCH_SIZE = 512;
constexpr uint32_t SSP_MAX_MESSAGE_SIZE = 8192;
constexpr uint32_t SSP_KEY_SIZE = 32;
constexpr uint32_t SSP_NONCE_SIZE = 12;
constexpr uint32_t SSP_TAG_SIZE = 16;

// SSP Protocol Frame Header (16 bytes)
struct SSPHeader {
    uint32_t message_length;   // Bytes 0-3: Length of encrypted payload
    uint32_t sequence_number;  // Bytes 4-7: Message sequence counter
    uint64_t timestamp;        // Bytes 8-15: Request timestamp (for replay detection)
};

constexpr uint32_t SSP_HEADER_SIZE = sizeof(SSPHeader);

// ────────────────────────────────────────────────────────────────────────────
// GPU Buffer Management
// ────────────────────────────────────────────────────────────────────────────

/**
 * Persistent GPU buffer for batch encryption
 * Allocated once, reused for all batches
 * Minimizes PCIe transfer overhead with pinned memory and zero-copy access
 *
 * Day 3 Optimizations:
 * - Pinned host memory: direct GPU access without staging
 * - CUDA Unified Virtual Addressing: transparent memory mapping
 * - Persistent ring buffers: no allocation/deallocation per batch
 * - Async operations: GPU and CPU work in parallel
 */
class GPUBatchBuffer {
public:
    GPUBatchBuffer(uint32_t batch_size = SSP_MAX_BATCH_SIZE);
    ~GPUBatchBuffer();

    // Allocate GPU memory and pinned host buffers
    bool allocate();

    // Get pointers to GPU memory (for kernel launch)
    uint8_t* getPlaintexts() { return d_plaintexts; }
    uint8_t* getCiphertexts() { return d_ciphertexts; }
    uint8_t* getKeys() { return d_keys; }
    uint8_t* getNonces() { return d_nonces; }
    uint32_t* getLengths() { return d_lengths; }
    uint8_t* getTags() { return d_tags; }

    // Get pinned host buffers for zero-copy access
    uint8_t* getPinnedPlaintexts() { return h_plaintexts_pinned; }
    uint8_t* getPinnedCiphertexts() { return h_ciphertexts_pinned; }
    uint8_t* getPinnedKeys() { return h_keys_pinned; }
    uint8_t* getPinnedNonces() { return h_nonces_pinned; }
    uint32_t* getPinnedLengths() { return h_lengths_pinned; }
    uint8_t* getPinnedTags() { return h_tags_pinned; }

    // Copy host → GPU (async) - for non-pinned data
    void copyToGPU(const uint8_t* plaintexts, const uint8_t* keys,
                   const uint8_t* nonces, const uint32_t* lengths,
                   uint32_t batch_size);

    // Copy GPU → host (async) - for non-pinned data
    void copyFromGPU(uint8_t* ciphertexts, uint8_t* tags, uint32_t batch_size);

    // Synchronize (wait for GPU operations)
    void synchronize();

    // Zero-copy methods: work directly with pinned memory
    void encryptWithPinnedBuffers(uint32_t batch_size);
    void decryptWithPinnedBuffers(uint32_t batch_size);

    uint32_t getBatchSize() const { return batch_size_; }
    bool isUVASupported() const { return uva_supported_; }

private:
    uint32_t batch_size_;
    bool uva_supported_;

    // Device pointers (GPU memory)
    uint8_t* d_plaintexts;
    uint8_t* d_ciphertexts;
    uint8_t* d_keys;
    uint8_t* d_nonces;
    uint32_t* d_lengths;
    uint8_t* d_tags;

    // Pinned host memory (accessible directly from GPU via UVA)
    uint8_t* h_plaintexts_pinned;
    uint8_t* h_ciphertexts_pinned;
    uint8_t* h_keys_pinned;
    uint8_t* h_nonces_pinned;
    uint32_t* h_lengths_pinned;
    uint8_t* h_tags_pinned;

    // CUDA stream for async operations
    void* stream_;  // cudaStream_t
    void* stream2_; // Second stream for pipelining

    // Helper: check UVA support
    bool checkUVASupport();
};

// ────────────────────────────────────────────────────────────────────────────
// Session Management
// ────────────────────────────────────────────────────────────────────────────

/**
 * SSP Session - one per client connection
 * Manages encryption keys and sequence numbers
 */
struct SSPSession {
    uint8_t session_key[SSP_KEY_SIZE];           // Derived from ECDHE
    uint32_t sequence_number = 0;                // Incremented per message
    uint64_t creation_time = 0;                  // For session timeout
    std::atomic<uint32_t> message_count{0};      // Diagnostics
    std::atomic<uint64_t> bytes_encrypted{0};    // Metrics
};

// ────────────────────────────────────────────────────────────────────────────
// Batch Accumulator (Phase 3 Optimization)
// ────────────────────────────────────────────────────────────────────────────

/**
 * Batch accumulator for collecting requests before GPU kernel launch
 * Amortizes kernel launch overhead across multiple messages
 *
 * Phase 3: Collects requests until batch is full or flush is called
 */
template<typename T>
class BatchAccumulator {
public:
    explicit BatchAccumulator(uint32_t max_batch_size = 256)
        : max_batch_size_(max_batch_size) {
        items_.reserve(max_batch_size);
    }

    void add(const T& item) {
        items_.push_back(item);
        if (items_.size() >= max_batch_size_) {
            // Auto-flush when full
            flush();
        }
    }

    void flush() {
        if (!items_.empty() && on_flush_) {
            on_flush_(items_);
            items_.clear();
        }
    }

    bool is_full() const {
        return items_.size() >= max_batch_size_;
    }

    uint32_t size() const {
        return items_.size();
    }

    // Callback for flush event
    std::function<void(const std::vector<T>&)> on_flush_;

private:
    uint32_t max_batch_size_;
    std::vector<T> items_;
};

// ────────────────────────────────────────────────────────────────────────────
// SSP Layer
// ────────────────────────────────────────────────────────────────────────────

/**
 * Main Symmetra Security Protocol layer
 * Manages encryption/decryption, session keys, batching
 */
class SSPLayer {
public:
    SSPLayer();
    ~SSPLayer();

    // Initialize GPU acceleration
    bool initialize();

    // Per-connection initialization (ECDHE handshake, session establishment)
    bool establishSession(void* client_fd, SSPSession& session);

    // Encrypt single message
    bool encryptMessage(
        const SSPSession& session,
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint32_t& ciphertext_len,
        uint8_t* tag
    );

    // Decrypt single message with verification
    bool decryptMessage(
        const SSPSession& session,
        const uint8_t* ciphertext,
        uint32_t ciphertext_len,
        const uint8_t* tag,
        uint8_t* plaintext,
        uint32_t& plaintext_len
    );

    // Batch encryption (for high throughput)
    bool encryptBatch(
        const std::vector<SSPSession>& sessions,
        const std::vector<const uint8_t*>& plaintexts,
        const std::vector<uint32_t>& plaintext_lengths,
        std::vector<uint8_t*>& ciphertexts,
        std::vector<uint32_t>& ciphertext_lengths,
        std::vector<uint8_t*>& tags
    );

    // Batch decryption with verification
    bool decryptBatch(
        const std::vector<SSPSession>& sessions,
        const std::vector<const uint8_t*>& ciphertexts,
        const std::vector<uint32_t>& ciphertext_lengths,
        const std::vector<const uint8_t*>& tags,
        std::vector<uint8_t*>& plaintexts,
        std::vector<uint32_t>& plaintext_lengths,
        std::vector<bool>& verify_results
    );

    // Shutdown GPU
    void shutdown();

    // Diagnostics
    void printStats() const;
    float getBenchmarkThroughput() const;

private:
    // GPU buffer management
    std::unique_ptr<GPUBatchBuffer> gpu_buffer_;

    // Phase 3: Batch accumulation for kernel launch optimization
    struct EncryptRequest {
        const SSPSession* session;
        const uint8_t* plaintext;
        uint32_t plaintext_len;
        uint8_t* ciphertext;
        uint32_t* ciphertext_len;
        uint8_t* tag;
    };

    struct DecryptRequest {
        const SSPSession* session;
        const uint8_t* ciphertext;
        uint32_t ciphertext_len;
        const uint8_t* tag;
        uint8_t* plaintext;
        uint32_t* plaintext_len;
    };

    // Session storage
    std::unordered_map<uint64_t, SSPSession> sessions_;
    std::mutex sessions_mutex_;

    // Metrics
    std::atomic<uint64_t> total_messages_encrypted{0};
    std::atomic<uint64_t> total_messages_decrypted{0};
    std::atomic<uint64_t> total_bytes_encrypted{0};
    std::atomic<uint64_t> total_bytes_decrypted{0};

    // GPU info
    bool gpu_initialized_ = false;
    int cuda_device_ = -1;

    // Helper functions
    bool initializeGPU();
    void deriveSessionKey(const uint8_t* shared_secret, uint8_t* session_key);
    void incrementSequenceNumber(SSPSession& session);
};

}  // namespace symm
