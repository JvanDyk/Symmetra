#pragma once
/**
 * ThreadLocalBatchBuffer — Per-thread request accumulation (ZERO synchronization)
 *
 * Design:
 * - Each accept/worker thread gets one buffer (thread_local storage)
 * - Buffer accumulates requests with no locks or atomics
 * - When full or timeout, flushes to a dispatcher shard
 * - Enables 100K+ submission RPS with zero contention
 *
 * Lifetime:
 * - Thread constructs it on first use (thread_local magic)
 * - Buffer lives for thread duration
 * - On flush: requests move to dispatcher, buffer resets
 */

#include <vector>
#include <chrono>
#include <functional>
#include "BatchScheduler.h"

namespace symm {

class ThreadLocalBatchBuffer {
public:
    ThreadLocalBatchBuffer(
        int preferredBatchSize = 256,
        int minBatchSize = 32,
        int timeoutMs = 10,
        std::function<void(std::vector<BatchRequest>&&)> onFlush = nullptr
    )
        : _preferredBatchSize(preferredBatchSize),
          _minBatchSize(minBatchSize),
          _timeoutMs(timeoutMs),
          _onFlush(onFlush),
          _lastFlushUs(nowUs())
    {
        _buffer.reserve(preferredBatchSize);
    }

    // FAST PATH: Accumulate request (NO synchronization, NO allocation after reserve)
    // Returns true if buffer is full and needs flush
    bool submitRequest(const BatchRequest& req) {
        _buffer.push_back(req);
        return (int)_buffer.size() >= _preferredBatchSize;
    }

    // Flush if size reached OR timeout expired
    // Non-blocking: returns immediately
    bool shouldFlush() const {
        if ((int)_buffer.size() >= _preferredBatchSize) {
            return true;  // Size threshold reached
        }
        if (_buffer.empty()) {
            return false;  // Nothing to flush
        }
        int64_t now = nowUs();
        int64_t elapsed = (now - _lastFlushUs) / 1000;  // Convert to ms
        return elapsed >= _timeoutMs;
    }

    // Flush current buffer (caller is responsible for batching logic)
    std::vector<BatchRequest> flush() {
        std::vector<BatchRequest> batch = std::move(_buffer);
        _buffer.clear();
        _buffer.reserve(_preferredBatchSize);
        _lastFlushUs = nowUs();
        return batch;
    }

    int size() const { return (int)_buffer.size(); }
    bool empty() const { return _buffer.empty(); }

    // Manual flush trigger (for testing/debugging)
    void forceFlush() {
        if (!_buffer.empty() && _onFlush) {
            _onFlush(flush());
        }
    }

private:
    int _preferredBatchSize;
    int _minBatchSize;
    int _timeoutMs;
    std::function<void(std::vector<BatchRequest>&&)> _onFlush;
    std::vector<BatchRequest> _buffer;
    int64_t _lastFlushUs;

    int64_t nowUs() const {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()
        ).count();
    }
};

// Thread-local accessor (one buffer per thread)
inline ThreadLocalBatchBuffer& getLocalBatchBuffer(
    int preferredBatchSize = 256,
    int timeoutMs = 10,
    std::function<void(std::vector<BatchRequest>&&)> onFlush = nullptr
) {
    thread_local ThreadLocalBatchBuffer buffer(
        preferredBatchSize, 32, timeoutMs, onFlush
    );
    return buffer;
}

}  // namespace symm
