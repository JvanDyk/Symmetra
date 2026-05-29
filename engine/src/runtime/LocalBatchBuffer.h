#pragma once
/**
 * LocalBatchBuffer — High-performance per-thread request batching
 *
 * Instead of all workers contending on a central queue, each worker:
 * 1. Accumulates requests in a thread-local buffer (no synchronization)
 * 2. When buffer reaches preferredBatchSize, posts to AsyncBatchLauncher
 * 3. On timeout or explicit flush, posts remaining requests
 *
 * This eliminates lock-free queue contention entirely. With 256 workers
 * and 256-request batches, we get 256 independent batch pipelines.
 */

#include <vector>
#include <thread>
#include <chrono>
#include "BatchScheduler.h"  // For BatchRequest, BatchResult
#include "AsyncBatchLauncher.h"

namespace symm {

class LocalBatchBuffer {
public:
    LocalBatchBuffer(int preferredBatchSize = 256, int minBatchSize = 32)
        : _preferredBatchSize(preferredBatchSize),
          _minBatchSize(minBatchSize),
          _buffer(),
          _lastFlushUs(nowUs())
    {
        _buffer.reserve(preferredBatchSize);
    }

    // Non-blocking submit — returns immediately, may batch request locally
    void submitRequest(const BatchRequest& req) {
        _buffer.push_back(req);

        // Flush if we've reached preferred size
        if ((int)_buffer.size() >= _preferredBatchSize) {
            flush();
        }
    }

    // Submit and potentially flush (periodic call, e.g. every 10ms)
    // Returns true if a batch was flushed
    bool flushIfTimeout(int timeoutMs = 10) {
        int64_t now = nowUs();
        if (now - _lastFlushUs >= timeoutMs * 1000LL) {
            if (!_buffer.empty()) {
                flush();
                return true;
            }
            _lastFlushUs = now;
        }
        return false;
    }

    // Explicit flush — posts whatever is queued
    void flush() {
        if (_buffer.empty()) return;

        // Pad to minimum batch size if needed
        int paddedSize = (_buffer.size() < _minBatchSize)
            ? _minBatchSize
            : _buffer.size();

        while (_buffer.size() < (size_t)paddedSize) {
            BatchRequest dummy;
            dummy.fd = -1;  // Dummy request
            _buffer.push_back(dummy);
        }

        // In production, post to AsyncBatchLauncher here
        // For now, this is a placeholder
        _buffer.clear();
        _lastFlushUs = nowUs();
    }

    int size() const { return (int)_buffer.size(); }
    bool empty() const { return _buffer.empty(); }

private:
    int _preferredBatchSize;
    int _minBatchSize;
    std::vector<BatchRequest> _buffer;
    int64_t _lastFlushUs;

    int64_t nowUs() const {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()
        ).count();
    }
};

}  // namespace symm
