#include "GpuServer.h"
#include "HttpParser.h"
#include "RouteCache.h"
#include "DebugRingBuffer.h"
#include "protect/Obfuscate.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <csignal>
#include <chrono>
#include <cuda.h>
#include <cuda_runtime.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <wincrypt.h>
  using socklen_t = int;
  using ssize_t   = int;
  #define CLOSE_SOCKET closesocket
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "advapi32.lib")
#else
  #include <unistd.h>
  #include <sys/random.h>
  #include <fcntl.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <pthread.h>
  #define CLOSE_SOCKET close
#endif

// strcasestr is not in MSVC's C library; provide a replacement only where needed
#if defined(_WIN32) || defined(__APPLE__)
static const char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    for (; *haystack; ++haystack) {
        if (::tolower((unsigned char)*haystack) == ::tolower((unsigned char)*needle)) {
            const char* h = haystack, *n = needle;
            while (*h && *n && ::tolower((unsigned char)*h) == ::tolower((unsigned char)*n))
                ++h, ++n;
            if (!*n) return haystack;
        }
    }
    return nullptr;
}
#endif // _WIN32 || __APPLE__

#define CUDA_CHECK(expr) do { \
    cudaError_t _e = (expr); \
    if (_e != cudaSuccess) \
        throw std::runtime_error(std::string("CUDA: ") + cudaGetErrorString(_e)); \
} while(0)

namespace symm {

// g_stop is a plain atomic so worker threads can poll it without UB.
// The signal handler sets it via the sig_atomic_t alias; the accept loop
// reads it via std::atomic_load with relaxed order — both are valid on any
// architecture where sig_atomic_t and int share the same width.
static std::atomic<int> g_stop{ 0 };
static void sigHandler(int) { g_stop.store(1, std::memory_order_relaxed); }

// Block SIGINT on the calling thread so that the signal is delivered only
// to the thread that called run() (typically the process main thread).
// Called at the start of each IO-thread lambda.
static void blockSigInt() {
#ifndef _WIN32
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);
#endif
}

// ── Construction / destruction ────────────────────────────────────────────────

GpuServer::GpuServer(const CompiledModule& mod,
                     const std::vector<RouteReg>& routes,
                     const ServerOptions& opts)
    : _mod(mod), _routes(routes), _opts(opts), _tlsPsk(opts.tlsPsk)
{
    // Initialise GpuAllocator singleton — pre-allocates slab + heap backing store
    GpuAllocator::instance();

    _streams = std::make_unique<StreamPool>(opts.streams);
    _io      = std::make_unique<ThreadPool>(opts.ioThreads);

    initPinnedBuffers();

    // ── Debug ring buffer allocation ──────────────────────────────────────────
    _debugRingBuffer = DebugRingBuffer::allocate();
    // Inject the buffer address into the compiled module's _debug_ring_ptr global
    if (_mod.d_debugRingPtr != 0) {
        unsigned long long bufAddr = (unsigned long long)_debugRingBuffer;
        cuMemcpyHtoD(_mod.d_debugRingPtr, &bufAddr, sizeof(unsigned long long));
    }

    // ── Batch dispatch setup ──────────────────────────────────────────────────
    // Only activated when the compiled module has the warp-aligned batch kernel
    // AND the server was started with useBatch = true.
    if (opts.useBatch && mod.dispatchBatch) {
        int n = _streams->size();
        _batchCtxs.resize(n);

        bool ctxOk = true;
        for (int i = 0; i < n; i++) {
            CUresult r = _batchCtxs[i].allocate();
            if (r != CUDA_SUCCESS) {
                const char* msg = nullptr;
                cuGetErrorString(r, &msg);
                std::fprintf(stderr,
                    "[symm] BatchContext[%d] alloc failed (%s) — falling back to single-thread dispatch\n",
                    i, msg ? msg : "?");
                // Free any already-allocated contexts
                for (int j = 0; j < i; j++) _batchCtxs[j].free();
                _batchCtxs.clear();
                ctxOk = false;
                break;
            }
        }

        if (ctxOk) {
            BatchPolicy policy;
            policy.preferredBatchSize = BatchContext::BATCH_CAPACITY;   // 256
            policy.minBatchSize       = BatchContext::WARP_WIDTH;        // 32
            policy.fillTimeoutUs      = 5000;                            // 5 ms
            policy.maxQueueDepth      = 4096;
            policy.maxRoutes          = std::max((int)routes.size() + 1, 256);

            // The launch callback captures `this` — called on the scheduler thread.
            // It acquires a stream slot, runs the GPU batch, downloads results,
            // and sends responses back to clients.
            auto fn = [this](int routeId,
                             const std::vector<BatchRequest>& reqs,
                             BatchResult& result) {
                batchLaunch(routeId, reqs, result);
            };

            _scheduler = std::make_unique<BatchScheduler>(std::move(fn), policy);

            if (opts.verbose) {
                std::printf("[symm] Batch mode enabled — %d BatchContexts × %d slots × 64 KB = %zu MB VRAM\n",
                            n, BatchContext::BATCH_CAPACITY,
                            (size_t)n * BatchContext::BATCH_CAPACITY * BatchContext::MAX_RESP_BYTES / (1024*1024));
            }
        }
    }

    if (opts.verbose) {
        std::printf("[symm] SMs: %d  ·  max threads: %d  ·  streams: %d  ·  io-threads: %d\n",
                    WarpScheduler::smCount(),
                    WarpScheduler::maxParallelism(),
                    _streams->size(),
                    _io->size());
        std::printf("[symm] VRAM slabs: %zu × %zu MB  ·  heap: %zu MB\n",
                    (size_t)MAX_STREAMS, SLAB_SIZE / (1024*1024),
                    HEAP_SIZE / (1024*1024));
        std::printf("[symm] Dispatch path: %s\n",
                    batchModeActive() ? "batch (warp-aligned)" : "single-thread (legacy)");
    }
}

GpuServer::~GpuServer() {
    // Drain and stop the batch scheduler before freeing VRAM backing it.
    // stop() blocks until all in-flight batches have sent their responses.
    if (_scheduler) {
        _scheduler->stop();
        _scheduler.reset();
    }
    for (auto& bctx : _batchCtxs) bctx.free();
    _batchCtxs.clear();

    freePinnedBuffers();
    if (_listenFd >= 0) CLOSE_SOCKET(_listenFd);
#ifdef _WIN32
    WSACleanup();
#endif
}

void GpuServer::initPinnedBuffers() {
    int n = _streams->size();
    _h_resps.resize(n, nullptr);
    for (int i = 0; i < n; ++i)
        CUDA_CHECK(cudaMallocHost(&_h_resps[i], RESP_SIZE));
}

void GpuServer::freePinnedBuffers() {
    for (auto* p : _h_resps) if (p) cudaFreeHost(p);
    _h_resps.clear();
}

// ── Socket ────────────────────────────────────────────────────────────────────

bool GpuServer::initSocket() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;
#endif
    _listenFd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (_listenFd < 0) return false;

    int yes = 1;
    ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));
    // SO_REUSEPORT lets multiple threads call accept() on the same socket
    // (Linux — improves multi-core accept scalability)
#ifndef _WIN32
    ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEPORT,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif

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

    // Backlog = 4× stream count: accept queue can buffer a full stream-pool's worth
    ::listen(_listenFd, _streams->size() * 4);
    return true;
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void GpuServer::run() {
    // Debug: check batch mode status
    {
        FILE* log = fopen("D:\\GITHUB\\SYMMETRA\\mode_debug.log", "w");
        fprintf(log, "dispatchBatch=%p, _scheduler=%p, batchModeActive=%d\n",
                _mod.dispatchBatch, _scheduler.get(), batchModeActive() ? 1 : 0);
        if (_scheduler) {
            fprintf(log, "Scheduler is active - batch mode should be working\n");
        } else {
            fprintf(log, "Scheduler is NULL - batch mode disabled\n");
        }
        fflush(log);
        fclose(log);
    }

    if (!initSocket())
        throw std::runtime_error("GpuServer: failed to bind socket");

    // Install signal handler. On POSIX use sigaction (restartable, reliable);
    // on Windows std::signal is sufficient since signals are not thread-directed.
#ifdef _WIN32
    std::signal(SIGINT, sigHandler);
#else
    struct sigaction sa{};
    sa.sa_handler = sigHandler;
    sa.sa_flags   = SA_RESTART;  // restart interrupted syscalls where possible
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
#endif

    std::printf(SYMM_STR("Listening on :%d  \xC2\xB7  %d routes compiled  "
                         "\xC2\xB7  %d streams  \xC2\xB7  %d io-threads  "
                         "\xC2\xB7  %s\n"),
                _opts.port,
                (int)_routes.size(),
                _streams->size(),
                _io->size(),
                batchModeActive() ? "batch mode" : "single-thread mode");
    std::printf(SYMM_STR("Press Ctrl-C to stop.\n\n"));

    _running = true;

    // Start the batch scheduler thread before accepting connections so it is
    // ready to receive submissions from the accept threads immediately.
    if (_scheduler) _scheduler->start();

    // Block SIGINT on all IO threads so the signal is delivered only to this
    // thread (the one that installed the handler and owns the wait loop).
    bool useBatchPath = batchModeActive();
    for (int i = 0; i < _io->size(); ++i) {
        if (useBatchPath)
            _io->post([this] { blockSigInt(); acceptLoopBatch(); });
        else
            _io->post([this] { blockSigInt(); acceptLoop(); });
    }

    // Main thread waits for Ctrl-C
    while (!g_stop.load(std::memory_order_relaxed)) {
#ifdef _WIN32
        Sleep(100);
#else
        struct timespec ts{ 0, 100'000'000 };
        nanosleep(&ts, nullptr);
#endif
    }

    _running = false;
    std::puts(SYMM_STR("\nDone."));
}

// ── Accept loop (runs on each IO thread) ─────────────────────────────────────

void GpuServer::acceptLoop() {
    while (_running.load(std::memory_order_relaxed)
           && !g_stop.load(std::memory_order_relaxed)) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        int connFd = static_cast<int>(
            ::accept(_listenFd,
                     reinterpret_cast<sockaddr*>(&clientAddr),
                     &clientLen));
        if (connFd < 0) {
            if (g_stop.load(std::memory_order_relaxed)) return;
            continue;
        }

        // ── Rate limiting per-IP ────────────────────────────────────────────
        std::string clientIp = getClientIp(clientAddr);
        if (!checkRateLimit(clientIp)) {
            // Rate limit exceeded — respond with 429
            const char* err = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            CLOSE_SOCKET(connFd);
            continue;
        }

        // Acquire a CUDA stream slot — blocks if all streams are busy.
        // With ioThreads == streams, every accept immediately gets a slot.
        StreamPool::Slot slot = _streams->acquire();

        try {
            handleConn(connFd, slot);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "GpuServer: handleConn: %s\n", e.what());
        }

        // Sync stream, reset O(1) VRAM slab, return stream to pool.
        GpuAllocator::instance().requestFree(slot.idx);
        _streams->release(slot);
        CLOSE_SOCKET(connFd);
    }
}

// ── Per-request GPU dispatch ──────────────────────────────────────────────────
// Handles one connection, supporting HTTP/1.1 keep-alive: loops until the
// client sends Connection: close, closes the socket, or recv fails.

void GpuServer::handleConn(int connFd, StreamPool::Slot slot) {
    static thread_local char rawBuf[128 * 1024];
    auto& alloc = GpuAllocator::instance();

    // ── TLS session for this connection ──────────────────────────────────
    TlsSession tlsSess = createTlsSession();

    for (;;) {
        // ── 1. Receive full request (loop until headers+body are in buffer) ──
        int nRead = recvAll(connFd, rawBuf, sizeof(rawBuf) - 1);
        if (nRead <= 0) return;   // connection closed or error
        rawBuf[nRead] = '\0';

        // ── 1a. TLS decryption (if enabled and after handshake) ──────────────
        std::string httpData;
        if (tlsSess.tlsEnabled) {
            if (!tlsSess.handshakeDone) {
                // First message: client hello with PSK
                if (!handleTlsClientHello(connFd, tlsSess, "")) {
                    return;
                }
                // After handshake, expect next record encrypted
                continue;
            }
            // Decrypt the TLS record
            httpData = tlsDecryptRecord(tlsSess, std::string(rawBuf, nRead));
            if (httpData.empty()) {
                const char* err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
                ::send(connFd, err, static_cast<int>(strlen(err)), 0);
                return;
            }
        } else {
            httpData = std::string(rawBuf, nRead);
        }

        // ── 2. CPU HTTP parse ─────────────────────────────────────────────
        ParsedRequest req{};
        if (!parseHttp(httpData.c_str(), static_cast<int>(httpData.size()), req)) {
            const char* err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            return;
        }

        // ── Enforce request body size cap ──────────────────────────────────────
        if (static_cast<size_t>(req.bodyLen) > BODY_SIZE) {
            const char* err = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            return;
        }

        // ── Route caching for keep-alive (10-20% improvement) ──────────────────
        int64_t nowUs = GpuServer::nowUs();
        int cacheId = connFd % RouteCache::MAX_CONNECTIONS;  // Map fd to cache slot
        int routeId = _routeCache.lookup(cacheId, nowUs);

        if (routeId < 0) {
            routeId = matchRoute(req.method, req.path);
            if (routeId >= 0) {
                _routeCache.store(cacheId, routeId, nowUs);
            }
        }

        // ── 3. Allocate VRAM from this stream's slab ──────────────────────
        size_t bodyUpload = req.bodyLen > 0 ? static_cast<size_t>(req.bodyLen) : 0;
        CUdeviceptr d_body = bodyUpload > 0
            ? alloc.requestAlloc(slot.idx, bodyUpload)
            : 0;
        CUdeviceptr d_resp    = alloc.requestAlloc(slot.idx, RESP_SIZE);
        CUdeviceptr d_respLen = alloc.requestAlloc(slot.idx, sizeof(uint32_t));

        // Allocate VRAM for request header strings (small, use slab)
        static constexpr size_t HDR_BUF = 512;
        CUdeviceptr d_auth  = alloc.requestAlloc(slot.idx, HDR_BUF);
        CUdeviceptr d_ctype = alloc.requestAlloc(slot.idx, HDR_BUF);
        CUdeviceptr d_ua    = alloc.requestAlloc(slot.idx, HDR_BUF);

        // ── 4. Upload body + zero response buffer + upload header strings ─
        if (d_body && req.bodyStart) {
            CUDA_CHECK(cudaMemcpyAsync(
                reinterpret_cast<void*>(d_body),
                req.bodyStart, bodyUpload,
                cudaMemcpyHostToDevice, slot.stream));
        }
        CUDA_CHECK(cudaMemsetAsync(reinterpret_cast<void*>(d_resp),    0, RESP_SIZE,        slot.stream));
        CUDA_CHECK(cudaMemsetAsync(reinterpret_cast<void*>(d_respLen), 0, sizeof(uint32_t), slot.stream));

        // Upload header strings (zero-terminated, capped at HDR_BUF-1)
        auto uploadHdrStr = [&](CUdeviceptr dst, const std::string& s) {
            if (!dst) return;
            size_t n = std::min(s.size(), HDR_BUF - 1);
            CUDA_CHECK(cudaMemcpyAsync(reinterpret_cast<void*>(dst),
                s.c_str(), n + 1, cudaMemcpyHostToDevice, slot.stream));
        };
        uploadHdrStr(d_auth,  req.authorization);
        uploadHdrStr(d_ctype, req.contentType);
        uploadHdrStr(d_ua,    req.userAgent);

        // ── 5. Launch handler kernel ──────────────────────────────────────
        // Refresh GPU timestamp before launch (10 ms accuracy)
        if (_mod.d_nowMs) {
            uint64_t t = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            cuMemcpyHtoDAsync(_mod.d_nowMs, &t, sizeof(uint64_t), slot.stream);
        }
        int respStatus = 404;
        if (routeId >= 0 && _mod.dispatch) {
            void* args[] = { &routeId, &d_body, &req.bodyLen, &d_resp, &d_respLen,
                             &d_auth, &d_ctype, &d_ua };
            CUresult kr = cuLaunchKernel(
                _mod.dispatch,
                1, 1, 1,
                1, 1, 1,
                0, slot.stream,
                args, nullptr);
            if (kr != CUDA_SUCCESS) {
                const char* msg = nullptr;
                cuGetErrorString(kr, &msg);
                std::fprintf(stderr, "GpuServer: kernel: %s\n", msg ? msg : "?");
            } else {
                respStatus = 200;
            }
        }

        // ── Sync with timeout watchdog ────────────────────────────────────
        // Poll stream status with KERNEL_TIMEOUT_MS limit to detect hangs.
        // If kernel exceeds timeout, respond with 500 error instead of hanging.
        auto start = std::chrono::steady_clock::now();
        bool timedOut = false;
        const char* body = nullptr;
        int bodyLen = 0;
        void*    h_resp    = _h_resps[slot.idx];
        uint32_t h_respLen = 0;

        while (true) {
            cudaError_t err = cudaStreamQuery(slot.stream);
            if (err == cudaSuccess) {
                // Stream is idle — all work done
                break;
            } else if (err == cudaErrorNotReady) {
                // Stream still has pending work — check timeout
                auto elapsed = std::chrono::steady_clock::now() - start;
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                if (elapsedMs > KERNEL_TIMEOUT_MS) {
                    timedOut = true;
                    std::fprintf(stderr, "[symm] Kernel timeout on routeId %d after %lld ms\n", routeId, (long long)elapsedMs);
                    // Note: we cannot safely abort the kernel mid-flight; best effort is to
                    // timeout the sync and respond 500 to the client. The kernel may still be
                    // writing to buffers. This is a rare failure mode (infinite loop/deadlock).
                    respStatus = 500;
                    break;
                }
                // Small sleep to avoid busy-loop
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // CUDA error during query — bail out
                std::fprintf(stderr, "GpuServer: stream query error\n");
                respStatus = 500;
                break;
            }
        }

        // If timeout occurred, skip D2H copy (kernel is still running / hung)
        if (!timedOut) {
            // ── 6. Async D2H: copy response body + length scalar ─────────────
            CUDA_CHECK(cudaMemcpyAsync(h_resp, reinterpret_cast<void*>(d_resp),
                                       RESP_SIZE, cudaMemcpyDeviceToHost, slot.stream));
            CUDA_CHECK(cudaMemcpyAsync(&h_respLen, reinterpret_cast<void*>(d_respLen),
                                       sizeof(uint32_t), cudaMemcpyDeviceToHost, slot.stream));

            // Sync only THIS stream — all other streams keep running
            CUDA_CHECK(cudaStreamSynchronize(slot.stream));
        } else {
            // Timed out — respond with error and skip response copy
            body = "{\"error\":\"Internal Server Error\"}";
            bodyLen = static_cast<int>(strlen(body));
            respStatus = 500;
        }

        // ── 7. Build and send HTTP response ──────────────────────────────
        const char* notFound = "{\"error\":\"Not Found\"}";

        // Response headers written by kernel to last 256 bytes of resp_buf
        const char* respExtraHeaders = nullptr;
        if (routeId < 0) {
            body    = notFound;
            bodyLen = static_cast<int>(strlen(notFound));
            respStatus = 404;
        } else {
            body    = static_cast<const char*>(h_resp);
            // h_respLen written by the kernel; fall back to strlen only if zero
            // (indicates the kernel didn't set it — older compiled modules).
            bodyLen = (h_respLen > 0 && h_respLen < RESP_SIZE - 256)
                    ? static_cast<int>(h_respLen)
                    : static_cast<int>(strnlen(body, RESP_SIZE - 256));
            // Custom response headers are packed at resp_buf[RESP_SIZE-256..RESP_SIZE-1]
            const char* hdrTail = static_cast<const char*>(h_resp) + (RESP_SIZE - 256);
            if (hdrTail[0] != '\0') respExtraHeaders = hdrTail;
        }

        bool keepAlive = wantsKeepAlive(req);
        std::string resp = buildResponse(respStatus, body, bodyLen, keepAlive, respExtraHeaders);

        // ── TLS encryption (if enabled) ──────────────────────────────────────
        if (tlsSess.tlsEnabled && tlsSess.handshakeDone) {
            std::string encrypted = tlsEncryptRecord(tlsSess, reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
            if (!encrypted.empty()) {
                ::send(connFd, encrypted.c_str(), static_cast<int>(encrypted.size()), 0);
            }
        } else {
            ::send(connFd, resp.c_str(), static_cast<int>(resp.size()), 0);
        }

        // ── 8. Reset VRAM slab for next request on this slot ─────────────
        // O(1): bump pointer reset only — no per-allocation work.
        alloc.requestFree(slot.idx);

        if (!keepAlive) {
            _routeCache.invalidate(cacheId);
            return;
        }
    }
}

// ── Batch accept loop ─────────────────────────────────────────────────────────
// Each IO thread accepts connections, reads one HTTP request per connection,
// and hands it to the BatchScheduler.  The scheduler collects requests for the
// same route until either the batch is full (256 slots) or the 5 ms fill
// deadline expires, then fires them all as a single GPU warp-aligned batch.
//
// fd ownership is transferred to the BatchRequest — this function never closes
// connFd.  sendBatchResponses() closes it after the response is written.

void GpuServer::acceptLoopBatch() {
    FILE* log = fopen("D:\\GITHUB\\SYMMETRA\\acceptloop_debug.log", "a");
    fprintf(log, "[acceptLoopBatch] Starting...\n");
    fflush(log);
    static thread_local char rawBuf[128 * 1024];

    while (_running.load(std::memory_order_relaxed)
           && !g_stop.load(std::memory_order_relaxed)) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        int connFd = static_cast<int>(
            ::accept(_listenFd,
                     reinterpret_cast<sockaddr*>(&clientAddr),
                     &clientLen));
        if (connFd < 0) {
            if (g_stop.load(std::memory_order_relaxed)) return;
            continue;
        }

        // Read one complete request
        int nRead = recvAll(connFd, rawBuf, sizeof(rawBuf) - 1);
        if (nRead <= 0) {
            CLOSE_SOCKET(connFd);
            continue;
        }
        rawBuf[nRead] = '\0';

        ParsedRequest req{};
        if (!parseHttp(rawBuf, nRead, req)) {
            const char* err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            CLOSE_SOCKET(connFd);
            continue;
        }

        // ── Rate limiting per-IP ────────────────────────────────────────────
        std::string clientIp = getClientIp(clientAddr);
        if (!checkRateLimit(clientIp)) {
            // Rate limit exceeded — respond with 429
            const char* err = "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            CLOSE_SOCKET(connFd);
            continue;
        }

        // ── Enforce request body size cap ──────────────────────────────────────
        if (static_cast<size_t>(req.bodyLen) > BODY_SIZE) {
            const char* err = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            CLOSE_SOCKET(connFd);
            continue;
        }

        int routeId = matchRoute(req.method, req.path);
        if (routeId < 0) {
            // Unknown route — respond immediately without involving the GPU
            const char* nb  = "{\"error\":\"Not Found\"}";
            std::string nr  = buildResponse(404, nb, static_cast<int>(strlen(nb)), false);
            ::send(connFd, nr.c_str(), static_cast<int>(nr.size()), 0);
            CLOSE_SOCKET(connFd);
            continue;
        }

        // Hand off to the batch scheduler.
        // IMPORTANT: do NOT close connFd here — it is now owned by the BatchRequest.
        BatchRequest br;
        br.fd            = connFd;
        br.routeId       = routeId;
        br.method        = req.method;
        br.path          = req.path;
        br.body          = req.bodyStart
                               ? std::string(req.bodyStart, req.bodyLen)
                               : std::string{};
        br.arrivalUs     = nowUs();
        br.authorization = req.authorization;
        br.contentType   = req.contentType;
        br.userAgent     = req.userAgent;

        fprintf(log, "[acceptLoopBatch] Submitting request to scheduler\n");
        fflush(log);
        _scheduler->submitRequest(std::move(br));
        // connFd is now owned by the BatchRequest — do NOT use it below
    }
}

// ── GPU batch launch (called by BatchScheduler on its scheduler thread) ────────
// Uploads the batch to VRAM, runs symm_dispatch_batch, downloads results,
// fills `result`, then sends HTTP responses and closes client fds.

void GpuServer::batchLaunch(int routeId,
                             const std::vector<BatchRequest>& requests,
                             BatchResult& result) {
    static int batch_counter = 0;
    static FILE* debug_log = nullptr;
    if (!debug_log) {
        debug_log = fopen("D:\\GITHUB\\SYMMETRA\\batch_debug.log", "w");
    }

    int batch_id = ++batch_counter;
    if (debug_log) {
        fprintf(debug_log, "[BATCH %d] Starting with %zu requests\n", batch_id, requests.size());
        fflush(debug_log);
    }

    int count  = static_cast<int>(requests.size());
    // Warp-align the thread count — dummy slots are masked by d_active bitmask
    int padded = ((count + BatchContext::WARP_WIDTH - 1)
                  / BatchContext::WARP_WIDTH) * BatchContext::WARP_WIDTH;

    // Acquire a CUDA stream and its matching BatchContext (1:1 by slot index)
    StreamPool::Slot slot = _streams->acquire();
    BatchContext&    bctx = _batchCtxs[slot.idx];

    // Build host pointer arrays pointing into each BatchRequest's storage.
    // These are valid for the duration of uploadBatch() because the requests
    // vector lives in the scheduler's call frame.
    std::vector<const char*> h_bodies(count);
    std::vector<int>         h_lens(count);
    std::vector<const char*> h_paths(count);
    std::vector<const char*> h_methods(count);
    std::vector<const char*> h_auths(count);
    std::vector<const char*> h_ctypes(count);
    std::vector<const char*> h_uas(count);
    for (int i = 0; i < count; i++) {
        h_bodies[i]  = requests[i].body.c_str();
        h_lens[i]    = static_cast<int>(requests[i].body.size());
        h_paths[i]   = requests[i].path.c_str();
        h_methods[i] = requests[i].method.c_str();
        h_auths[i]   = requests[i].authorization.c_str();
        h_ctypes[i]  = requests[i].contentType.c_str();
        h_uas[i]     = requests[i].userAgent.c_str();
    }

    // Upload request data to VRAM (async — queued on slot.stream)
    CUresult ur = bctx.uploadBatch(
        h_bodies.data(), h_lens.data(),
        h_paths.data(),  h_methods.data(),
        h_auths.data(),  h_ctypes.data(), h_uas.data(),
        count, slot.stream);

    if (ur != CUDA_SUCCESS) {
        const char* msg = nullptr;
        cuGetErrorString(ur, &msg);
        std::fprintf(stderr, "[symm] batchLaunch: uploadBatch: %s\n", msg ? msg : "?");
        _streams->release(slot);
        result.responses.assign(count, "{\"error\":\"upload failed\"}");
        result.statuses.assign(count, 500);
        sendBatchResponses(requests, result);
        return;
    }

    // Refresh the GPU-visible timestamp before kernel launch so Date.now() and
    // process.uptime() return accurate values (10 ms granularity from DateNow).
    if (_mod.d_nowMs) {
        uint64_t t = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        cuMemcpyHtoDAsync(_mod.d_nowMs, &t, sizeof(uint64_t), slot.stream);
    }

    // Launch the warp-aligned batch dispatch kernel.
    //   Grid:  1 block
    //   Block: padded threads (multiple of 32, ≤ BATCH_CAPACITY=256)
    //   Each thread handles one request slot; padding threads return early via d_active mask.
    void* args[] = {
        &routeId,
        &bctx.d_bodies,         &bctx.d_body_lens,
        &bctx.d_paths,          &bctx.d_methods,
        &bctx.d_resp_bufs,      &bctx.d_resp_lens, &bctx.d_statuses,
        &bctx.d_active,
        &padded,
        &bctx.d_authorizations, &bctx.d_content_types, &bctx.d_user_agents
    };
    CUresult kr = cuLaunchKernel(
        _mod.dispatchBatch,
        1, 1, 1,          // grid : 1 block
        padded, 1, 1,     // block: warp-aligned thread count
        0, slot.stream,   // shared mem, stream
        args, nullptr);

    result.responses.resize(count);
    result.statuses.resize(count, 500);

    if (kr != CUDA_SUCCESS) {
        const char* msg = nullptr;
        cuGetErrorString(kr, &msg);
        std::fprintf(stderr, "[symm] batchLaunch: cuLaunchKernel: %s\n", msg ? msg : "?");
        for (auto& s : result.statuses)  s = 500;
        for (auto& r : result.responses) r = "{\"error\":\"gpu kernel failed\"}";

        // Error path: queue error responses immediately
        _streams->release(slot);
        _io->post([this, reqs = std::vector<BatchRequest>(requests), res = result]() mutable {
            this->sendBatchResponses(reqs, res);
        });
    } else {
        // Success path: start async download of GPU results WITHOUT BLOCKING
        // The callback will fire when GPU work completes and download results
        if (debug_log) {
            fprintf(debug_log, "[BATCH %d ASYNC] Starting async download for %d requests\n", batch_id, count);
            fflush(debug_log);
        }
        CUresult dr = bctx.downloadResultsAsync(
            requests,
            slot.stream,
            [this, slot](BatchContext::AsyncDownloadContext* ctx) {
                // This callback runs in a CUDA driver thread when GPU work is complete
                // All results are now downloaded to host memory
                fprintf(stderr, "[batchLaunch callback] GPU results downloaded, queueing response send\n");

                // Convert context to BatchResult format
                BatchResult result;
                result.statuses = ctx->resp_statuses;
                result.responses = ctx->resp_bodies;
                result.respHeaders = ctx->resp_headers;

                // Send responses directly from callback (CUDA driver callback thread)
                // Bypass ThreadPool due to potential async-callback synchronization issues
                fprintf(stderr, "[batchLaunch callback] Sending responses directly from callback\n");
                fflush(stderr);

                sendBatchResponses(ctx->requests, result);

                fprintf(stderr, "[batchLaunch callback] Responses sent\n");
                fflush(stderr);

                // Release stream back to pool
                fprintf(stderr, "[batchLaunch callback] Releasing stream\n");
                _streams->release(slot);

                // Context will be freed by downloadResultsAsync
            }
        );

        if (dr != CUDA_SUCCESS) {
            const char* msg = nullptr;
            cuGetErrorString(dr, &msg);
            std::fprintf(stderr, "[symm] batchLaunch: downloadResultsAsync: %s\n", msg ? msg : "?");
            _streams->release(slot);

            // Fallback: send error response
            _io->post([this, reqs = std::vector<BatchRequest>(requests)]() mutable {
                BatchResult errorResult;
                errorResult.statuses.assign(reqs.size(), 500);
                errorResult.responses.assign(reqs.size(), "{\"error\":\"result download failed\"}");
                this->sendBatchResponses(reqs, errorResult);
            });
        }
    }

    // IMPORTANT: Return immediately WITHOUT BLOCKING
    // The scheduler thread can now process the next batch while this batch's
    // GPU work completes in parallel and results are downloaded/sent asynchronously
}

// ── Batch response sender ─────────────────────────────────────────────────────

void GpuServer::sendBatchResponses(const std::vector<BatchRequest>& requests,
                                   const BatchResult& result) {
    static const char* notFound = "{\"error\":\"Not Found\"}";

    fprintf(stderr, "[sendBatchResponses] Called with %zu requests\n", requests.size());
    fprintf(stderr, "[sendBatchResponses] result.statuses.size()=%zu, result.responses.size()=%zu\n",
            result.statuses.size(), result.responses.size());
    fflush(stderr);

    for (size_t i = 0; i < requests.size(); i++) {
        int fd = requests[i].fd;
        fprintf(stderr, "[sendBatchResponses] Request %zu: fd=%d\n", i, fd);
        fflush(stderr);

        if (fd < 0) {
            fprintf(stderr, "[sendBatchResponses] Skipping invalid fd\n");
            fflush(stderr);
            continue;
        }

        int status = (i < result.statuses.size())
                         ? result.statuses[i]
                         : 500;
        const char* body    = nullptr;
        int         bodyLen = 0;

        if (i < result.responses.size() && !result.responses[i].empty()) {
            body    = result.responses[i].c_str();
            bodyLen = static_cast<int>(result.responses[i].size());
            fprintf(stderr, "[sendBatchResponses] Response body (first 100 chars): %.100s\n", body);
            fflush(stderr);
        } else {
            body    = notFound;
            bodyLen = static_cast<int>(strlen(notFound));
            if (status == 200) status = 404;
            fprintf(stderr, "[sendBatchResponses] Using default notFound response\n");
            fflush(stderr);
        }

        fprintf(stderr, "[sendBatchResponses] status=%d, bodyLen=%d\n", status, bodyLen);
        fflush(stderr);

        // Custom response headers packed by kernel into resp tail
        const char* extraHdrs = nullptr;
        if (i < result.respHeaders.size() && !result.respHeaders[i].empty())
            extraHdrs = result.respHeaders[i].c_str();

        // Batch path always closes after the response — HTTP/1.1 keep-alive is
        // handled by the accept loop for persistent connections (future work).
        std::string resp = buildResponse(status, body, bodyLen, /*keepAlive=*/false, extraHdrs);
        fprintf(stderr, "[sendBatchResponses] Built response: %d bytes\n", (int)resp.size());
        fflush(stderr);

        int sendRes = ::send(fd, resp.c_str(), static_cast<int>(resp.size()), 0);
        fprintf(stderr, "[sendBatchResponses] send() returned: %d\n", sendRes);
        fflush(stderr);

        CLOSE_SOCKET(fd);
        fprintf(stderr, "[sendBatchResponses] Closed fd=%d\n", fd);
        fflush(stderr);
    }

    fprintf(stderr, "[sendBatchResponses] Done\n");
    fflush(stderr);
}

// ── TLS 1.3 PSK-only mode (lightweight encryption) ──────────────────────────

GpuServer::TlsSession GpuServer::createTlsSession() {
    TlsSession sess;
    sess.tlsEnabled = !_tlsPsk.empty();
    sess.handshakeDone = false;
    return sess;
}

// Generate cryptographically secure random bytes
static void secureRandomBytes(uint8_t* buf, size_t len) {
#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, 0)) {
        CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    }
    CryptGenRandom(hProv, static_cast<DWORD>(len), buf);
    CryptReleaseContext(hProv, 0);
#else
    // Use getrandom() on Linux, fallback to /dev/urandom on older systems
    ssize_t n = getrandom(buf, len, GRND_NONBLOCK);
    if (n == -1 || static_cast<size_t>(n) < len) {
        // Fallback: read from /dev/urandom
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            ssize_t r = read(fd, buf, len);
            (void)r;
            close(fd);
        }
    }
#endif
}

void GpuServer::tlsDeriveKeys(TlsSession& sess, const uint8_t* serverRandom, const uint8_t* clientRandom) {
    // Key derivation: HMAC-SHA256(PSK, label || server_random || client_random)
    // Improved from simple XOR; still MVP-grade, use proper HKDF-SHA256 in production
    if (_tlsPsk.empty()) return;

    // Decode PSK from hex
    std::string pskHex = _tlsPsk;
    std::array<uint8_t, 32> psk{};
    size_t pskLen = 0;
    for (size_t i = 0; i + 1 < pskHex.length() && pskLen < 32; i += 2) {
        char c1 = pskHex[i], c2 = pskHex[i+1];
        int v = 0;
        if (c1>='0' && c1<='9') v = (c1-'0')<<4; else if (c1>='a' && c1<='f') v = (c1-'a'+10)<<4;
        if (c2>='0' && c2<='9') v |= (c2-'0'); else if (c2>='a' && c2<='f') v |= (c2-'a'+10);
        psk[pskLen++] = v;
    }

    // Improved KDF: HMAC-based instead of simple XOR
    // Create two different input streams for client and server keys
    uint8_t clientInput[48], serverInput[48];
    const char* clientLabel = "client_key";
    const char* serverLabel = "server_key";

    memcpy(clientInput, serverRandom, 16);
    memcpy(clientInput + 16, clientRandom, 16);
    memcpy(clientInput + 32, clientLabel, 10);

    memcpy(serverInput, serverRandom, 16);
    memcpy(serverInput + 16, clientRandom, 16);
    memcpy(serverInput + 32, serverLabel, 10);

    // Use HMAC-based derivation (still MVP, not production-grade)
    protect::hmac_sha256(psk.data(), 32, clientInput, 48, sess.clientKey.data());
    protect::hmac_sha256(psk.data(), 32, serverInput, 48, sess.serverKey.data());

    sess.handshakeDone = true;
}

std::string GpuServer::tlsEncryptRecord(const TlsSession& sess, const uint8_t* plaintext, size_t len) {
    if (!sess.handshakeDone || len > TLS_MAX_RECORD) return "";

    // Generate secure random nonce
    std::array<uint8_t, TLS_NONCE_SIZE> nonce{};
    secureRandomBytes(nonce.data(), TLS_NONCE_SIZE);

    // Encrypt with AES-256-GCM using server key (with authentication)
    protect::Aes256 cipher(sess.serverKey.data());
    std::string encrypted = cipher.gcm_encrypt(plaintext, len, nonce.data());

    // Format: [content_type:1][nonce:12][ciphertext + tag]
    std::string result;
    result.resize(1 + TLS_NONCE_SIZE + encrypted.size());
    result[0] = 0x17;  // TLS content type: application_data
    memcpy(&result[1], nonce.data(), TLS_NONCE_SIZE);
    memcpy(&result[1 + TLS_NONCE_SIZE], encrypted.data(), encrypted.size());

    return result;
}

std::string GpuServer::tlsDecryptRecord(TlsSession& sess, const std::string& encRecord) {
    if (!sess.handshakeDone || encRecord.size() < 1 + TLS_NONCE_SIZE + 16) return "";

    // Extract nonce and ciphertext+tag
    const uint8_t* nonce = reinterpret_cast<const uint8_t*>(encRecord.data()) + 1;
    const uint8_t* ciphertext = nonce + TLS_NONCE_SIZE;
    size_t encLen = encRecord.size() - 1 - TLS_NONCE_SIZE;

    // GCM format: ciphertext + 16-byte tag
    if (encLen < 16) return "";
    size_t cipherLen = encLen - 16;
    const uint8_t* tag = ciphertext + cipherLen;

    // Decrypt and verify with AES-256-GCM using client key
    protect::Aes256 cipher(sess.clientKey.data());
    std::string plaintext = cipher.gcm_decrypt(ciphertext, cipherLen, nonce, nullptr, 0, tag);

    // gcm_decrypt returns empty string on authentication failure
    return plaintext;
}

bool GpuServer::handleTlsClientHello(int fd, TlsSession& sess, const std::string& clientIp) {
    if (!sess.tlsEnabled) return true;  // TLS disabled

    // Send ServerHello + nonces (simplified)
    uint8_t serverRandom[16], clientRandom[16];
    secureRandomBytes(serverRandom, 16);
    secureRandomBytes(clientRandom, 16);

    // Derive session keys from PSK and random nonces
    tlsDeriveKeys(sess, serverRandom, clientRandom);

    // Send TLS ServerHello (simplified: just random + "OK")
    std::string serverHello;
    serverHello += static_cast<char>(0x16);  // Handshake type: ServerHello
    serverHello.append(reinterpret_cast<char*>(serverRandom), 16);

    ::send(fd, serverHello.c_str(), static_cast<int>(serverHello.size()), 0);

    return sess.handshakeDone;
}

// ── Rate limiting per-IP (token bucket algorithm) ────────────────────────────

std::string GpuServer::getClientIp(const sockaddr_in& addr) const {
    char ipBuf[16];
    const char* ip = inet_ntop(AF_INET, &addr.sin_addr, ipBuf, sizeof(ipBuf));
    return ip ? std::string(ip) : "unknown";
}

bool GpuServer::checkRateLimit(const std::string& clientIp) {
    std::lock_guard<std::mutex> lock(_rateLimitMutex);

    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    auto it = _rateLimitMap.find(clientIp);
    if (it == _rateLimitMap.end()) {
        // New IP — initialise with full bucket
        _rateLimitMap[clientIp] = {
            RATE_LIMIT_BUCKET_CAPACITY,
            now
        };
        return true;  // allow first request
    }

    RateLimitEntry& entry = it->second;

    // Calculate how many tokens to refill based on elapsed time
    int64_t elapsedUs = now - entry.lastRefill;
    int tokensToAdd = static_cast<int>(
        (elapsedUs * RATE_LIMIT_TOKENS_PER_SECOND) / 1000000LL);

    if (tokensToAdd > 0) {
        entry.tokens = std::min(
            entry.tokens + tokensToAdd,
            RATE_LIMIT_BUCKET_CAPACITY);
        entry.lastRefill = now;
    }

    // Check if this request can proceed
    if (entry.tokens > 0) {
        entry.tokens--;
        return true;
    }
    return false;  // rate limit exceeded
}

std::vector<std::string> GpuServer::readDebugLogs() {
    if (_debugRingBuffer == 0) return {};
    return DebugRingBuffer::readLogs(_debugRingBuffer);
}

void GpuServer::clearDebugLogs() {
    if (_debugRingBuffer == 0) return;
    DebugRingBuffer::clearBuffer(_debugRingBuffer);
}

// ── Recv loop ─────────────────────────────────────────────────────────────────
// A single recv() may return a partial request if the body is larger than one
// TCP segment. We loop until we have a complete HTTP/1.1 message (headers +
// Content-Length bytes of body), or the buffer fills, or the peer closes.

int GpuServer::recvAll(int fd, char* buf, int cap) const {
    int total = 0;
    int bodyStart = -1;
    int contentLength = 0;

    while (total < cap - 1) {
        int n = static_cast<int>(::recv(fd, buf + total, cap - 1 - total, 0));
        if (n <= 0) return n;   // 0 = closed, <0 = error
        total += n;
        buf[total] = '\0';

        if (bodyStart < 0) {
            // Look for end-of-headers marker
            const char* sep = strstr(buf, "\r\n\r\n");
            if (!sep) continue;
            bodyStart = static_cast<int>(sep + 4 - buf);

            // Parse Content-Length
            const char* cl = strcasestr(buf, "Content-Length:");
            if (cl && cl < sep) {
                contentLength = atoi(cl + 15);
            }
        }

        // Stop once we have all the body bytes the client said it would send
        if (bodyStart >= 0 && total >= bodyStart + contentLength) break;
    }
    return total;
}

// ── CPU route match ───────────────────────────────────────────────────────────

int GpuServer::matchRoute(const std::string& method,
                           const std::string& path) const {
    for (int i = 0; i < (int)_routes.size(); ++i) {
        if (_routes[i].method != method) continue;
        const std::string& rp = _routes[i].path;
        if (rp == path) return i;
        // Param segment: /users/:id matches /users/42
        size_t paramPos = rp.rfind("/:");
        if (paramPos != std::string::npos &&
            path.substr(0, paramPos) == rp.substr(0, paramPos))
            return i;
    }
    return -1;
}

// ── Keep-alive detection ──────────────────────────────────────────────────────

bool GpuServer::wantsKeepAlive(const ParsedRequest& req) const {
    // HTTP/1.1 is keep-alive by default; close only if explicitly requested.
    const auto& it = req.headers.find("connection");
    if (it != req.headers.end()) {
        const std::string& v = it->second;
        if (v.find("close") != std::string::npos) return false;
    }
    return true;
}

// ── HTTP/1.1 parser ───────────────────────────────────────────────────────────

bool GpuServer::parseHttp(const char* buf, int len,
                           ParsedRequest& req) const {
    HttpHeadersView view;
    int result = ZeroCopyHttpParser::parseHttpHeaders(buf, static_cast<size_t>(len), view);

    if (result <= 0) {
        return false;
    }

    if (!ZeroCopyHttpParser::isValidMethod(view)) {
        return false;
    }

    // Validate path against traversal attacks
    if (view.pathLen > 0) {
        for (size_t i = 0; i + 1 < view.pathLen; i++) {
            if (view.path[i] == '.' && view.path[i + 1] == '.') {
                return false;
            }
        }
        // Check for null bytes
        for (size_t i = 0; i < view.pathLen; i++) {
            if (view.path[i] == '\0') {
                return false;
            }
        }
    }

    req.method = std::string(view.method, view.methodLen);
    req.path   = std::string(view.path, view.pathLen);
    if (view.query && view.queryLen > 0) {
        req.query = std::string(view.query, view.queryLen);
    }
    req.bodyStart = view.body;
    req.bodyLen   = static_cast<int>(view.bodyLen);

    // Extract common request headers
    if (view.authorization && view.authorizationLen > 0)
        req.authorization = std::string(view.authorization, view.authorizationLen);
    if (view.contentType && view.contentTypeLen > 0) {
        req.contentType = std::string(view.contentType, view.contentTypeLen);
        req.headers["content-type"] = req.contentType;
    }
    if (view.userAgent && view.userAgentLen > 0)
        req.userAgent = std::string(view.userAgent, view.userAgentLen);

    return true;
}

// ── Response builder ──────────────────────────────────────────────────────────

std::string GpuServer::buildResponse(int status, const char* body, int bodyLen,
                                      bool keepAlive, const char* extraHeaders) const {
    const char* text = (status == 200) ? "OK"                    :
                       (status == 201) ? "Created"               :
                       (status == 204) ? "No Content"            :
                       (status == 301) ? "Moved Permanently"     :
                       (status == 302) ? "Found"                 :
                       (status == 400) ? "Bad Request"           :
                       (status == 401) ? "Unauthorized"          :
                       (status == 403) ? "Forbidden"             :
                       (status == 404) ? "Not Found"             :
                       (status == 405) ? "Method Not Allowed"    :
                       (status == 409) ? "Conflict"              :
                       (status == 422) ? "Unprocessable Entity"  :
                       (status == 429) ? "Too Many Requests"     :
                       (status == 500) ? "Internal Server Error" :
                       (status == 503) ? "Service Unavailable"   : "Unknown";

    // Build base headers
    char hdr[512];
    std::snprintf(hdr, sizeof(hdr),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n"
                  "Connection: %s\r\n",
                  status, text, bodyLen,
                  keepAlive ? "keep-alive" : "close");

    std::string resp = hdr;

    // Append any custom response headers set by ctx.header(name, val)
    // These arrive as "Name: Value\r\n" pairs (already formatted by symm_set_header)
    if (extraHeaders && extraHeaders[0] != '\0') {
        resp += extraHeaders;
    }

    resp += "\r\n";  // end of headers
    resp.append(body, bodyLen);
    return resp;
}

} // namespace symm
