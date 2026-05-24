#include "GpuServer.h"
#include "protect/Obfuscate.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <csignal>
#include <chrono>
#include <cuda_runtime.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socklen_t = int;
  using ssize_t   = int;
  #define CLOSE_SOCKET closesocket
  #pragma comment(lib, "ws2_32.lib")

  // strcasestr is not in the Windows CRT; provide a local replacement.
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
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <pthread.h>
  #define CLOSE_SOCKET close
#endif

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
    : _mod(mod), _routes(routes), _opts(opts)
{
    // Initialise GpuAllocator singleton — pre-allocates slab + heap backing store
    GpuAllocator::instance();

    _streams = std::make_unique<StreamPool>(opts.streams);
    _io      = std::make_unique<ThreadPool>(opts.ioThreads);

    initPinnedBuffers();

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

    for (;;) {
        // ── 1. Receive full request (loop until headers+body are in buffer) ──
        int nRead = recvAll(connFd, rawBuf, sizeof(rawBuf) - 1);
        if (nRead <= 0) return;   // connection closed or error
        rawBuf[nRead] = '\0';

        // ── 2. CPU HTTP parse ─────────────────────────────────────────────
        ParsedRequest req{};
        if (!parseHttp(rawBuf, nRead, req)) {
            const char* err = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            ::send(connFd, err, static_cast<int>(strlen(err)), 0);
            return;
        }

        int routeId = matchRoute(req.method, req.path);

        // ── 3. Allocate VRAM from this stream's slab ──────────────────────
        size_t bodyUpload = req.bodyLen > 0 ? static_cast<size_t>(req.bodyLen) : 0;
        CUdeviceptr d_body = bodyUpload > 0
            ? alloc.requestAlloc(slot.idx, bodyUpload)
            : 0;
        CUdeviceptr d_resp    = alloc.requestAlloc(slot.idx, RESP_SIZE);
        CUdeviceptr d_respLen = alloc.requestAlloc(slot.idx, sizeof(uint32_t));

        // ── 4. Upload body + zero response buffer ─────────────────────────
        if (d_body && req.bodyStart) {
            CUDA_CHECK(cudaMemcpyAsync(
                reinterpret_cast<void*>(d_body),
                req.bodyStart, bodyUpload,
                cudaMemcpyHostToDevice, slot.stream));
        }
        CUDA_CHECK(cudaMemsetAsync(reinterpret_cast<void*>(d_resp),    0, RESP_SIZE,        slot.stream));
        CUDA_CHECK(cudaMemsetAsync(reinterpret_cast<void*>(d_respLen), 0, sizeof(uint32_t), slot.stream));

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
            void* args[] = { &routeId, &d_body, &req.bodyLen, &d_resp, &d_respLen };
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

        // ── 6. Async D2H: copy response body + length scalar ─────────────
        void*    h_resp    = _h_resps[slot.idx];
        uint32_t h_respLen = 0;
        CUDA_CHECK(cudaMemcpyAsync(h_resp, reinterpret_cast<void*>(d_resp),
                                   RESP_SIZE, cudaMemcpyDeviceToHost, slot.stream));
        CUDA_CHECK(cudaMemcpyAsync(&h_respLen, reinterpret_cast<void*>(d_respLen),
                                   sizeof(uint32_t), cudaMemcpyDeviceToHost, slot.stream));

        // Sync only THIS stream — all other streams keep running
        CUDA_CHECK(cudaStreamSynchronize(slot.stream));

        // ── 7. Build and send HTTP response ──────────────────────────────
        const char* body    = nullptr;
        int         bodyLen = 0;
        const char* notFound = "{\"error\":\"Not Found\"}";

        if (routeId < 0) {
            body    = notFound;
            bodyLen = static_cast<int>(strlen(notFound));
            respStatus = 404;
        } else {
            body    = static_cast<const char*>(h_resp);
            // h_respLen written by the kernel; fall back to strlen only if zero
            // (indicates the kernel didn't set it — older compiled modules).
            bodyLen = (h_respLen > 0 && h_respLen < RESP_SIZE)
                    ? static_cast<int>(h_respLen)
                    : static_cast<int>(strnlen(body, RESP_SIZE));
        }

        bool keepAlive = wantsKeepAlive(req);
        std::string resp = buildResponse(respStatus, body, bodyLen, keepAlive);
        ::send(connFd, resp.c_str(), static_cast<int>(resp.size()), 0);

        // ── 8. Reset VRAM slab for next request on this slot ─────────────
        // O(1): bump pointer reset only — no per-allocation work.
        alloc.requestFree(slot.idx);

        if (!keepAlive) return;
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
        br.fd        = connFd;
        br.routeId   = routeId;
        br.method    = req.method;
        br.path      = req.path;
        br.body      = req.bodyStart
                           ? std::string(req.bodyStart, req.bodyLen)
                           : std::string{};
        br.arrivalUs = nowUs();

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
    for (int i = 0; i < count; i++) {
        h_bodies[i]  = requests[i].body.c_str();
        h_lens[i]    = static_cast<int>(requests[i].body.size());
        h_paths[i]   = requests[i].path.c_str();
        h_methods[i] = requests[i].method.c_str();
    }

    // Upload request data to VRAM (async — queued on slot.stream)
    CUresult ur = bctx.uploadBatch(
        h_bodies.data(), h_lens.data(),
        h_paths.data(), h_methods.data(),
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
        &bctx.d_bodies,   &bctx.d_body_lens,
        &bctx.d_paths,    &bctx.d_methods,
        &bctx.d_resp_bufs,&bctx.d_resp_lens, &bctx.d_statuses,
        &bctx.d_active,
        &padded
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
    } else {
        // Download response lengths, status codes, and bodies.
        // downloadResults() synchronises slot.stream internally.
        std::vector<unsigned int> lens;
        bctx.downloadResults(lens, result.statuses, result.responses, slot.stream);
    }

    // Return stream to the pool.
    // BatchContext owns its own permanent VRAM — the slab allocator is NOT used
    // in the batch path, so we skip GpuAllocator::requestFree here.
    // Other streams continue running; only this stream was synchronised.
    _streams->release(slot);

    // Send HTTP responses to all client fds and close them.
    sendBatchResponses(requests, result);
}

// ── Batch response sender ─────────────────────────────────────────────────────

void GpuServer::sendBatchResponses(const std::vector<BatchRequest>& requests,
                                   const BatchResult& result) {
    static const char* notFound = "{\"error\":\"Not Found\"}";

    for (size_t i = 0; i < requests.size(); i++) {
        int fd = requests[i].fd;
        if (fd < 0) continue;   // shouldn't happen, but guard anyway

        int status = (i < result.statuses.size())
                         ? result.statuses[i]
                         : 500;
        const char* body    = nullptr;
        int         bodyLen = 0;

        if (i < result.responses.size() && !result.responses[i].empty()) {
            body    = result.responses[i].c_str();
            bodyLen = static_cast<int>(result.responses[i].size());
        } else {
            body    = notFound;
            bodyLen = static_cast<int>(strlen(notFound));
            if (status == 200) status = 404;
        }

        // Batch path always closes after the response — HTTP/1.1 keep-alive is
        // handled by the accept loop for persistent connections (future work).
        std::string resp = buildResponse(status, body, bodyLen, /*keepAlive=*/false);
        ::send(fd, resp.c_str(), static_cast<int>(resp.size()), 0);
        CLOSE_SOCKET(fd);
    }
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
    const char* end = buf + len;
    const char* p   = buf;

    // Method
    const char* sp = static_cast<const char*>(memchr(p, ' ', end - p));
    if (!sp) return false;
    req.method = std::string(p, sp);
    p = sp + 1;

    // Path (+ query)
    sp = static_cast<const char*>(memchr(p, ' ', end - p));
    if (!sp) return false;
    std::string fullPath(p, sp);
    size_t qpos = fullPath.find('?');
    if (qpos != std::string::npos) {
        req.path  = fullPath.substr(0, qpos);
        req.query = fullPath.substr(qpos + 1);
    } else {
        req.path  = fullPath;
    }

    // Headers (skip request line)
    const char* lineStart = static_cast<const char*>(memchr(p, '\n', end - p));
    if (!lineStart) return false;
    lineStart++;   // past the '\n' of the request line

    while (lineStart < end - 1) {
        if (lineStart[0] == '\r' && lineStart[1] == '\n') {
            lineStart += 2;  // end of headers
            break;
        }
        const char* colon = static_cast<const char*>(memchr(lineStart, ':', end - lineStart));
        const char* eol   = static_cast<const char*>(memchr(lineStart, '\n', end - lineStart));
        if (!colon || !eol || colon > eol) { lineStart = eol ? eol + 1 : end; continue; }

        std::string key(lineStart, colon);
        // Lowercase key for case-insensitive lookup
        for (char& c : key) if (c >= 'A' && c <= 'Z') c |= 0x20;

        const char* vs = colon + 1;
        while (vs < eol && (*vs == ' ' || *vs == '\t')) ++vs;
        const char* ve = eol;
        while (ve > vs && (ve[-1] == '\r' || ve[-1] == ' ')) --ve;

        req.headers[std::move(key)] = std::string(vs, ve);
        lineStart = eol + 1;
    }

    // Body
    if (lineStart < end) {
        req.bodyStart = lineStart;
        req.bodyLen   = static_cast<int>(end - lineStart);
    }
    return true;
}

// ── Response builder ──────────────────────────────────────────────────────────

std::string GpuServer::buildResponse(int status, const char* body, int bodyLen,
                                      bool keepAlive) const {
    const char* text = (status == 200) ? "OK"        :
                       (status == 404) ? "Not Found" : "Internal Server Error";
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr),
                  "HTTP/1.1 %d %s\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n"
                  "Connection: %s\r\n"
                  "\r\n",
                  status, text, bodyLen,
                  keepAlive ? "keep-alive" : "close");
    return std::string(hdr) + std::string(body, bodyLen);
}

} // namespace symm
