#pragma once
/**
 * GlobalFlushManager — Distributed timeout-based flushing
 *
 * Design:
 * - Maintains a registry of all active thread-local buffers
 * - Background thread periodically checks each buffer's timeout
 * - If timeout expired, forces flush (non-blocking)
 * - No centralization: each buffer maintains its own timer
 *
 * Alternative: Workers check their own timeout on submission
 * (Most efficient: no global thread needed)
 */

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>
#include "ThreadLocalBatchBuffer.h"

namespace symm {

class GlobalFlushManager {
public:
    // Start periodic flush checker thread
    explicit GlobalFlushManager(int checkIntervalMs = 5)
        : _checkIntervalMs(checkIntervalMs), _stop(false)
    {
        _flushThread = std::thread([this] { flushLoop(); });
    }

    ~GlobalFlushManager() {
        _stop.store(true);
        if (_flushThread.joinable()) _flushThread.join();
    }

    // Register a buffer for periodic timeout checking
    void registerBuffer(std::function<void()> onFlush) {
        std::lock_guard<std::mutex> lock(_buffersMutex);
        _buffers.push_back(onFlush);
    }

private:
    void flushLoop() {
        while (!_stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(_checkIntervalMs));

            // Check all registered buffers for timeout
            {
                std::lock_guard<std::mutex> lock(_buffersMutex);
                for (auto& callback : _buffers) {
                    callback();  // Call buffer's shouldFlush() check
                }
            }
        }
    }

    int _checkIntervalMs;
    std::atomic<bool> _stop;
    std::thread _flushThread;
    std::vector<std::function<void()>> _buffers;
    std::mutex _buffersMutex;
};

}  // namespace symm
