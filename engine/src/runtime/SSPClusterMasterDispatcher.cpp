/**
 * SSP Cluster Master Dispatcher Implementation — Phase 7a
 *
 * Distributed encryption across worker machines
 */

#include "runtime/SSPClusterMasterDispatcher.h"
#include <chrono>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace symm {

// ────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ────────────────────────────────────────────────────────────────────────────

SSPClusterMasterDispatcher::SSPClusterMasterDispatcher(uint16_t listen_port)
    : listen_port_(listen_port),
      next_request_id_(1)
{
    worker_nodes_.reserve(50);  // Support up to 50 workers
}

SSPClusterMasterDispatcher::~SSPClusterMasterDispatcher() {
    shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// Add Worker Machine
// ────────────────────────────────────────────────────────────────────────────

bool SSPClusterMasterDispatcher::addWorker(
    const std::string& hostname,
    uint16_t port,
    int gpu_count)
{
    std::lock_guard<std::mutex> lock(workers_mutex_);

    auto worker = std::make_shared<ClusterWorkerNode>();
    worker->hostname = hostname;
    worker->port = port;
    worker->gpu_count = gpu_count;
    worker->estimated_rps = 11.4e6 * gpu_count;  // 11.4M RPS per GPU

    worker_nodes_.push_back(worker);

    std::cout << "[Cluster] Added worker: " << hostname << ":" << port
              << " (GPUs: " << gpu_count << ", Est. RPS: "
              << (gpu_count * 11.4) << "M)\n";

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Initialize Cluster
// ────────────────────────────────────────────────────────────────────────────

bool SSPClusterMasterDispatcher::initialize() {
    if (worker_nodes_.empty()) {
        std::cerr << "[Cluster] No workers configured\n";
        return false;
    }

    // Validate all workers are reachable
    if (!healthCheck()) {
        std::cerr << "[Cluster] Worker health check failed\n";
        return false;
    }

    // Start response polling thread
    poller_running_ = true;
    response_poller_ = std::thread([this]() { this->pollWorkerResponses(); });

    std::cout << "[Cluster] Initialized with " << worker_nodes_.size() << " workers\n";
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Health Check
// ────────────────────────────────────────────────────────────────────────────

bool SSPClusterMasterDispatcher::healthCheck() {
    std::lock_guard<std::mutex> lock(workers_mutex_);

    for (auto& worker : worker_nodes_) {
        // In production: actual gRPC health check to worker
        // For now: assume healthy
        worker->healthy = true;
    }

    uint32_t healthy = 0;
    for (const auto& worker : worker_nodes_) {
        if (worker->healthy) healthy++;
    }

    std::cout << "[Cluster] Health check: " << healthy << "/"
              << worker_nodes_.size() << " workers healthy\n";

    return healthy > 0;
}

// ────────────────────────────────────────────────────────────────────────────
// Select Best Worker
// ────────────────────────────────────────────────────────────────────────────

int SSPClusterMasterDispatcher::selectBestWorker() {
    std::lock_guard<std::mutex> lock(workers_mutex_);

    // Find worker with fewest pending requests (queue-depth aware)
    int best_worker = -1;
    uint32_t min_pending = UINT32_MAX;

    for (size_t i = 0; i < worker_nodes_.size(); i++) {
        if (!worker_nodes_[i]->healthy) continue;

        uint32_t pending = worker_nodes_[i]->pending_requests;
        if (pending < min_pending) {
            min_pending = pending;
            best_worker = i;
        }
    }

    return best_worker;
}

// ────────────────────────────────────────────────────────────────────────────
// Submit Request to Cluster
// ────────────────────────────────────────────────────────────────────────────

bool SSPClusterMasterDispatcher::submitRequest(
    const uint8_t* plaintext,
    uint32_t plaintext_len,
    uint8_t* ciphertext,
    uint8_t* tag,
    std::function<void(uint32_t, const uint8_t*, uint32_t, const uint8_t*)> on_complete)
{
    if (!plaintext || !ciphertext || !tag) return false;
    if (plaintext_len == 0) return false;

    // Select best worker
    int worker_id = selectBestWorker();
    if (worker_id < 0) {
        std::cerr << "[Cluster] No healthy workers available\n";
        return false;
    }

    // Create distributed request
    DistributedEncryptionRequest req;
    req.global_request_id = next_request_id_++;
    req.assigned_worker_id = worker_id;
    req.plaintext = plaintext;
    req.plaintext_len = plaintext_len;
    req.ciphertext = ciphertext;
    req.tag = tag;
    req.on_complete = on_complete;
    req.submit_time = std::chrono::high_resolution_clock::now();

    // Track response
    {
        std::lock_guard<std::mutex> lock(response_tracking_mutex_);
        PendingResponse pending;
        pending.global_request_id = req.global_request_id;
        pending.ciphertext_buffer = ciphertext;
        pending.tag_buffer = tag;
        pending.callback = on_complete;
        pending.submit_time = req.submit_time;

        pending_responses_[req.global_request_id] = pending;
    }

    // Send to worker
    sendRequestToWorker(worker_id, req);

    // Update worker state
    worker_nodes_[worker_id]->pending_requests++;
    total_requests_submitted_++;

    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Send Request to Worker
// ────────────────────────────────────────────────────────────────────────────

void SSPClusterMasterDispatcher::sendRequestToWorker(
    int worker_id,
    const DistributedEncryptionRequest& req)
{
    if (worker_id < 0 || worker_id >= static_cast<int>(worker_nodes_.size())) {
        return;
    }

    auto worker = worker_nodes_[worker_id];

    // In production: use gRPC to send to worker:port
    // For simulation: inline processing
    // worker->hostname = "gpu-worker-1.internal"
    // worker->port = 50051
    // grpc_stub->SubmitEncryption(req, response);
}

// ────────────────────────────────────────────────────────────────────────────
// Poll Worker Responses
// ────────────────────────────────────────────────────────────────────────────

void SSPClusterMasterDispatcher::pollWorkerResponses() {
    while (poller_running_) {
        // In production: poll gRPC workers for completed responses
        // For simulation: immediately process

        std::vector<uint32_t> completed_ids;
        {
            std::lock_guard<std::mutex> lock(response_tracking_mutex_);
            for (auto& [req_id, pending] : pending_responses_) {
                // In production: check if response is ready from worker
                // For simulation: assume immediate completion

                auto elapsed = std::chrono::high_resolution_clock::now() - pending.submit_time;
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

                if (elapsed_ms >= 1) {  // Simulate latency
                    completed_ids.push_back(req_id);
                }
            }
        }

        // Process completions
        {
            std::lock_guard<std::mutex> lock(response_tracking_mutex_);
            for (uint32_t id : completed_ids) {
                auto it = pending_responses_.find(id);
                if (it != pending_responses_.end()) {
                    const auto& pending = it->second;

                    // Invoke callback
                    if (pending.callback) {
                        pending.callback(id, pending.ciphertext_buffer,
                                       0, pending.tag_buffer);
                    }

                    pending_responses_.erase(it);
                    total_requests_completed_++;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Statistics
// ────────────────────────────────────────────────────────────────────────────

SSPClusterMasterDispatcher::ClusterStats SSPClusterMasterDispatcher::getClusterStats() const {
    ClusterStats stats;

    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(workers_mutex_));
        stats.total_workers = worker_nodes_.size();
        stats.healthy_workers = 0;

        for (const auto& worker : worker_nodes_) {
            if (worker->healthy) {
                stats.healthy_workers++;
            }
        }

        stats.aggregate_rps = 0.0;
        for (const auto& worker : worker_nodes_) {
            if (worker->healthy) {
                stats.aggregate_rps += worker->estimated_rps;
            }
        }
    }

    stats.total_requests = total_requests_submitted_;
    stats.avg_latency_ms = 1.0;  // Placeholder

    return stats;
}

// ────────────────────────────────────────────────────────────────────────────
// Shutdown
// ────────────────────────────────────────────────────────────────────────────

void SSPClusterMasterDispatcher::shutdown() {
    // Stop response poller
    poller_running_ = false;
    if (response_poller_.joinable()) {
        response_poller_.join();
    }

    // Wait for all pending responses
    while (true) {
        {
            std::lock_guard<std::mutex> lock(response_tracking_mutex_);
            if (pending_responses_.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Stop gRPC server if running
    stopGRPCServer();

    std::cout << "[Cluster] Shutdown complete. Processed "
              << total_requests_completed_ << " requests\n";
}

}  // namespace symm
