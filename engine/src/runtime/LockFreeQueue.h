#pragma once
/**
 * LockFreeQueue — non-blocking ring buffer queue using atomic CAS operations.
 *
 * Uses a fixed-size ring buffer with atomic head/tail pointers.
 * Simpler and more predictable than linked-list queues.
 *
 * Designed for the BatchScheduler:
 *   - HTTP threads call tryEnqueue() (producers)
 *   - Scheduler thread calls tryDequeue()/tryDequeueBatch() (consumer)
 *   - Never blocks, always returns immediately
 */

#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>

namespace symm {

template<typename T, size_t Capacity = 65536>
class LockFreeQueue {
    std::array<T, Capacity> _buffer;
    std::atomic<uint64_t> _head{0};      // Next position to dequeue from
    std::atomic<uint64_t> _tail{0};      // Next position to enqueue to

public:
    LockFreeQueue() = default;

    ~LockFreeQueue() = default;

    // Non-blocking enqueue — returns false if queue full
    // Safe for multiple concurrent producers
    bool tryEnqueue(const T& item) {
        while (true) {
            uint64_t tail = _tail.load(std::memory_order_relaxed);
            uint64_t next_tail = tail + 1;

            // Check if next position is full (wrapping around)
            uint64_t head = _head.load(std::memory_order_acquire);
            if ((next_tail % Capacity) == (head % Capacity)) {
                return false;  // Queue full
            }

            // Try to atomically advance tail using CAS
            if (_tail.compare_exchange_strong(tail, next_tail,
                std::memory_order_release, std::memory_order_relaxed)) {
                // We won the race - we own this slot
                _buffer[tail % Capacity] = item;
                return true;
            }
            // CAS failed - another thread got this slot, retry
        }
    }

    // Non-blocking dequeue — returns false if queue empty
    bool tryDequeue(T& item) {
        uint64_t head = _head.load(std::memory_order_relaxed);
        uint64_t tail = _tail.load(std::memory_order_acquire);

        // Check if queue is empty
        if (head == tail) {
            return false;  // Queue empty
        }

        // Read item from buffer
        item = _buffer[head % Capacity];

        // Atomically advance head
        _head.store(head + 1, std::memory_order_release);
        return true;
    }

    // Non-blocking dequeue of multiple items (for batching)
    size_t tryDequeueBatch(std::vector<T>& out, size_t maxCount) {
        uint64_t head = _head.load(std::memory_order_relaxed);
        uint64_t tail = _tail.load(std::memory_order_acquire);

        // Calculate how many items are available
        size_t available = tail > head ? (tail - head) : 0;
        size_t toRead = available < maxCount ? available : maxCount;

        for (size_t i = 0; i < toRead; i++) {
            out.push_back(_buffer[(head + i) % Capacity]);
        }

        if (toRead > 0) {
            _head.store(head + toRead, std::memory_order_release);
        }

        return toRead;
    }

    // Approximate size (lock-free read, may be slightly stale)
    size_t size() const {
        uint64_t tail = _tail.load(std::memory_order_acquire);  // Acquire to see latest writes
        uint64_t head = _head.load(std::memory_order_relaxed);
        return tail > head ? (tail - head) : 0;
    }

    // Check if queue is empty (lock-free read)
    bool empty() const {
        uint64_t tail = _tail.load(std::memory_order_acquire);  // Acquire to see latest writes
        uint64_t head = _head.load(std::memory_order_relaxed);
        return head == tail;
    }

private:
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
};

} // namespace symm
