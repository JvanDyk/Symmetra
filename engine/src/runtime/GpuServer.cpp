#include "GpuServer.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <csignal>
#include <cuda_runtime.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socklen_t = int;
  using ssize_t   = int;
  #define CLOSE_SOCKET closesocket
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define CLOSE_SOCKET close
#endif

#define CUDA_CHECK(expr) do { \
    cudaError_t _e = (expr); \
    if (_e != cudaSuccess) \
        throw std::runtime_error(std::string("CUDA: ") + cudaGetErrorString(_e)); \
} while(0)

namespace symm {

// ── VRAM layout sizes ─────────────────────────────────────────────────────────
static constexpr size_t CTX_SIZE    = 256;          // sizeof(SymmCtx) — must match prelude
static constexpr size_t BODY_SIZE   = 64 * 1024;    // 64 KB per request body
static constexpr size_t RESP_SIZE   = 64 * 1024;    // 64 KB per response

static volatile sig_atomic_t g_stop = 0;
static void sigHandler(int) { g_stop = 1; }

GpuServer::GpuServer(const CompiledModule& mod,
                     const std::vector<RouteReg>& routes,
                     const ServerOptions& opts)
    : _mod(mod), _routes(routes), _opts(opts)
{
    allocVram();
}

GpuServer::~GpuServer() {
    freeVram();
    if (_listenFd >= 0) CLOSE_SOCKET(_listenFd);
#ifdef _WIN32
    WSACleanup();
#endif
}

void GpuServer::allocVram() {
    // Device memory: SymmCtx array + body storage + response buffers
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&_d_ctx),
                          CTX_SIZE * MAX_BATCH));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&_d_bodies),
                          BODY_SIZE * MAX_BATCH));
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&_d_resps),
                          RESP_SIZE * MAX_BATCH));
    CUDA_CHECK(cudaMemset(reinterpret_cast<void*>(_d_ctx),    0, CTX_SIZE    * MAX_BATCH));
    CUDA_CHECK(cudaMemset(reinterpret_cast<void*>(_d_bodies), 0, BODY_SIZE   * MAX_BATCH));
    CUDA_CHECK(cudaMemset(reinterpret_cast<void*>(_d_resps),  0, RESP_SIZE   * MAX_BATCH));

    // Host-side pinned memory for zero-copy transfers
    CUDA_CHECK(cudaMallocHost(&_h_ctx,   CTX_SIZE * MAX_BATCH));
    CUDA_CHECK(cudaMallocHost(&_h_resps, RESP_SIZE * MAX_BATCH));

    if (_opts.verbose) {
        std::printf("VRAM allocated: %.1f MB (ctx) + %.1f MB (bodies) + %.1f MB (resps)\n",
                    CTX_SIZE  * MAX_BATCH / 1e6,
                    BODY_SIZE * MAX_BATCH / 1e6,
                    RESP_SIZE * MAX_BATCH / 1e6);
    }
}

void GpuServer::freeVram() {
    if (_d_ctx)    cudaFree(reinterpret_cast<void*>(_d_ctx));
    if (_d_bodies) cudaFree(reinterpret_cast<void*>(_d_bodies));
    if (_d_resps)  cudaFree(reinterpret_cast<void*>(_d_resps));
    if (_h_ctx)    cudaFreeHost(_h_ctx);
    if (_h_resps)  cudaFreeHost(_h_resps);
    _d_ctx = _d_bodies = _d_resps = 0;
    _h_ctx = _h_resps = nullptr;
}

bool GpuServer::initSocket() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::fprintf(stderr, "GpuServer: WSAStartup failed\n");
        return false;
    }
#endif

    _listenFd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (_listenFd < 0) {
        std::fprintf(stderr, "GpuServer: socket() failed\n");
        return false;
    }

    // SO_REUSEADDR so restart doesn't wait for TIME_WAIT
    int yes = 1;
    ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(_opts.port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "GpuServer: bind() on port %d failed\n", _opts.port);
        CLOSE_SOCKET(_listenFd);
        _listenFd = -1;
        return false;
    }

    ::listen(_listenFd, 128);
    return true;
}

void GpuServer::run() {
    if (!initSocket()) throw std::runtime_error("GpuServer: failed to bind socket");

    std::signal(SIGINT, sigHandler);

    std::printf("Listening on :%d  ·  %d routes compiled  ·  engine=%s\n",
                _opts.port, (int)_routes.size(), _opts.engine.c_str());
    std::printf("Press Ctrl-C to stop.\n\n");

    _running = true;
    while (!g_stop) {
        serveOne();
    }

    std::puts("\nSymmetra: shutting down.");
}

void GpuServer::serveOne() {
    // Accept one connection
    sockaddr_in clientAddr{};
    socklen_t   clientLen = sizeof(clientAddr);
    int connFd = static_cast<int>(
        ::accept(_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen));
    if (connFd < 0) return;

    // Read raw HTTP request into pinned host memory (zero-copy to GPU)
    static thread_local char rawBuf[128 * 1024];
    ssize_t nRead = ::recv(connFd, rawBuf, sizeof(rawBuf) - 1, 0);
    if (nRead <= 0) { CLOSE_SOCKET(connFd); return; }
    rawBuf[nRead] = '\0';

    // Parse HTTP/1.1 (CPU side — just header parsing, body stays as pointer)
    ParsedRequest req{};
    if (!parseHttp(rawBuf, static_cast<int>(nRead), req)) {
        const char* badReq = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        ::send(connFd, badReq, static_cast<int>(strlen(badReq)), 0);
        CLOSE_SOCKET(connFd);
        return;
    }

    // Look up route
    int routeId = matchRoute(req.method, req.path);

    // ── GPU dispatch ──────────────────────────────────────────────────────────
    // 1. Fill SymmCtx host-side (pointers will be remapped to VRAM below)
    //    For simplicity, we run 1 request per dispatch in this version.
    //    Production: batch multiple concurrent requests.

    // Upload body to VRAM
    if (req.bodyLen > 0 && req.bodyStart) {
        CUDA_CHECK(cudaMemcpyAsync(
            reinterpret_cast<void*>(_d_bodies),
            req.bodyStart,
            static_cast<size_t>(req.bodyLen),
            cudaMemcpyHostToDevice));
    }

    // Build SymmCtx in pinned memory — GPU will read from VRAM pointers
    // (simplified: we pass route_id and let the dispatch kernel handle it)
    struct SymmCtxHost {
        const char* method;
        const char* path;
        const char* query;
        const char* body;
        int         body_len;
        char*       resp_buf;
        int         resp_len;
        int         resp_status;
        uint64_t    fingerprint;
    };

    // Upload path, method strings to VRAM
    char methodBuf[16] = {};
    std::strncpy(methodBuf, req.method.c_str(), 15);
    // (For production: keep per-request string buffers in VRAM)

    // Clear response buffer
    CUDA_CHECK(cudaMemset(reinterpret_cast<void*>(_d_resps), 0, RESP_SIZE));

    // Launch dispatch kernel: <<<1, 1>>> for single request
    // (Production: <<<batch/256, 256>>> for batched requests)
    if (routeId >= 0 && _mod.dispatch) {
        // We pass device pointers as kernel args via the Driver API
        CUdeviceptr d_resp = _d_resps;

        void* args[] = {
            reinterpret_cast<void*>(&_d_ctx),
            &routeId,
        };

        // Grid: 1 block of 1 thread for single request
        int gridX = 1, blockX = 1, n = 1;
        (void)n;

        CUresult kr = cuLaunchKernel(
            _mod.dispatch,
            gridX, 1, 1,
            blockX, 1, 1,
            0, nullptr,
            args, nullptr);

        if (kr != CUDA_SUCCESS) {
            const char* msg = nullptr;
            cuGetErrorString(kr, &msg);
            std::fprintf(stderr, "GpuServer: kernel launch failed: %s\n", msg ? msg : "?");
        }

        // Synchronize + download response
        cudaDeviceSynchronize();
        CUDA_CHECK(cudaMemcpy(_h_resps, reinterpret_cast<void*>(_d_resps), RESP_SIZE,
                              cudaMemcpyDeviceToHost));
    } else {
        // 404
        const char* notFound = "{\"error\":\"Not Found\"}";
        std::memcpy(_h_resps, notFound, strlen(notFound) + 1);
    }

    // Build and send HTTP response
    const char* respBody = static_cast<const char*>(_h_resps);
    int         respLen  = static_cast<int>(strlen(respBody));
    std::string httpResp = buildResponse(200, respBody, respLen);
    ::send(connFd, httpResp.c_str(), static_cast<int>(httpResp.size()), 0);
    CLOSE_SOCKET(connFd);
}

int GpuServer::matchRoute(const std::string& method, const std::string& path) const {
    for (int i = 0; i < (int)_routes.size(); i++) {
        if (_routes[i].method == method) {
            // Simple prefix match: support /path/:param patterns
            const std::string& rp = _routes[i].path;
            if (rp == path) return i;
            // Param match: /users/:id matches /users/42
            if (rp.back() != '/') {
                size_t paramPos = rp.rfind("/:");
                if (paramPos != std::string::npos) {
                    if (path.substr(0, paramPos) == rp.substr(0, paramPos)) return i;
                }
            }
        }
    }
    return -1;
}

bool GpuServer::parseHttp(const char* buf, int len, ParsedRequest& req) const {
    // Parse first line: METHOD /path HTTP/1.1
    const char* p = buf;
    const char* end = buf + len;

    // Method
    const char* sp = static_cast<const char*>(memchr(p, ' ', end - p));
    if (!sp) return false;
    req.method = std::string(p, sp);
    p = sp + 1;

    // Path (and query)
    sp = static_cast<const char*>(memchr(p, ' ', end - p));
    if (!sp) return false;
    std::string fullPath(p, sp);
    size_t qpos = fullPath.find('?');
    if (qpos != std::string::npos) {
        req.path  = fullPath.substr(0, qpos);
        req.query = fullPath.substr(qpos + 1);
    } else {
        req.path  = fullPath;
        req.query = "";
    }
    p = sp + 1;

    // Skip to end of headers (\r\n\r\n)
    const char* bodyStart = strstr(buf, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;
        req.bodyStart = bodyStart;
        req.bodyLen   = static_cast<int>(end - bodyStart);
    } else {
        req.bodyStart = nullptr;
        req.bodyLen   = 0;
    }

    return true;
}

std::string GpuServer::buildResponse(int status, const char* body, int bodyLen) const {
    const char* statusText = (status == 200) ? "OK" : (status == 404) ? "Not Found" : "Error";
    char header[256];
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  status, statusText, bodyLen);
    return std::string(header) + std::string(body, bodyLen);
}

} // namespace symm
