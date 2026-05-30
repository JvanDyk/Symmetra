/**
 * SSP Advanced Cache Layer Implementation — Phase 13
 */

#include "runtime/SSPCacheLayer.h"
#include <chrono>
#include <algorithm>
#include <functional>
#include <cstring>

namespace symm {

SSPCacheLayer::SSPCacheLayer(uint64_t l1_size_bytes, uint64_t l2_size_bytes)
    : l1_size_bytes_(l1_size_bytes),
      l1_used_bytes_(0),
      l2_size_bytes_(l2_size_bytes),
      l2_used_bytes_(0),
      compression_enabled_(true),
      deduplication_enabled_(true)
{
}

SSPCacheLayer::~SSPCacheLayer() = default;

// ────────────────────────────────────────────────────────────────────────────
// Get Cached (L1 → L2 fallback)
// ────────────────────────────────────────────────────────────────────────────

bool SSPCacheLayer::getCached(const uint8_t* plaintext, uint32_t plaintext_len,
                              std::vector<uint8_t>& ciphertext_out)
{
    total_requests_++;

    std::string hash = hashContent(plaintext, plaintext_len);

    // Check L1
    auto l1_it = l1_cache_.find(hash);
    if (l1_it != l1_cache_.end()) {
        l1_hits_++;
        ciphertext_out = l1_it->second.ciphertext;
        l1_it->second.last_access_us = getCurrentTimeUs();
        l1_it->second.access_count++;
        return true;
    }

    l1_misses_++;

    // Check L2
    auto l2_it = l2_cache_.find(hash);
    if (l2_it != l2_cache_.end()) {
        l2_hits_++;
        ciphertext_out = l2_it->second.ciphertext;
        l2_it->second.last_access_us = getCurrentTimeUs();
        l2_it->second.access_count++;

        // Promote to L1
        if (l1_used_bytes_ + l2_it->second.ciphertext.size() > l1_size_bytes_) {
            evictL1LRU();
        }
        l1_cache_[hash] = l2_it->second;
        l1_used_bytes_ += l2_it->second.ciphertext.size();

        return true;
    }

    l2_misses_++;
    return false;
}

// ────────────────────────────────────────────────────────────────────────────
// Put Cached (Add to L1, may promote to L2)
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::putCached(const uint8_t* plaintext, uint32_t plaintext_len,
                              const uint8_t* ciphertext, uint32_t ciphertext_len,
                              uint32_t ttl_seconds)
{
    std::string hash = hashContent(plaintext, plaintext_len);

    // Prepare entry
    CacheEntry entry;
    entry.content_hash = hash;
    entry.plaintext_size = plaintext_len;
    entry.timestamp_us = getCurrentTimeUs();
    entry.last_access_us = entry.timestamp_us;
    entry.access_count = 1;
    entry.ttl_seconds = ttl_seconds;
    entry.is_compressed = false;

    // Copy ciphertext (possibly compressed)
    entry.ciphertext.assign(ciphertext, ciphertext + ciphertext_len);
    entry.compressed_size = ciphertext_len;

    // Try compression for small payloads
    if (compression_enabled_ && ciphertext_len < 1024) {
        auto compressed = compressData(ciphertext, ciphertext_len);
        if (compressed.size() < ciphertext_len * 0.9) {  // Only if 10%+ compression
            entry.ciphertext = compressed;
            entry.compressed_size = compressed.size();
            entry.is_compressed = true;
        }
    }

    // Add to L1
    if (l1_used_bytes_ + entry.ciphertext.size() > l1_size_bytes_) {
        evictL1LRU();
    }

    l1_cache_[hash] = entry;
    l1_used_bytes_ += entry.ciphertext.size();

    // Also add to L2 if space
    if (l2_used_bytes_ + entry.ciphertext.size() <= l2_size_bytes_) {
        l2_cache_[hash] = entry;
        l2_used_bytes_ += entry.ciphertext.size();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Get Duplicate (Deduplication)
// ────────────────────────────────────────────────────────────────────────────

bool SSPCacheLayer::getDuplicate(const uint8_t* plaintext, uint32_t plaintext_len,
                                  std::vector<uint8_t>& ciphertext_out)
{
    if (!deduplication_enabled_) {
        return false;
    }

    // Detect exact duplicate by hashing plaintext
    std::string hash = hashContent(plaintext, plaintext_len);

    // Check both caches
    auto l1_it = l1_cache_.find(hash);
    if (l1_it != l1_cache_.end()) {
        ciphertext_out = l1_it->second.ciphertext;
        return true;
    }

    auto l2_it = l2_cache_.find(hash);
    if (l2_it != l2_cache_.end()) {
        ciphertext_out = l2_it->second.ciphertext;
        return true;
    }

    return false;
}

// ────────────────────────────────────────────────────────────────────────────
// Invalidate Cache Entry
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::invalidate(const uint8_t* plaintext, uint32_t plaintext_len) {
    std::string hash = hashContent(plaintext, plaintext_len);

    auto l1_it = l1_cache_.find(hash);
    if (l1_it != l1_cache_.end()) {
        l1_used_bytes_ -= l1_it->second.ciphertext.size();
        l1_cache_.erase(l1_it);
    }

    auto l2_it = l2_cache_.find(hash);
    if (l2_it != l2_cache_.end()) {
        l2_used_bytes_ -= l2_it->second.ciphertext.size();
        l2_cache_.erase(l2_it);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Clear All Caches
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::clearAll() {
    l1_cache_.clear();
    l1_used_bytes_ = 0;
    l2_cache_.clear();
    l2_used_bytes_ = 0;
}

// ────────────────────────────────────────────────────────────────────────────
// Set Compression Enabled
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::setCompressionEnabled(bool enabled) {
    compression_enabled_ = enabled;
}

// ────────────────────────────────────────────────────────────────────────────
// Set Deduplication Enabled
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::setDeduplicationEnabled(bool enabled) {
    deduplication_enabled_ = enabled;
}

// ────────────────────────────────────────────────────────────────────────────
// Cache Warming
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::warmCache(const std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>& entries) {
    for (const auto& entry : entries) {
        putCached(entry.first.data(), entry.first.size(),
                  entry.second.data(), entry.second.size(), 3600);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Get Statistics
// ────────────────────────────────────────────────────────────────────────────

CacheStats SSPCacheLayer::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    CacheStats stats = {};
    stats.l1_hits = l1_hits_;
    stats.l1_misses = l1_misses_;
    stats.l2_hits = l2_hits_;
    stats.l2_misses = l2_misses_;
    stats.total_requests = total_requests_;

    if (stats.total_requests > 0) {
        stats.l1_hit_rate = static_cast<double>(stats.l1_hits) / stats.total_requests * 100.0;
    }

    uint64_t l2_accesses = stats.l2_hits + stats.l2_misses;
    if (l2_accesses > 0) {
        stats.l2_hit_rate = static_cast<double>(stats.l2_hits) / l2_accesses * 100.0;
    }

    return stats;
}

// ────────────────────────────────────────────────────────────────────────────
// Set L1 Capacity
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::setL1Capacity(uint64_t bytes) {
    l1_size_bytes_ = bytes;
    while (l1_used_bytes_ > l1_size_bytes_ && !l1_cache_.empty()) {
        evictL1LRU();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Set L2 Capacity
// ────────────────────────────────────────────────────────────────────────────

void SSPCacheLayer::setL2Capacity(uint64_t bytes) {
    l2_size_bytes_ = bytes;
    while (l2_used_bytes_ > l2_size_bytes_ && !l2_cache_.empty()) {
        evictL2LRU();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Helper Methods
// ────────────────────────────────────────────────────────────────────────────

std::string SSPCacheLayer::hashContent(const uint8_t* data, uint32_t len) const {
    // Simple hash for demo (in production, use SHA256)
    std::hash<std::string> hasher;
    std::string str(reinterpret_cast<const char*>(data), len);
    return std::to_string(hasher(str));
}

std::vector<uint8_t> SSPCacheLayer::compressData(const uint8_t* data, uint32_t len) const {
    // Simple run-length encoding for demo (in production, use zstd/lz4)
    std::vector<uint8_t> compressed;
    compressed.reserve(len);

    for (uint32_t i = 0; i < len; i++) {
        uint32_t run_length = 1;
        while (i + run_length < len && data[i] == data[i + run_length] && run_length < 255) {
            run_length++;
        }

        if (run_length >= 3) {
            compressed.push_back(255);  // Marker
            compressed.push_back(run_length);
            compressed.push_back(data[i]);
            i += run_length - 1;
        } else {
            for (uint32_t j = 0; j < run_length; j++) {
                compressed.push_back(data[i]);
            }
            i += run_length - 1;
        }
    }

    return compressed;
}

std::vector<uint8_t> SSPCacheLayer::decompressData(const uint8_t* data, uint32_t len) const {
    // Simple run-length decoding
    std::vector<uint8_t> decompressed;
    decompressed.reserve(len * 2);

    for (uint32_t i = 0; i < len; i++) {
        if (data[i] == 255 && i + 2 < len) {
            uint32_t run_length = data[i + 1];
            uint8_t value = data[i + 2];
            for (uint32_t j = 0; j < run_length; j++) {
                decompressed.push_back(value);
            }
            i += 2;
        } else {
            decompressed.push_back(data[i]);
        }
    }

    return decompressed;
}

void SSPCacheLayer::evictL1LRU() {
    if (l1_cache_.empty()) return;

    auto lru_it = l1_cache_.begin();
    uint64_t min_access_time = lru_it->second.last_access_us;

    for (auto it = l1_cache_.begin(); it != l1_cache_.end(); ++it) {
        if (it->second.last_access_us < min_access_time) {
            min_access_time = it->second.last_access_us;
            lru_it = it;
        }
    }

    l1_used_bytes_ -= lru_it->second.ciphertext.size();
    l1_cache_.erase(lru_it);
}

void SSPCacheLayer::evictL2LRU() {
    if (l2_cache_.empty()) return;

    auto lru_it = l2_cache_.begin();
    uint64_t min_access_time = lru_it->second.last_access_us;

    for (auto it = l2_cache_.begin(); it != l2_cache_.end(); ++it) {
        if (it->second.last_access_us < min_access_time) {
            min_access_time = it->second.last_access_us;
            lru_it = it;
        }
    }

    l2_used_bytes_ -= lru_it->second.ciphertext.size();
    l2_cache_.erase(lru_it);
}

uint64_t SSPCacheLayer::getCurrentTimeUs() const {
    using namespace std::chrono;
    return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
}

}  // namespace symm
