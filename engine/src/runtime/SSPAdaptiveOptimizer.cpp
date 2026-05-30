/**
 * SSP Adaptive Optimizer Implementation — Phase 8
 */

#include "runtime/SSPAdaptiveOptimizer.h"
#include <algorithm>
#include <cmath>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ────────────────────────────────────────────────────────────────────────────

SSPAdaptiveOptimizer::SSPAdaptiveOptimizer(uint32_t history_window)
    : history_window_(history_window)
{
    // Initialize with baseline parameters
    current_params_.current_batch_size = 256;
    current_params_.kernel_block_size = 32;
    current_params_.gpu_stream_count = 2;
    current_params_.timeout_ms = 5.0;
    current_params_.gpu_utilization_target = 0.85;
    current_params_.enable_prefetching = false;
    current_params_.enable_compression = false;
}

SSPAdaptiveOptimizer::~SSPAdaptiveOptimizer() = default;

// ────────────────────────────────────────────────────────────────────────────
// Record Metrics
// ────────────────────────────────────────────────────────────────────────────

void SSPAdaptiveOptimizer::recordMetrics(const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    metrics_history_.push_back(metrics);

    // Keep only recent history
    while (metrics_history_.size() > history_window_) {
        metrics_history_.pop_front();
    }

    // Analyze trends and optimize
    if (metrics_history_.size() >= 10) {
        analyzeTrends();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Analyze Trends & Optimize
// ────────────────────────────────────────────────────────────────────────────

void SSPAdaptiveOptimizer::analyzeTrends() {
    // Calculate average metrics
    double avg_throughput = 0.0;
    double avg_latency = 0.0;
    double avg_gpu_util = 0.0;

    for (const auto& m : metrics_history_) {
        avg_throughput += m.throughput_rps;
        avg_latency += m.avg_latency_us;
        avg_gpu_util += m.gpu_utilization_percent;
    }

    avg_throughput /= metrics_history_.size();
    avg_latency /= metrics_history_.size();
    avg_gpu_util /= metrics_history_.size();

    // Predict optimal parameters
    uint32_t optimal_batch = predictOptimalBatchSize();
    double optimal_timeout = predictOptimalTimeout();

    // Update parameters
    current_params_.current_batch_size = optimal_batch;
    current_params_.timeout_ms = optimal_timeout;

    // Enable prefetching if queue is growing
    if (metrics_history_.back().queue_depth > 100) {
        current_params_.enable_prefetching = true;
    }

    // Enable compression if latency is high
    if (avg_latency > 10.0) {
        current_params_.enable_compression = true;
    }

    optimization_count_++;
}

// ────────────────────────────────────────────────────────────────────────────
// Predict Optimal Batch Size
// ────────────────────────────────────────────────────────────────────────────

uint32_t SSPAdaptiveOptimizer::predictOptimalBatchSize() {
    if (metrics_history_.empty()) {
        return 256;  // Default
    }

    // Analyze throughput at different batch sizes
    double max_throughput = 0.0;
    uint32_t optimal_batch = 256;

    // Group metrics by batch size and find max throughput
    std::map<uint32_t, double> throughput_by_batch;
    for (const auto& m : metrics_history_) {
        if (throughput_by_batch.find(m.batch_size) == throughput_by_batch.end()) {
            throughput_by_batch[m.batch_size] = m.throughput_rps;
        } else {
            throughput_by_batch[m.batch_size] =
                (throughput_by_batch[m.batch_size] + m.throughput_rps) / 2.0;
        }
    }

    // Find batch size with highest throughput
    for (const auto& [batch, throughput] : throughput_by_batch) {
        if (throughput > max_throughput) {
            max_throughput = throughput;
            optimal_batch = batch;
        }
    }

    // Increase batch size if GPU utilization is low
    const auto& latest = metrics_history_.back();
    if (latest.gpu_utilization_percent < 50.0 && optimal_batch < 1024) {
        optimal_batch = std::min(optimal_batch * 2, 1024u);
    }

    // Decrease batch size if latency is high
    if (latest.avg_latency_us > 20.0 && optimal_batch > 256) {
        optimal_batch = std::max(optimal_batch / 2, 256u);
    }

    return optimal_batch;
}

// ────────────────────────────────────────────────────────────────────────────
// Predict Optimal Timeout
// ────────────────────────────────────────────────────────────────────────────

double SSPAdaptiveOptimizer::predictOptimalTimeout() {
    if (metrics_history_.empty()) {
        return 5.0;  // Default 5ms
    }

    const auto& latest = metrics_history_.back();

    // Increase timeout if queue is shallow (less frequent flushing needed)
    if (latest.queue_depth < 50) {
        return std::min(50.0, 5.0 + latest.queue_depth * 0.1);
    }

    // Decrease timeout if queue is deep (frequent processing needed)
    if (latest.queue_depth > 500) {
        return std::max(1.0, 5.0 - (latest.queue_depth - 500) * 0.01);
    }

    return 5.0;  // Default
}

// ────────────────────────────────────────────────────────────────────────────
// Optimize for Current Load
// ────────────────────────────────────────────────────────────────────────────

void SSPAdaptiveOptimizer::optimizeForLoad(double current_load_percent) {
    // Adjust parameters based on system load
    if (current_load_percent > 80.0) {
        // High load: increase batch size and streams
        current_params_.current_batch_size = std::min(1024u, current_params_.current_batch_size + 64);
        current_params_.gpu_stream_count = std::min(8u, current_params_.gpu_stream_count + 1);
    } else if (current_load_percent < 20.0) {
        // Low load: decrease batch size for lower latency
        current_params_.current_batch_size = std::max(256u, current_params_.current_batch_size - 64);
        current_params_.gpu_stream_count = std::max(2u, current_params_.gpu_stream_count - 1);
    }

    optimization_count_++;
}

// ────────────────────────────────────────────────────────────────────────────
// Get Recommended Parameters
// ────────────────────────────────────────────────────────────────────────────

AdaptiveParameters SSPAdaptiveOptimizer::getRecommendedParameters() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_params_;
}

// ────────────────────────────────────────────────────────────────────────────
// Get Statistics
// ────────────────────────────────────────────────────────────────────────────

SSPAdaptiveOptimizer::OptimizationStats SSPAdaptiveOptimizer::getStats() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(metrics_mutex_));

    OptimizationStats stats = {};
    stats.current_batch_size = current_params_.current_batch_size;

    if (metrics_history_.empty()) {
        return stats;
    }

    // Calculate statistics
    double total_throughput = 0.0;
    double total_latency = 0.0;
    double peak_throughput = 0.0;

    for (const auto& m : metrics_history_) {
        total_throughput += m.throughput_rps;
        total_latency += m.avg_latency_us;
        peak_throughput = std::max(peak_throughput, m.throughput_rps);
    }

    stats.avg_throughput_rps = total_throughput / metrics_history_.size();
    stats.avg_latency_us = total_latency / metrics_history_.size();
    stats.peak_throughput_rps = peak_throughput;
    stats.optimization_gain_percent = calculateOptimizationGain();

    return stats;
}

// ────────────────────────────────────────────────────────────────────────────
// Calculate Optimization Gain
// ────────────────────────────────────────────────────────────────────────────

double SSPAdaptiveOptimizer::calculateOptimizationGain() {
    if (metrics_history_.size() < 2) {
        return 0.0;
    }

    // Compare first and last metrics
    double first_throughput = metrics_history_.front().throughput_rps;
    double last_throughput = metrics_history_.back().throughput_rps;

    if (first_throughput == 0.0) {
        return 0.0;
    }

    return ((last_throughput - first_throughput) / first_throughput) * 100.0;
}

// ────────────────────────────────────────────────────────────────────────────
// Reset Metrics
// ────────────────────────────────────────────────────────────────────────────

void SSPAdaptiveOptimizer::resetMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_history_.clear();
}

}  // namespace symm
