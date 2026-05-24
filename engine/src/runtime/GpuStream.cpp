#include "GpuStream.h"
#include <cuda.h>
#include <cuda_runtime.h>
#include <thread>
#include <stdexcept>
#include <cstdio>

#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  define OPEN_RD(p) _open((p), _O_RDONLY | _O_BINARY)
#  define OPEN_WR(p) _open((p), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644)
#  define READ_FD  _read
#  define WRITE_FD _write
#  define CLOSE_FD _close
#else
#  include <unistd.h>
#  include <fcntl.h>
#  define OPEN_RD(p) open((p), O_RDONLY)
#  define OPEN_WR(p) open((p), O_WRONLY | O_CREAT | O_TRUNC, 0644)
#  define READ_FD  read
#  define WRITE_FD write
#  define CLOSE_FD close
#endif

namespace symm {

// ── VramPipe ──────────────────────────────────────────────────────────────────

VramPipe::VramPipe(int capacity)
    : ring_(capacity), capacity_(capacity) {}

void VramPipe::push(const Chunk& chunk) {
    std::unique_lock<std::mutex> lk(mu_);
    notFull_.wait(lk, [this] { return count_ < capacity_ || closed_.load(); });
    if (closed_.load()) return;
    ring_[tail_] = chunk;
    tail_ = (tail_ + 1) % capacity_;
    ++count_;
    notEmpty_.notify_one();
}

bool VramPipe::pull(Chunk& out) {
    std::unique_lock<std::mutex> lk(mu_);
    notEmpty_.wait(lk, [this] { return count_ > 0 || closed_.load(); });
    if (count_ == 0) return false;   // closed and drained
    out = ring_[head_];
    head_ = (head_ + 1) % capacity_;
    --count_;
    notFull_.notify_one();
    if (out.end) return false;       // end-of-stream sentinel
    return true;
}

void VramPipe::close() {
    closed_.store(true);
    notFull_.notify_all();
    notEmpty_.notify_all();
}

// ── GpuReadable ───────────────────────────────────────────────────────────────

void GpuReadable::pipe(GpuWritable* dest) {
    // Both the producer and consumer share one pipe allocated on the shared_ptr
    // heap so neither thread can outlive it. The outer thread detaches and runs
    // until both producer.join() and the consumer loop complete.
    std::thread([this, dest] {
        auto pipe = std::make_shared<VramPipe>(8);

        std::thread producer([this, pipe] {
            bool ok = true;
            while (ok) {
                try { ok = produce(*pipe); }
                catch (const std::exception& e) {
                    if (onError) onError(e.what());
                    ok = false;
                }
            }
            // Signal end-of-stream then close so the consumer loop exits cleanly.
            pipe->push({ 0, 0, true });
            pipe->close();
        });

        Chunk chunk;
        while (pipe->pull(chunk)) {
            bool ok = dest->consume(chunk);
            cuMemFree(chunk.ptr);  // consumer owns chunks pulled from the pipe
            if (!ok) break;
        }

        // Close the pipe before joining so the producer is never permanently
        // blocked on a full pipe with nobody pulling.
        pipe->close();
        producer.join();

        dest->finish();
        if (dest->onFinish) dest->onFinish();
        if (onEnd) onEnd();
    }).detach();
}

// ── GpuTransform ──────────────────────────────────────────────────────────────

GpuTransform::GpuTransform(KernelFn fn, CUstream stream)
    : fn_(std::move(fn)), stream_(stream), ownStream_(stream == nullptr) {
    if (ownStream_) {
        cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING);
    }
}

GpuTransform::~GpuTransform() {
    if (ownStream_ && stream_) {
        cuStreamSynchronize(stream_);
        cuStreamDestroy(stream_);
    }
}

// consume() does NOT free the input chunk — ownership stays with the caller.
// It allocates a new output chunk and pushes it to outputPipe_.
bool GpuTransform::consume(const Chunk& chunk) {
    CUdeviceptr outPtr = 0;
    size_t outLen = 0;

    CUresult r = cuMemAlloc(&outPtr, chunk.len);
    if (r != CUDA_SUCCESS) return false;

    try {
        outLen = fn_(chunk.ptr, chunk.len, outPtr, stream_);
        cuStreamSynchronize(stream_);
    } catch (...) {
        cuMemFree(outPtr);
        return false;
    }

    outputPipe_.push({ outPtr, outLen, false });
    return true;
}

void GpuTransform::finish() {
    outputPipe_.push({ 0, 0, true });
    outputPipe_.close();
}

bool GpuTransform::produce(VramPipe& pipe) {
    Chunk chunk;
    if (!outputPipe_.pull(chunk)) return false;
    pipe.push(chunk);
    return true;
}

// ── pipeline() ────────────────────────────────────────────────────────────────

void PipelineHandle::waitDone() {
    for (auto& t : pumpers) if (t.joinable()) t.join();
}

PipelineHandle pipeline(GpuReadable* source,
                        std::vector<GpuTransform*> transforms,
                        GpuWritable* dest) {
    PipelineHandle h;

    int stages = 1 + (int)transforms.size() + 1;
    std::vector<std::shared_ptr<VramPipe>> pipes;
    for (int i = 0; i < stages - 1; ++i)
        pipes.push_back(std::make_shared<VramPipe>(8));

    // Source pumper → pipes[0]
    h.pumpers.emplace_back([source, p = pipes[0]] {
        bool ok = true;
        while (ok) {
            try { ok = source->produce(*p); }
            catch (...) { ok = false; }
        }
        p->push({ 0, 0, true });
        p->close();
        if (source->onEnd) source->onEnd();
    });

    // Transform pumpers — consume owns nothing; caller (pumper) frees input
    for (int i = 0; i < (int)transforms.size(); ++i) {
        auto in  = pipes[i];
        auto out = pipes[i + 1];
        GpuTransform* tr = transforms[i];

        h.pumpers.emplace_back([tr, in, out] {
            Chunk chunk;
            while (in->pull(chunk)) {
                bool ok = tr->consume(chunk);
                cuMemFree(chunk.ptr);        // input freed here, not inside consume()
                if (!ok) break;
            }
            // Drain remaining chunks if we exited early
            in->close();
            out->push({ 0, 0, true });
            out->close();
        });
    }

    // Sink pumper
    auto lastPipe = pipes.back();
    h.pumpers.emplace_back([dest, lastPipe] {
        Chunk chunk;
        while (lastPipe->pull(chunk)) {
            dest->consume(chunk);
            cuMemFree(chunk.ptr);
        }
        dest->finish();
        if (dest->onFinish) dest->onFinish();
    });

    return h;
}

// ── GpuFileReadable ───────────────────────────────────────────────────────────

GpuFileReadable::GpuFileReadable(const std::string& path, size_t chunkSize)
    : path_(path), chunkSize_(chunkSize) {}

bool GpuFileReadable::produce(VramPipe& pipe) {
    int fd = OPEN_RD(path_.c_str());
    if (fd < 0) {
        if (onError) onError("GpuFileReadable: cannot open file");
        return false;
    }

    std::vector<char> hostBuf(chunkSize_);
    for (;;) {
        int n = static_cast<int>(READ_FD(fd, hostBuf.data(), (int)chunkSize_));
        if (n <= 0) break;

        CUdeviceptr devPtr = 0;
        if (cuMemAlloc(&devPtr, n) != CUDA_SUCCESS) {
            if (onError) onError("GpuFileReadable: cuMemAlloc failed");
            break;
        }
        cuMemcpyHtoD(devPtr, hostBuf.data(), n);
        pipe.push({ devPtr, (size_t)n, false });
    }
    CLOSE_FD(fd);
    return false;   // done producing
}

// ── GpuFileWritable ───────────────────────────────────────────────────────────

GpuFileWritable::GpuFileWritable(const std::string& path) : path_(path) {}

bool GpuFileWritable::consume(const Chunk& chunk) {
    if (fd_ < 0) {
        fd_ = OPEN_WR(path_.c_str());
        if (fd_ < 0) {
            if (onError) onError("GpuFileWritable: cannot open file for writing");
            return false;
        }
    }

    std::vector<char> hostBuf(chunk.len);
    cuMemcpyDtoH(hostBuf.data(), chunk.ptr, chunk.len);
    int written = static_cast<int>(WRITE_FD(fd_, hostBuf.data(), (int)chunk.len));
    return written == static_cast<int>(chunk.len);
}

void GpuFileWritable::finish() {
    if (fd_ >= 0) { CLOSE_FD(fd_); fd_ = -1; }
}

// ── GpuCollector ──────────────────────────────────────────────────────────────

bool GpuCollector::consume(const Chunk& chunk) {
    chunks_.push_back(chunk);
    resultLen_ += chunk.len;
    return true;
}

void GpuCollector::finish() {
    if (chunks_.empty()) return;

    if (cuMemAlloc(&result_, resultLen_) != CUDA_SUCCESS) {
        result_    = 0;
        resultLen_ = 0;
        return;
    }

    size_t offset = 0;
    for (auto& c : chunks_) {
        cuMemcpyDtoD(result_ + offset, c.ptr, c.len);
        offset += c.len;
        cuMemFree(c.ptr);   // collector takes ownership of chunks at finish()
    }
    chunks_.clear();
}

} // namespace symm
