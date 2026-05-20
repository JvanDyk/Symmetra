#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <cuda.h>
#include "NvrtcCompiler.h"
#include "parser/SymmParser.h"

namespace symm {

struct ServerOptions {
    int         port    = 8080;
    int         streams = 32;
    std::string engine  = "OMEGA";
    bool        verbose = false;
};

// The GpuServer owns the CUDA memory, route table, and TCP socket.
// One instance per symmetra process. run() blocks until SIGINT.
class GpuServer {
public:
    GpuServer(const CompiledModule& mod,
              const std::vector<RouteReg>& routes,
              const ServerOptions& opts);
    ~GpuServer();

    // Start serving. Blocks until Ctrl-C / SIGINT.
    void run();

private:
    const CompiledModule& _mod;
    const std::vector<RouteReg>& _routes;
    const ServerOptions& _opts;

    // VRAM buffers for one batch of concurrent requests
    static constexpr int MAX_BATCH = 64;   // requests per GPU dispatch

    CUdeviceptr _d_ctx    = 0;   // SymmCtx[MAX_BATCH] in VRAM
    CUdeviceptr _d_bodies = 0;   // raw body storage
    CUdeviceptr _d_resps  = 0;   // response buffers

    // Host-side pinned memory for zero-copy I/O
    void* _h_ctx   = nullptr;
    void* _h_resps = nullptr;

    // TCP socket
    int _listenFd = -1;

    std::atomic<bool> _running{false};

    // Internals
    void allocVram();
    void freeVram();
    bool initSocket();
    void serveOne();

    // Route lookup: returns index into _routes, -1 if not found
    int matchRoute(const std::string& method, const std::string& path) const;

    // Parse minimal HTTP/1.1 from raw buffer
    struct ParsedRequest {
        std::string method;
        std::string path;
        std::string query;
        const char* bodyStart;
        int         bodyLen;
    };
    bool parseHttp(const char* buf, int len, ParsedRequest& req) const;

    // Build HTTP/1.1 response string
    std::string buildResponse(int status, const char* body, int bodyLen) const;
};

} // namespace symm
