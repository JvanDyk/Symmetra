/**
 * SSP Global Geo-Replication Implementation — Phase 14
 */

#include "runtime/SSPGlobalCluster.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace symm {

using namespace std::chrono;

SSPGlobalCluster::SSPGlobalCluster(ConsistencyLevel consistency)
    : consistency_level_(consistency)
{
    initializeDefaultRegions();
}

SSPGlobalCluster::~SSPGlobalCluster() = default;

// ────────────────────────────────────────────────────────────────────────────
// Initialize Default Regions
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::initializeDefaultRegions() {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    // US-East (Primary)
    DatacenterRegion us_east;
    us_east.region_name = "us-east";
    us_east.replica_id = 1;
    us_east.machine_count = 50;
    us_east.requests_handled = 0;
    us_east.throughput_rps = 4.56e9;
    us_east.is_healthy = true;
    us_east.latency_to_primary_ms = 0;
    regions_.push_back(us_east);

    // EU-West
    DatacenterRegion eu_west;
    eu_west.region_name = "eu-west";
    eu_west.replica_id = 2;
    eu_west.machine_count = 45;
    eu_west.requests_handled = 0;
    eu_west.throughput_rps = 4.10e9;
    eu_west.is_healthy = true;
    eu_west.latency_to_primary_ms = 80;
    regions_.push_back(eu_west);

    // AP-South
    DatacenterRegion ap_south;
    ap_south.region_name = "ap-south";
    ap_south.replica_id = 3;
    ap_south.machine_count = 35;
    ap_south.requests_handled = 0;
    ap_south.throughput_rps = 3.19e9;
    ap_south.is_healthy = true;
    ap_south.latency_to_primary_ms = 150;
    regions_.push_back(ap_south);

    primary_region_ = "us-east";
    backup_region_ = "eu-west";
}

// ────────────────────────────────────────────────────────────────────────────
// Initialize Regions (Custom)
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::initializeRegions() {
    // Use default regions
}

// ────────────────────────────────────────────────────────────────────────────
// Add Region
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::addRegion(const std::string& region_name, uint32_t machine_count) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    DatacenterRegion region = {
        .region_name = region_name,
        .replica_id = static_cast<uint32_t>(regions_.size() + 1),
        .machine_count = machine_count,
        .requests_handled = 0,
        .throughput_rps = machine_count * 91.2e6,
        .is_healthy = true,
        .latency_to_primary_ms = 0
    };

    regions_.push_back(region);
}

// ────────────────────────────────────────────────────────────────────────────
// Route to Region
// ────────────────────────────────────────────────────────────────────────────

std::string SSPGlobalCluster::routeToRegion() {
    std::lock_guard<std::mutex> lock(cluster_mutex_);
    return selectBestRegion();
}

// ────────────────────────────────────────────────────────────────────────────
// Replicate Request
// ────────────────────────────────────────────────────────────────────────────

bool SSPGlobalCluster::replicateRequest(uint64_t request_id,
                                       const uint8_t* data,
                                       uint32_t data_len,
                                       ConsistencyLevel consistency)
{
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    total_requests_++;

    ReplicationState state = {
        .request_id = request_id,
        .data_hash = std::string(reinterpret_cast<const char*>(data), std::min(data_len, 32u)),
        .encrypted_data = std::vector<uint8_t>(data, data + data_len),
        .replication_targets = 0,
        .replicas_synced = 0,
        .is_complete = false,
        .timestamp_us = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count()
    };

    // Determine replication targets based on consistency
    for (const auto& region : regions_) {
        if (!region.is_healthy) continue;

        if (consistency == ConsistencyLevel::Strong) {
            // Strong: all replicas must sync
            state.replication_targets |= (1 << region.replica_id);
        } else if (consistency == ConsistencyLevel::Eventual) {
            // Eventual: async, no waiting
            state.replication_targets = 0;  // No blocking
            break;
        } else if (consistency == ConsistencyLevel::Causal) {
            // Causal: primary + backup only
            if (region.replica_id <= 2) {
                state.replication_targets |= (1 << region.replica_id);
            }
        }
    }

    pending_replications_[request_id] = state;

    // For eventual consistency, mark as complete immediately
    if (consistency == ConsistencyLevel::Eventual) {
        state.is_complete = true;
        pending_replications_[request_id].is_complete = true;
        return true;
    }

    // For strong/causal, wait for acknowledgments
    return waitForReplication(request_id, 1000);  // 1 second timeout
}

// ────────────────────────────────────────────────────────────────────────────
// Acknowledge Replication
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::acknowledgeReplication(uint64_t request_id, uint32_t replica_id) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    auto it = pending_replications_.find(request_id);
    if (it == pending_replications_.end()) {
        return;
    }

    it->second.replicas_synced++;

    // Check if all required replicas have synced
    uint32_t required_count = 0;
    uint32_t targets = it->second.replication_targets;
    while (targets) {
        required_count += (targets & 1);
        targets >>= 1;
    }
    if (it->second.replicas_synced >= required_count) {
        it->second.is_complete = true;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Mark Region Unhealthy
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::markRegionUnhealthy(const std::string& region_name, const char* reason) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    for (auto& region : regions_) {
        if (region.region_name == region_name) {
            region.is_healthy = false;
            break;
        }
    }

    // Check if we're partitioned (primary is unhealthy)
    if (region_name == primary_region_) {
        is_partitioned_ = true;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Mark Region Healthy
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::markRegionHealthy(const std::string& region_name) {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    for (auto& region : regions_) {
        if (region.region_name == region_name) {
            region.is_healthy = true;
            break;
        }
    }

    // Check if primary is back online
    for (const auto& region : regions_) {
        if (region.region_name == primary_region_ && region.is_healthy) {
            is_partitioned_ = false;
            break;
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Get Healthy Regions
// ────────────────────────────────────────────────────────────────────────────

std::vector<std::string> SSPGlobalCluster::getHealthyRegions() const {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    std::vector<std::string> healthy;
    for (const auto& region : regions_) {
        if (region.is_healthy) {
            healthy.push_back(region.region_name);
        }
    }
    return healthy;
}

// ────────────────────────────────────────────────────────────────────────────
// Failover to Primary Backup
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::failoverToPrimaryBackup() {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    // Mark old primary as unhealthy
    for (auto& region : regions_) {
        if (region.region_name == primary_region_) {
            region.is_healthy = false;
            break;
        }
    }

    // Promote backup to primary
    primary_region_ = backup_region_;

    // Select new backup
    for (const auto& region : regions_) {
        if (region.region_name != primary_region_ && region.is_healthy) {
            backup_region_ = region.region_name;
            break;
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Get Region Status
// ────────────────────────────────────────────────────────────────────────────

const DatacenterRegion& SSPGlobalCluster::getRegionStatus(const std::string& region_name) const {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    for (const auto& region : regions_) {
        if (region.region_name == region_name) {
            return region;
        }
    }

    static DatacenterRegion dummy = {};
    return dummy;
}

// ────────────────────────────────────────────────────────────────────────────
// Set Consistency Level
// ────────────────────────────────────────────────────────────────────────────

void SSPGlobalCluster::setConsistencyLevel(ConsistencyLevel consistency) {
    consistency_level_ = consistency;
}

// ────────────────────────────────────────────────────────────────────────────
// Get Consistency Level
// ────────────────────────────────────────────────────────────────────────────

ConsistencyLevel SSPGlobalCluster::getConsistencyLevel() const {
    return consistency_level_;
}

// ────────────────────────────────────────────────────────────────────────────
// Get Statistics
// ────────────────────────────────────────────────────────────────────────────

SSPGlobalCluster::GlobalClusterStats SSPGlobalCluster::getStats() const {
    std::lock_guard<std::mutex> lock(cluster_mutex_);

    GlobalClusterStats stats = {};
    stats.total_regions = regions_.size();
    stats.total_requests = total_requests_;

    double aggregate_rps = 0.0;
    double aggregate_latency = 0.0;

    for (const auto& region : regions_) {
        if (region.is_healthy) {
            stats.healthy_regions++;
            aggregate_rps += region.throughput_rps;
            aggregate_latency += region.latency_to_primary_ms;
        }
    }

    stats.aggregate_rps = aggregate_rps;
    stats.average_latency_us = stats.healthy_regions > 0 ? (aggregate_latency / stats.healthy_regions) * 1000.0 : 0.0;
    stats.is_partitioned = is_partitioned_;
    stats.consistency_level = consistency_level_;

    return stats;
}

// ────────────────────────────────────────────────────────────────────────────
// Helper Methods
// ────────────────────────────────────────────────────────────────────────────

std::string SSPGlobalCluster::selectBestRegion() const {
    // Select nearest healthy region
    std::string best_region = primary_region_;
    double min_latency = 1e9;

    for (const auto& region : regions_) {
        if (!region.is_healthy) continue;

        if (region.latency_to_primary_ms < min_latency) {
            min_latency = region.latency_to_primary_ms;
            best_region = region.region_name;
        }
    }

    return best_region;
}

double SSPGlobalCluster::getLatencyToRegion(const std::string& region) const {
    for (const auto& r : regions_) {
        if (r.region_name == region) {
            return r.latency_to_primary_ms;
        }
    }
    return 1e9;
}

bool SSPGlobalCluster::waitForReplication(uint64_t request_id, uint64_t timeout_ms) {
    auto start = high_resolution_clock::now();

    while (true) {
        auto it = pending_replications_.find(request_id);
        if (it != pending_replications_.end() && it->second.is_complete) {
            return true;
        }

        auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            return false;  // Timeout
        }

        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(microseconds(100));
    }
}

}  // namespace symm
