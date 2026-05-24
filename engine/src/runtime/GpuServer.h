#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <cuda.h>
#include "NvrtcCompiler.h"
#include "StreamPool.h"
#include "ThreadPool.h"
#include "GpuAllocator.h"
#include "WarpScheduler.h"
#include "BatchScheduler.h"
#include "BatchContext.h"
#include "parser/SymmParser.h"

namespace symm {

struct ServerOptions {
    int  port      = 8080;
    int  streams   = 32;   // CUDA stream pool size
    // ioThreads must be >= streams for full GPU utilisation; each thread blocks on
    // cudaStreamSynchronize() while holding a stream slot.  Default matches streams.
    int  ioThreads = 32;
    bool verbose   = false;

    // useBatch: prefer the warp-aligned batch dispatch path (symm_dispatch_batch).
    // Automatically falls back to single-thread dispatch if the compiled module
    // does not contain symm_dispatch_batch (e.g. older modules).
    bool useBatch  = true;
};

// GpuServer — multi-stream, multi-threaded HTTP server.
//
// Request lifecycle:
//   CPU thread (ThreadPool) → accept → recv → StreamPool::acquire
//   → GpuAllocator::requestAlloc (slab from stream slot)
//   → cudaMemcpyAsync body → GPU kernel on stream
//   → cudaMemcpyAsync response → send → GpuAllocator::requestFree
//   → StreamPool::release (sync + return)
//
// All N ioThreads compete on the same listen socket (OS-level serialisation).
// Each thread that wins accept() immediately processes the request on its own
// CUDA stream — no head-of-line blocking between requests.
//
class GpuServer {
public:
    GpuServer(const CompiledModule& mod,
              const std::vector<RouteReg>& routes,
              const ServerOptions& opts);
    ~GpuServer();

    void run();   // blocks until SIGINT

private:
    const CompiledModule&        _mod;
    const std::vector<RouteReg>& _routes;
    const ServerOptions&         _opts;

    std::unique_ptr<StreamPool>  _streams;
    std::unique_ptr<ThreadPool>  _io;

    // Per-stream pinned host memory for async D2H response copy (single-thread path)
    std::vector<void*> _h_resps;
    static constexpr size_t RESP_SIZE = 64 * 1024;   // 64 KB per response buffer
    static constexpr size_t BODY_SIZE = 64 * 1024;   // 64 KB max request body

    // ── Batch dispatch infrastructure ─────────────────────────────────────────
    // One BatchContext per stream slot — allocated once at startup, reused forever.
    // _scheduler owns the thread that fires batches; valid only when dispatchBatch
    // is available and useBatch is true.
    std::vector<BatchContext>           _batchCtxs;
    std::unique_ptr<BatchScheduler>     _scheduler;

    int _listenFd = -1;
    std::atomic<bool> _running{ false };

    void initPinnedBuffers();
    void freePinnedBuffers();
    bool initSocket();

    // Whether the batch path is active for this server instance.
    bool batchModeActive() const {
        return _scheduler && _mod.dispatchBatch;
    }

    struct ParsedRequest {
        std::string method;
        std::string path;
        std::string query;
        std::unordered_map<std::string, std::string> headers;
        const char* bodyStart = nullptr;
        int         bodyLen   = 0;
    };

    // ── Accept loops ──────────────────────────────────────────────────────────
    // Single-thread path (legacy / fallback):
    void acceptLoop();
    void handleConn(int connFd, StreamPool::Slot slot);

    // Batch path: accepts requests and submits them to the BatchScheduler.
    // The fd is transferred to the BatchRequest; GpuServer never closes it —
    // sendBatchResponses() does that after the kernel completes.
    void acceptLoopBatch();

    // ── Batch GPU dispatch (called by BatchScheduler on its thread) ───────────
    // Uploads the batch to VRAM, launches symm_dispatch_batch, downloads results,
    // then calls sendBatchResponses().
    void batchLaunch(int routeId,
                     const std::vector<BatchRequest>& requests,
                     BatchResult& result);

    // Writes one HTTP response per request and closes each fd.
    void sendBatchResponses(const std::vector<BatchRequest>& requests,
                            const BatchResult& result);

    // ── Shared helpers ────────────────────────────────────────────────────────
    int matchRoute(const std::string& method, const std::string& path) const;
    int  recvAll(int fd, char* buf, int cap) const;
    bool parseHttp(const char* buf, int len, ParsedRequest& req) const;
    bool wantsKeepAlive(const ParsedRequest& req) const;
    std::string buildResponse(int status, const char* body, int bodyLen,
                              bool keepAlive = false) const;

    // Monotonic microsecond clock (shared by both paths)
    static int64_t nowUs() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace symm
