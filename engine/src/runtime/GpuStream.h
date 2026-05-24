#pragma once
#include <cuda.h>
#include <cuda_runtime.h>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstddef>

namespace symm {

// ── Chunk — one unit of streaming data in VRAM ────────────────────────────────

struct Chunk {
    CUdeviceptr ptr  = 0;
    size_t      len  = 0;
    bool        end  = false;  // true = end-of-stream sentinel
};

// ── VramPipe — VRAM-backed queue connecting stream stages ─────────────────────
// Chunks are VRAM pointers — the data never leaves GPU memory between stages.
// CPU side manages ordering via CUDA events.

class VramPipe {
public:
    explicit VramPipe(int capacity = 8);
    ~VramPipe() = default;

    // Producer: push a chunk. Blocks if pipe is full (backpressure).
    void push(const Chunk& chunk);

    // Consumer: pull next chunk. Blocks until one is available.
    bool pull(Chunk& out);

    // Mark the pipe closed — consumers will drain remaining chunks then stop.
    void close();

    bool closed() const { return closed_.load(); }

private:
    std::vector<Chunk>      ring_;
    int                     capacity_;
    int                     head_ = 0;
    int                     tail_ = 0;
    int                     count_= 0;
    std::mutex              mu_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::atomic<bool>       closed_{ false };
};

// ── GpuReadable ───────────────────────────────────────────────────────────────
// Produces VRAM chunks. Subclass and implement produce().

class GpuWritable;

class GpuReadable {
public:
    virtual ~GpuReadable() = default;

    // Pipe this readable into a writable (starts a background pump thread).
    void pipe(GpuWritable* dest);

    // Internal: produce the next chunk into pipe. Return false = end of stream.
    virtual bool produce(VramPipe& pipe) = 0;

    // Called when all data has been pumped.
    std::function<void()> onEnd;
    std::function<void(const char*)> onError;
};

// ── GpuWritable ───────────────────────────────────────────────────────────────
// Consumes VRAM chunks. Subclass and implement consume().

class GpuWritable {
public:
    virtual ~GpuWritable() = default;

    // Internal: consume one chunk. Return false to signal write error.
    virtual bool consume(const Chunk& chunk) = 0;

    // Called once after the last chunk.
    virtual void finish() {}

    std::function<void()> onFinish;
    std::function<void(const char*)> onError;
};

// ── GpuTransform ──────────────────────────────────────────────────────────────
// Reads chunks from an upstream VramPipe, transforms them via a GPU kernel,
// and writes results into a downstream VramPipe.
// Both input and output data stay in VRAM — zero CPU involvement in the data path.

class GpuTransform : public GpuReadable, public GpuWritable {
public:
    // transform: (inPtr, inLen, outPtr) → outLen
    //   Called once per chunk. Runs as a GPU kernel on the transform stream.
    //   outPtr is pre-allocated from GpuAllocator (same size as inLen).
    using KernelFn = std::function<size_t(CUdeviceptr in, size_t inLen,
                                           CUdeviceptr out, CUstream stream)>;

    explicit GpuTransform(KernelFn fn, CUstream stream = nullptr);
    ~GpuTransform();

    bool produce(VramPipe& pipe) override;
    bool consume(const Chunk& chunk) override;
    void finish() override;

private:
    KernelFn   fn_;
    CUstream   stream_;
    bool       ownStream_;
    VramPipe   outputPipe_{ 8 };
};

// ── pipeline() ────────────────────────────────────────────────────────────────
// Connect source → [transforms...] → dest.
// Launches background threads to pump each pipe stage.
// Returns immediately; call waitDone() on the returned handle.

struct PipelineHandle {
    std::vector<std::thread> pumpers;
    void waitDone();
};

PipelineHandle pipeline(GpuReadable* source,
                        std::vector<GpuTransform*> transforms,
                        GpuWritable* dest);

// ── Concrete readable/writable implementations ────────────────────────────────

// Read a file from disk into VRAM chunks
class GpuFileReadable : public GpuReadable {
public:
    explicit GpuFileReadable(const std::string& path, size_t chunkSize = 64*1024);
    bool produce(VramPipe& pipe) override;
private:
    std::string path_;
    size_t      chunkSize_;
};

// Write VRAM chunks to a file
class GpuFileWritable : public GpuWritable {
public:
    explicit GpuFileWritable(const std::string& path);
    bool consume(const Chunk& chunk) override;
    void finish() override;
private:
    std::string path_;
    int         fd_ = -1;
};

// Collect all VRAM chunks into one contiguous VRAM buffer (for ctx.send())
class GpuCollector : public GpuWritable {
public:
    bool consume(const Chunk& chunk) override;
    void finish() override;

    CUdeviceptr result()    const { return result_; }
    size_t      resultLen() const { return resultLen_; }

private:
    CUdeviceptr result_    = 0;
    size_t      resultLen_ = 0;
    std::vector<Chunk> chunks_;
};

} // namespace symm
