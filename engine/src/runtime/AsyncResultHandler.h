#pragma once
/**
 * AsyncResultHandler — handles GPU result synchronization asynchronously using CUDA callbacks.
 *
 * Instead of blocking the scheduler thread on cuStreamSynchronize(), we:
 * 1. Queue async memory copies (cuMemcpyDtoHAsync)
 * 2. Register a CUDA callback (cuStreamAddCallback) to be called when GPU work finishes
 * 3. In the callback, trigger response sending on the I/O thread pool
 *
 * This allows the scheduler thread to return immediately and process the next batch
 * while GPU results are being downloaded and responses sent asynchronously.
 */

#include <functional>
#include <memory>
#include <cuda.h>
#include "GpuServer.h"

namespace symm {

struct ResultDownloadContext {
    // Results that will be filled in by async operations
    std::vector<BatchRequest> requests;
    std::vector<unsigned int> resp_lens;
    std::vector<int>          resp_statuses;
    std::vector<std::string>  resp_bodies;
    std::vector<std::string>  resp_headers;

    // Callback to invoke when GPU work completes
    std::function<void(const std::vector<BatchRequest>&,
                       const std::vector<int>&,
                       const std::vector<std::string>&,
                       const std::vector<std::string>&)> onComplete;

    // Metadata
    int count;
    int batch_id;
};

// CUDA callback function signature: void(*)(CUstream, CUresult, void*)
// This is called when the stream has completed all queued operations
static void CUDAAPI asyncDownloadCallback(CUstream stream, CUresult status, void* userData) {
    if (!userData) return;

    auto* ctx = static_cast<ResultDownloadContext*>(userData);

    if (status != CUDA_SUCCESS) {
        // GPU error — fill with error responses
        for (auto& s : ctx->resp_statuses) s = 500;
        for (auto& b : ctx->resp_bodies) b = "{\"error\":\"GPU computation failed\"}";
    }

    // Invoke completion callback
    if (ctx->onComplete) {
        ctx->onComplete(ctx->requests, ctx->resp_statuses, ctx->resp_bodies, ctx->resp_headers);
    }

    // Free context
    delete ctx;
}

} // namespace symm
