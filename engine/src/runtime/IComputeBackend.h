#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace symm {

// Forward declarations
struct Grid {
    unsigned int gridX, gridY, gridZ;
    Grid(unsigned int x = 1, unsigned int y = 1, unsigned int z = 1)
        : gridX(x), gridY(y), gridZ(z) {}
};

struct Block {
    unsigned int blockX, blockY, blockZ;
    Block(unsigned int x = 1, unsigned int y = 1, unsigned int z = 1)
        : blockX(x), blockY(y), blockZ(z) {}
};

// Opaque handle types for backend-specific objects
using StreamHandle = void*;
using KernelHandle = void*;
using ModuleHandle = void*;
using DeviceMemoryHandle = void*;

// Compiled kernel representation
struct CompiledModule {
    ModuleHandle handle;
    const char* ptxSource;  // For CUDA
    size_t ptxSize;
    const char* hipSource;  // For HIP
    size_t hipSize;
};

// Kernel execution configuration
struct KernelConfig {
    Grid grid;
    Block block;
    size_t sharedMemBytes;
    StreamHandle stream;
};

// ============================================================================
// Abstract Compute Backend Interface
// ============================================================================
class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;

    // ────────────────────────────────────────────────────────────────────
    // Initialization & Cleanup
    // ────────────────────────────────────────────────────────────────────

    /// Initialize compute backend on specified device
    /// \param deviceId Device ID (0-based)
    /// \return true if initialization successful
    virtual bool init(int deviceId) = 0;

    /// Shutdown compute backend and free resources
    virtual void shutdown() = 0;

    /// Get backend name ("CUDA", "ROCM", "HIP", etc.)
    virtual const char* getName() const = 0;

    /// Get backend version string
    virtual const char* getVersion() const = 0;

    // ────────────────────────────────────────────────────────────────────
    // Device Management
    // ────────────────────────────────────────────────────────────────────

    /// Get number of available compute devices
    virtual int getDeviceCount() const = 0;

    /// Get device name (e.g., "RTX 4090", "MI300X")
    virtual const char* getDeviceName(int deviceId) const = 0;

    /// Get total device memory in bytes
    virtual size_t getDeviceMemory(int deviceId) const = 0;

    /// Get free device memory in bytes
    virtual size_t getFreeMemory() const = 0;

    /// Get maximum threads per block
    virtual int getMaxThreadsPerBlock() const = 0;

    /// Get warp size (32 for NVIDIA, typically)
    virtual int getWarpSize() const = 0;

    /// Get number of SMs (multiprocessors)
    virtual int getSmCount() const = 0;

    // ────────────────────────────────────────────────────────────────────
    // Memory Management
    // ────────────────────────────────────────────────────────────────────

    /// Allocate device memory
    /// \param bytes Number of bytes to allocate
    /// \return Opaque handle to allocated memory, nullptr on failure
    virtual DeviceMemoryHandle allocDevice(size_t bytes) = 0;

    /// Free device memory
    virtual void freeDevice(DeviceMemoryHandle ptr) = 0;

    /// Copy from host to device (asynchronous)
    /// \param dst Device memory handle
    /// \param src Host memory pointer
    /// \param bytes Number of bytes to copy
    /// \param stream Optional stream for async operation
    /// \return true if successful
    virtual bool copyToDevice(DeviceMemoryHandle dst, const void* src,
                              size_t bytes, StreamHandle stream = nullptr) = 0;

    /// Copy from device to host (asynchronous)
    /// \param dst Host memory pointer
    /// \param src Device memory handle
    /// \param bytes Number of bytes to copy
    /// \param stream Optional stream for async operation
    /// \return true if successful
    virtual bool copyFromDevice(void* dst, DeviceMemoryHandle src,
                                size_t bytes, StreamHandle stream = nullptr) = 0;

    /// Copy device to device memory
    virtual bool copyDeviceToDevice(DeviceMemoryHandle dst, DeviceMemoryHandle src,
                                    size_t bytes, StreamHandle stream = nullptr) = 0;

    /// Synchronize entire device (wait for all operations)
    virtual bool deviceSynchronize() = 0;

    /// Get the native device pointer (for backend-specific operations)
    virtual void* getDevicePointer(DeviceMemoryHandle handle) = 0;

    // ────────────────────────────────────────────────────────────────────
    // Compilation
    // ────────────────────────────────────────────────────────────────────

    /// Compile kernel source code
    /// \param source Language-agnostic kernel source (will be translated)
    /// \param options Compiler options (e.g., "-O3 -arch=sm_80")
    /// \return Compiled module, empty handle if failed
    virtual CompiledModule compile(const char* source, const char* options) = 0;

    /// Load compiled module and extract kernel function
    /// \param module Compiled module
    /// \param kernelName Name of kernel function to extract
    /// \return Kernel handle, nullptr if not found
    virtual KernelHandle getKernel(const CompiledModule& module,
                                   const char* kernelName) = 0;

    /// Free compiled module resources
    virtual void freeModule(CompiledModule& module) = 0;

    // ────────────────────────────────────────────────────────────────────
    // Kernel Execution
    // ────────────────────────────────────────────────────────────────────

    /// Launch kernel synchronously (blocks until kernel completes)
    /// \param kernel Kernel to launch
    /// \param config Grid/block/stream configuration
    /// \param kernelArgs Array of pointers to kernel arguments
    /// \param argCount Number of arguments
    /// \return true if successful
    virtual bool launchKernel(KernelHandle kernel, const KernelConfig& config,
                             void** kernelArgs, size_t argCount) = 0;

    /// Launch kernel asynchronously (returns immediately)
    virtual bool launchKernelAsync(KernelHandle kernel, const KernelConfig& config,
                                   void** kernelArgs, size_t argCount) = 0;

    // ────────────────────────────────────────────────────────────────────
    // Stream Management
    // ────────────────────────────────────────────────────────────────────

    /// Create a new compute stream for concurrent execution
    /// \return Stream handle, nullptr if failed
    virtual StreamHandle createStream() = 0;

    /// Create a high-priority stream
    virtual StreamHandle createHighPriorityStream() = 0;

    /// Destroy a stream
    virtual void destroyStream(StreamHandle stream) = 0;

    /// Synchronize stream (wait for all operations on stream to complete)
    virtual bool streamSynchronize(StreamHandle stream) = 0;

    /// Query if stream is complete (non-blocking)
    virtual bool streamIsComplete(StreamHandle stream) = 0;

    /// Launch callback on stream (execute host function on stream completion)
    using HostCallback = void(*)(void*);
    virtual bool addStreamCallback(StreamHandle stream, HostCallback callback,
                                   void* userData) = 0;

    // ────────────────────────────────────────────────────────────────────
    // Memory Pools (for frequent allocation/deallocation)
    // ────────────────────────────────────────────────────────────────────

    /// Enable memory pooling for efficient allocation
    virtual bool enableMemoryPool() = 0;

    /// Disable memory pooling
    virtual void disableMemoryPool() = 0;

    /// Trim memory pool (free unused cached memory)
    virtual void trimMemoryPool() = 0;

    // ────────────────────────────────────────────────────────────────────
    // Event Management (for fine-grained profiling)
    // ────────────────────────────────────────────────────────────────────

    using EventHandle = void*;

    /// Create an event for timing
    virtual EventHandle createEvent() = 0;

    /// Record event on stream
    virtual bool recordEvent(EventHandle event, StreamHandle stream) = 0;

    /// Synchronize on event
    virtual bool waitEvent(EventHandle event) = 0;

    /// Get elapsed time between two events in milliseconds
    virtual bool getEventElapsedTime(EventHandle start, EventHandle end,
                                    float& elapsedMs) = 0;

    /// Destroy event
    virtual void destroyEvent(EventHandle event) = 0;

    // ────────────────────────────────────────────────────────────────────
    // Debugging & Profiling
    // ────────────────────────────────────────────────────────────────────

    /// Enable device profiling
    virtual void enableProfiling() = 0;

    /// Disable device profiling
    virtual void disableProfiling() = 0;

    /// Get profiling statistics as string
    virtual const char* getProfilingStats() = 0;

    /// Reset profiling counters
    virtual void resetProfilingStats() = 0;

    // ────────────────────────────────────────────────────────────────────
    // Error Handling
    // ────────────────────────────────────────────────────────────────────

    /// Get last error message
    virtual const char* getLastError() const = 0;

    /// Clear error state
    virtual void clearError() = 0;
};

// ============================================================================
// Factory Functions
// ============================================================================

/// Create compute backend by name
/// \param backendName "cuda", "rocm", "hip", etc.
/// \return New backend instance, nullptr if not supported
IComputeBackend* createComputeBackend(const char* backendName);

/// Create default backend (CUDA if available, else first available)
IComputeBackend* createDefaultBackend();

/// Get list of available backends
std::vector<const char*> getAvailableBackends();

}  // namespace symm
