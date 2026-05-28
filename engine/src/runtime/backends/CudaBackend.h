#pragma once
#include "../IComputeBackend.h"
#include <cuda.h>
#include <nvrtc.h>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <array>
#include <mutex>
#include <atomic>

namespace symm {

// ============================================================================
// CUDA Backend Implementation
// ============================================================================

class CudaBackend : public IComputeBackend {
public:
    CudaBackend();
    ~CudaBackend() override;

    // ────────────────────────────────────────────────────────────────────
    // Initialization & Cleanup
    // ────────────────────────────────────────────────────────────────────
    bool init(int deviceId) override;
    void shutdown() override;
    const char* getName() const override { return "CUDA"; }
    const char* getVersion() const override;

    // ────────────────────────────────────────────────────────────────────
    // Device Management
    // ────────────────────────────────────────────────────────────────────
    int getDeviceCount() const override;
    const char* getDeviceName(int deviceId) const override;
    size_t getDeviceMemory(int deviceId) const override;
    size_t getFreeMemory() const override;
    int getMaxThreadsPerBlock() const override;
    int getWarpSize() const override;
    int getSmCount() const override;

    // ────────────────────────────────────────────────────────────────────
    // Memory Management
    // ────────────────────────────────────────────────────────────────────
    DeviceMemoryHandle allocDevice(size_t bytes) override;
    void freeDevice(DeviceMemoryHandle ptr) override;
    bool copyToDevice(DeviceMemoryHandle dst, const void* src,
                      size_t bytes, StreamHandle stream = nullptr) override;
    bool copyFromDevice(void* dst, DeviceMemoryHandle src,
                        size_t bytes, StreamHandle stream = nullptr) override;
    bool copyDeviceToDevice(DeviceMemoryHandle dst, DeviceMemoryHandle src,
                            size_t bytes, StreamHandle stream = nullptr) override;
    bool deviceSynchronize() override;
    void* getDevicePointer(DeviceMemoryHandle handle) override;

    // ────────────────────────────────────────────────────────────────────
    // Compilation
    // ────────────────────────────────────────────────────────────────────
    CompiledModule compile(const char* source, const char* options) override;
    KernelHandle getKernel(const CompiledModule& module,
                           const char* kernelName) override;
    void freeModule(CompiledModule& module) override;

    // ────────────────────────────────────────────────────────────────────
    // Kernel Execution
    // ────────────────────────────────────────────────────────────────────
    bool launchKernel(KernelHandle kernel, const KernelConfig& config,
                      void** kernelArgs, size_t argCount) override;
    bool launchKernelAsync(KernelHandle kernel, const KernelConfig& config,
                           void** kernelArgs, size_t argCount) override;

    // ────────────────────────────────────────────────────────────────────
    // Stream Management
    // ────────────────────────────────────────────────────────────────────
    StreamHandle createStream() override;
    StreamHandle createHighPriorityStream() override;
    void destroyStream(StreamHandle stream) override;
    bool streamSynchronize(StreamHandle stream) override;
    bool streamIsComplete(StreamHandle stream) override;
    bool addStreamCallback(StreamHandle stream, HostCallback callback,
                           void* userData) override;

    // ────────────────────────────────────────────────────────────────────
    // Memory Pools
    // ────────────────────────────────────────────────────────────────────
    bool enableMemoryPool() override;
    void disableMemoryPool() override;
    void trimMemoryPool() override;

    // ────────────────────────────────────────────────────────────────────
    // Event Management
    // ────────────────────────────────────────────────────────────────────
    EventHandle createEvent() override;
    bool recordEvent(EventHandle event, StreamHandle stream) override;
    bool waitEvent(EventHandle event) override;
    bool getEventElapsedTime(EventHandle start, EventHandle end,
                             float& elapsedMs) override;
    void destroyEvent(EventHandle event) override;

    // ────────────────────────────────────────────────────────────────────
    // Debugging & Profiling
    // ────────────────────────────────────────────────────────────────────
    void enableProfiling() override;
    void disableProfiling() override;
    const char* getProfilingStats() override;
    void resetProfilingStats() override;

    // ────────────────────────────────────────────────────────────────────
    // Error Handling
    // ────────────────────────────────────────────────────────────────────
    const char* getLastError() const override;
    void clearError() override;

private:
    // ── Device context ─────────────────────────────────────────────────
    CUdevice _device = 0;
    CUcontext _context = nullptr;
    int _deviceId = -1;
    mutable std::string _lastError;
    bool _initialized = false;

    // ── Compilation state ─────────────────────────────────────────────
    std::map<ModuleHandle, CUmodule> _modules;
    std::map<KernelHandle, CUfunction> _kernels;
    mutable std::string _profilingStats;
    mutable std::mutex _moduleMutex;
    mutable std::mutex _kernelMutex;

    // ── Device info cache ─────────────────────────────────────────────
    mutable std::vector<std::string> _deviceNames;
    mutable std::mutex _deviceNameMutex;

    // ── Resource tracking ─────────────────────────────────────────────
    mutable std::atomic<size_t> _totalAllocated{0};
    mutable std::atomic<size_t> _peakAllocated{0};
    mutable std::atomic<int> _activeStreams{0};
    mutable std::atomic<int> _activeEvents{0};

    // ── Helper methods ─────────────────────────────────────────────────
    void checkCu(CUresult result, const char* call) const;
    void checkNvrtc(nvrtcResult result, const char* call) const;
    std::string archFlag() const;
    std::string compileToPtx(const std::string& cudaSrc, const std::string& options) const;
    bool isInitialized() const { return _initialized; }
    void validateInitialized(const char* method) const;
};

} // namespace symm
