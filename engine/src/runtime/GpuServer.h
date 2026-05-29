#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <cuda.h>

// Socket headers (needed for sockaddr_in)
#ifdef _WIN32
  #define NOMINMAX
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
#endif

#include "NvrtcCompiler.h"
#include "StreamPool.h"
#include "ThreadPool.h"
#include "GpuAllocator.h"
#include "WarpScheduler.h"
#include "BatchScheduler.h"
#include "BatchContext.h"
#include "RouteCache.h"
#include "AsyncBatchLauncher.h"
#include "parser/SymmParser.h"
#include "protect/Aes256.h"

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

    // TLS configuration (PSK-only mode, no certificates)
    // Set to non-empty hex string (32+ bytes) to enable TLS 1.3 PSK
    // Example: "0123456789ABCDEF0123456789ABCDEF" (16 bytes as hex → 32 ASCII)
    std::string tlsPsk;  // Pre-shared key in hex; empty = TLS disabled
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
    std::unique_ptr<AsyncBatchLauncher> _batchLauncher;  // Non-blocking batch dispatch

    // Per-stream pinned host memory for async D2H response copy (single-thread path)
    std::vector<void*> _h_resps;
    static constexpr size_t RESP_SIZE = 64 * 1024;   // 64 KB per response buffer
    static constexpr size_t BODY_SIZE = 64 * 1024;   // 64 KB max request body
    static constexpr int    KERNEL_TIMEOUT_MS = 5000;  // 5 second max per-batch execution

    // ── Rate limiting (per-IP token bucket) ───────────────────────────────────
    static constexpr int RATE_LIMIT_TOKENS_PER_SECOND = 100;  // tokens per IP per second
    static constexpr int RATE_LIMIT_BUCKET_CAPACITY   = 1000; // max tokens to accumulate
    struct RateLimitEntry {
        int    tokens;      // current token count
        int64_t lastRefill; // last refill timestamp (microseconds)
    };
    std::unordered_map<std::string, RateLimitEntry> _rateLimitMap;
    std::mutex _rateLimitMutex;

    // ── Route caching for keep-alive connections ───────────────────────────────
    RouteCache _routeCache;  // Caches route ID per connection (5-second TTL)

    bool checkRateLimit(const std::string& clientIp);
    std::string getClientIp(const sockaddr_in& addr) const;

    // ── TLS 1.3 PSK-only mode (lightweight, no handshake) ────────────────────
    // Pre-shared keys configured at server startup; no certificate validation.
    static constexpr int TLS_MAX_RECORD = 16384;  // TLS max record size (16 KB)
    static constexpr int TLS_NONCE_SIZE = 12;     // AES-GCM nonce size
    static constexpr int TLS_TAG_SIZE   = 16;     // AES-GCM tag size
    static constexpr int TLS_KEY_SIZE   = 32;     // AES-256 key size

    struct TlsSession {
        bool tlsEnabled = false;
        bool handshakeDone = false;
        std::array<uint8_t, TLS_KEY_SIZE> clientKey{};    // derived client → server key
        std::array<uint8_t, TLS_KEY_SIZE> serverKey{};    // derived server → client key
        uint64_t clientSeq = 0;  // client record sequence number
        uint64_t serverSeq = 0;  // server record sequence number
    };

    // TLS PSK configuration: set in ServerOptions before run()
    // Empty string = TLS disabled; non-empty = PSK (32+ bytes when hex-decoded)
    std::string _tlsPsk;  // Pre-shared key (hex-encoded)

    // TLS session lifecycle
    TlsSession createTlsSession();
    bool handleTlsClientHello(int fd, TlsSession& sess, const std::string& clientIp);
    std::string tlsEncryptRecord(const TlsSession& sess, const uint8_t* plaintext, size_t len);
    std::string tlsDecryptRecord(TlsSession& sess, const std::string& encRecord);
    void tlsDeriveKeys(TlsSession& sess, const uint8_t* serverRandom, const uint8_t* clientRandom);

    // ── Debug ring buffer for console.log output ──────────────────────────────
    CUdeviceptr _debugRingBuffer = 0;
    std::vector<std::string> readDebugLogs();
    void clearDebugLogs();

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
        const char* bodyStart     = nullptr;
        int         bodyLen       = 0;
        // Common request headers (populated from parsed HTTP)
        std::string authorization;   // "Bearer <token>", etc.
        std::string contentType;     // "application/json", etc.
        std::string userAgent;       // browser / client identifier
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
                              bool keepAlive = false,
                              const char* extraHeaders = nullptr) const;

    // Monotonic microsecond clock (shared by both paths)
    static int64_t nowUs() {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace symm
