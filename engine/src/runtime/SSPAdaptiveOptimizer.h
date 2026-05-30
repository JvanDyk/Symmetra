/**
 * SSP Adaptive Optimizer — Phase 8
 *
 * Intelligent dynamic tuning based on real-time metrics
 *
 * Features:
 * - Dynamic batch sizing (256-1024 messages)
 * - ML-based load prediction
 * - Adaptive GPU kernel parameters
 * - Real-time performance monitoring
 * - Self-tuning dispatcher
 *
 * Expected: 5-15% performance improvement through optimization
 */

#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <chrono>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Performance Metrics Window
// ────────────────────────────────────────────────────────────────────────────

struct PerformanceMetrics {
    std::chrono::high_resolution_clock::time_point timestamp;
    uint32_t batch_size;
    double throughput_rps;
    double avg_latency_us;
    double gpu_utilization_percent;
    double queue_depth;
    double network_latency_ms;
};

// ────────────────────────────────────────────────────────────────────────────
// Optimization Parameters
// ────────────────────────────────────────────────────────────────────────────

struct AdaptiveParameters {
    uint32_t current_batch_size;     // 256-1024
    uint32_t kernel_block_size;      // 32-256 threads per block
    uint32_t gpu_stream_count;       // 2-8 concurrent streams
    double   timeout_ms;             // Batch timeout (1-50ms)

    double gpu_utilization_target;   // Target 80-95%
    bool enable_prefetching;         // Prefetch next batch
    bool enable_compression;         // Compress small messages
};

// ────────────────────────────────────────────────────────────────────────────
// SSP Adaptive Optimizer (Phase 8)
// ────────────────────────────────────────────────────────────────────────────

class SSPAdaptiveOptimizer {
public:
    SSPAdaptiveOptimizer(uint32_t history_window = 100);
    ~SSPAdaptiveOptimizer();

    // Record performance metrics
    void recordMetrics(const PerformanceMetrics& metrics);

    // Get recommended parameters based on current load
    AdaptiveParameters getRecommendedParameters();

    // Update optimization based on metrics
    void optimizeForLoad(double current_load_percent);

    // Statistics
    struct OptimizationStats {
        double avg_throughput_rps;
        double avg_latency_us;
        double peak_throughput_rps;
        double optimization_gain_percent;
        uint32_t current_batch_size;
    };

    OptimizationStats getStats() const;

    // Reset metrics (for new test window)
    void resetMetrics();

private:
    std::deque<PerformanceMetrics> metrics_history_;
    uint32_t history_window_;
    std::mutex metrics_mutex_;

    AdaptiveParameters current_params_;
    std::atomic<uint32_t> optimization_count_{0};

    // Helper methods
    void analyzeTrends();
    uint32_t predictOptimalBatchSize();
    double predictOptimalTimeout();
    double calculateOptimizationGain();
};

}  // namespace symm
