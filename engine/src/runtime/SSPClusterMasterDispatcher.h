/**
 * SSP Cluster Master Dispatcher — Phase 7a
 *
 * Distributed encryption across multiple machines
 *
 * Architecture:
 * - Central master coordinates requests across worker machines
 * - Each worker: 8-GPU machine running Phase 6 (91.2M RPS)
 * - Request ID tracking for response matching
 * - Load balancing across worker pool
 * - Expected: Linear scaling (91.2M RPS per machine)
 *
 * 5 Machines:  456M RPS
 * 10 Machines: 912M RPS
 * 20 Machines: 1.8B RPS
 */

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <queue>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Worker Machine Configuration
// ────────────────────────────────────────────────────────────────────────────

struct ClusterWorkerNode {
    std::string hostname;           // e.g., "gpu-worker-1.internal"
    uint16_t port;                  // gRPC port (default 50051)
    int gpu_count;                  // Number of GPUs (typically 8)
    double estimated_rps;           // Per-worker RPS (11.4M per GPU)

    std::atomic<uint32_t> pending_requests{0};
    std::atomic<uint64_t> total_processed{0};
    std::atomic<bool> healthy{true};

    // Metrics
    double avg_latency_ms = 0.0;
    double utilization_percent = 0.0;
};

// ────────────────────────────────────────────────────────────────────────────
// Distributed Request (With Global Tracking)
// ────────────────────────────────────────────────────────────────────────────

struct DistributedEncryptionRequest {
    uint32_t global_request_id;     // Globally unique ID
    int assigned_worker_id;         // Which worker processes this

    const uint8_t* plaintext;
    uint32_t plaintext_len;
    uint8_t* ciphertext;
    uint8_t* tag;

    std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete;
    std::chrono::high_resolution_clock::time_point submit_time;
};

// ────────────────────────────────────────────────────────────────────────────
// Pending Response Tracking
// ────────────────────────────────────────────────────────────────────────────

struct PendingResponse {
    uint32_t global_request_id;
    uint8_t* ciphertext_buffer;
    uint8_t* tag_buffer;
    std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> callback;
    std::chrono::high_resolution_clock::time_point submit_time;
};

// ────────────────────────────────────────────────────────────────────────────
// SSP Cluster Master Dispatcher (Phase 7a)
// ────────────────────────────────────────────────────────────────────────────

class SSPClusterMasterDispatcher {
public:
    SSPClusterMasterDispatcher(uint16_t listen_port = 50050);
    ~SSPClusterMasterDispatcher();

    // Add worker machine to cluster
    bool addWorker(
        const std::string& hostname,
        uint16_t port,
        int gpu_count = 8
    );

    // Initialize cluster (validate all workers are reachable)
    bool initialize();

    /**
     * Submit request to cluster
     * Automatically routes to best available worker
     */
    bool submitRequest(
        const uint8_t* plaintext,
        uint32_t plaintext_len,
        uint8_t* ciphertext,
        uint8_t* tag,
        std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete
    );

    /**
     * Get cluster statistics
     */
    struct ClusterStats {
        uint32_t total_workers;
        uint32_t healthy_workers;
        uint64_t total_requests;
        double aggregate_rps;
        double avg_latency_ms;
    };

    ClusterStats getClusterStats() const;

    /**
     * Health check: verify all workers are online
     */
    bool healthCheck();

    /**
     * Graceful shutdown
     */
    void shutdown();

private:
    uint16_t listen_port_;
    std::vector<std::shared_ptr<ClusterWorkerNode>> worker_nodes_;
    std::mutex workers_mutex_;

    // Request tracking
    std::map<uint32_t, PendingResponse> pending_responses_;
    std::mutex response_tracking_mutex_;

    uint32_t next_request_id_;
    std::atomic<uint32_t> next_worker_idx_{0};

    // Master server
    void* grpc_server_;  // grpc::Server*
    std::thread server_thread_;
    std::atomic<bool> server_running_{false};

    // Response polling
    std::thread response_poller_;
    std::atomic<bool> poller_running_{false};

    // Statistics
    std::atomic<uint64_t> total_requests_submitted_{0};
    std::atomic<uint64_t> total_requests_completed_{0};

    // Helper methods
    int selectBestWorker();
    void sendRequestToWorker(int worker_id, const DistributedEncryptionRequest& req);
    void pollWorkerResponses();
    void startGRPCServer();
    void stopGRPCServer();
};

}  // namespace symm
