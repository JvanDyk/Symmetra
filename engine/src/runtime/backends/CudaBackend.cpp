#include "CudaBackend.h"
#include "protect/Obfuscate.h"
#include <cuda_runtime.h>
#include <stdexcept>
#include <cstdio>
#include <chrono>
#include <sstream>
#include <iomanip>

#define CU_CHECK(call) checkCu((call), #call)
#define NVRTC_CHECK(call) checkNvrtc((call), #call)

namespace symm {

// ============================================================================
// Constructor & Destructor
// ============================================================================

CudaBackend::CudaBackend() = default;

CudaBackend::~CudaBackend() {
    if (_context) {
        try {
            shutdown();
        } catch (...) {
            // Ignore errors during destructor
        }
    }
}

// ============================================================================
// Initialization & Cleanup
// ============================================================================

bool CudaBackend::init(int deviceId) {
    try {
        _deviceId = deviceId;
        CU_CHECK(cuInit(0));
        CU_CHECK(cuDeviceGet(&_device, deviceId));
        CU_CHECK(cuDevicePrimaryCtxRetain(&_context, _device));
        CU_CHECK(cuCtxSetCurrent(_context));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

void CudaBackend::shutdown() {
    try {
        if (_context) {
            CU_CHECK(cuCtxSetCurrent(_context));
            for (auto& [handle, module] : _modules) {
                CU_CHECK(cuModuleUnload(module));
            }
            _modules.clear();
            _kernels.clear();
        }
        if (_device) {
            CU_CHECK(cuDevicePrimaryCtxRelease(_device));
        }
        _context = nullptr;
        _device = 0;
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

const char* CudaBackend::getVersion() const {
    static std::string version;
    if (version.empty()) {
        int driverVersion = 0;
        cuDriverGetVersion(&driverVersion);
        int major = driverVersion / 1000;
        int minor = (driverVersion % 1000) / 10;
        std::ostringstream oss;
        oss << major << "." << minor;
        version = oss.str();
    }
    return version.c_str();
}

// ============================================================================
// Device Management
// ============================================================================

int CudaBackend::getDeviceCount() const {
    int count = 0;
    try {
        CU_CHECK(cuDeviceGetCount(&count));
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 0;
    }
    return count;
}

const char* CudaBackend::getDeviceName(int deviceId) const {
    if (deviceId < 0 || deviceId >= (int)_deviceNames.size()) {
        try {
            while ((int)_deviceNames.size() <= deviceId) {
                char name[256] = {0};
                CU_CHECK(cuDeviceGetName(name, sizeof(name), deviceId));
                _deviceNames.push_back(std::string(name));
            }
        } catch (const std::exception& e) {
            _lastError = e.what();
            return "Unknown";
        }
    }
    return _deviceNames[deviceId].c_str();
}

size_t CudaBackend::getDeviceMemory(int deviceId) const {
    try {
        CUdevice dev;
        CU_CHECK(cuDeviceGet(&dev, deviceId));
        size_t bytes = 0;
        CU_CHECK(cuDeviceTotalMem(&bytes, dev));
        return bytes;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 0;
    }
}

size_t CudaBackend::getFreeMemory() const {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        size_t free = 0, total = 0;
        CU_CHECK(cuMemGetInfo(&free, &total));
        return free;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 0;
    }
}

int CudaBackend::getMaxThreadsPerBlock() const {
    try {
        int maxThreads = 0;
        CU_CHECK(cuDeviceGetAttribute(&maxThreads,
                                      CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK,
                                      _device));
        return maxThreads;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 512;  // Conservative default
    }
}

int CudaBackend::getWarpSize() const {
    try {
        int warpSize = 0;
        CU_CHECK(cuDeviceGetAttribute(&warpSize,
                                      CU_DEVICE_ATTRIBUTE_WARP_SIZE,
                                      _device));
        return warpSize;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 32;  // Standard NVIDIA warp size
    }
}

int CudaBackend::getSmCount() const {
    try {
        int smCount = 0;
        CU_CHECK(cuDeviceGetAttribute(&smCount,
                                      CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT,
                                      _device));
        return smCount;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return 0;
    }
}

// ============================================================================
// Memory Management
// ============================================================================

void* CudaBackend::allocDevice(size_t bytes) {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUdeviceptr ptr = 0;
        CU_CHECK(cuMemAlloc(&ptr, bytes));
        return reinterpret_cast<void*>(ptr);
    } catch (const std::exception& e) {
        _lastError = e.what();
        return nullptr;
    }
}

void CudaBackend::freeDevice(void* ptr) {
    if (!ptr) return;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuMemFree(reinterpret_cast<CUdeviceptr>(ptr)));
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

bool CudaBackend::copyToDevice(void* dst, const void* src,
                                size_t bytes, void* stream) {
    if (!src || !dst || bytes == 0) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        if (stream) {
            CU_CHECK(cuMemcpyHtoDAsync(reinterpret_cast<CUdeviceptr>(dst), src, bytes,
                                       reinterpret_cast<CUstream>(stream)));
        } else {
            CU_CHECK(cuMemcpyHtoD(reinterpret_cast<CUdeviceptr>(dst), src, bytes));
        }
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::copyFromDevice(void* dst, void* src,
                                  size_t bytes, void* stream) {
    if (!dst || !src || bytes == 0) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        if (stream) {
            CU_CHECK(cuMemcpyDtoHAsync(dst, reinterpret_cast<CUdeviceptr>(src), bytes,
                                       reinterpret_cast<CUstream>(stream)));
        } else {
            CU_CHECK(cuMemcpyDtoH(dst, reinterpret_cast<CUdeviceptr>(src), bytes));
        }
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::copyDeviceToDevice(void* dst, void* src,
                                      size_t bytes, void* stream) {
    if (!src || !dst || bytes == 0) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        if (stream) {
            CU_CHECK(cuMemcpyDtoDAsync(reinterpret_cast<CUdeviceptr>(dst),
                                       reinterpret_cast<CUdeviceptr>(src), bytes,
                                       reinterpret_cast<CUstream>(stream)));
        } else {
            CU_CHECK(cuMemcpyDtoD(reinterpret_cast<CUdeviceptr>(dst),
                                  reinterpret_cast<CUdeviceptr>(src), bytes));
        }
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::deviceSynchronize() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuCtxSynchronize());
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

void* CudaBackend::getDevicePointer(void* handle) {
    return handle;
}

// ============================================================================
// Compilation
// ============================================================================

std::string CudaBackend::archFlag() const {
    try {
        int major = 0, minor = 0;
        CU_CHECK(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, _device));
        CU_CHECK(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, _device));
        return "--gpu-architecture=sm_" + std::to_string(major) + std::to_string(minor);
    } catch (...) {
        return "--gpu-architecture=sm_86";  // Fallback to RTX 30-series
    }
}

std::string CudaBackend::compileToPtx(const std::string& cudaSrc, const std::string& options) const {
    nvrtcProgram prog = nullptr;
    try {
        NVRTC_CHECK(nvrtcCreateProgram(&prog, cudaSrc.c_str(), "program.cu", 0, nullptr, nullptr));

        std::vector<const char*> opts;
        opts.push_back(archFlag().c_str());
        opts.push_back("--use_fast_math");
        opts.push_back("-default-device");
        opts.push_back("--std=c++17");
        if (!options.empty()) {
            opts.push_back(options.c_str());
        }

        nvrtcResult compResult = nvrtcCompileProgram(prog, (int)opts.size(), opts.data());

        size_t logSize = 0;
        NVRTC_CHECK(nvrtcGetProgramLogSize(prog, &logSize));
        if (logSize > 1) {
            std::string log(logSize, '\0');
            NVRTC_CHECK(nvrtcGetProgramLog(prog, log.data()));
            if (compResult != NVRTC_SUCCESS) {
                _lastError = "Compilation failed: " + log;
                throw std::runtime_error(_lastError);
            }
        }

        size_t ptxSize = 0;
        NVRTC_CHECK(nvrtcGetPTXSize(prog, &ptxSize));
        std::string ptx(ptxSize, '\0');
        NVRTC_CHECK(nvrtcGetPTX(prog, ptx.data()));

        nvrtcDestroyProgram(&prog);

        // Zero PTX immediately after loading for security
        volatile char* pp = const_cast<volatile char*>(ptx.data());
        for (size_t i = 0; i < ptx.size(); ++i) pp[i] = 0;
        ptx.clear();

        return ptx;
    } catch (const std::exception& e) {
        if (prog) nvrtcDestroyProgram(&prog);
        throw;
    }
}

CompiledModule CudaBackend::compile(const char* source, const char* options) {
    CompiledModule mod{};
    try {
        std::string cudaSrc(source ? source : "");
        std::string opts(options ? options : "");

        CU_CHECK(cuCtxSetCurrent(_context));

        // Compile to PTX
        std::string ptx = compileToPtx(cudaSrc, opts);

        // Load PTX into module
        CUmodule cuMod = nullptr;
        CU_CHECK(cuModuleLoadData(&cuMod, ptx.c_str()));

        // Get dispatch kernels
        CUfunction dispatch = nullptr;
        CU_CHECK(cuModuleGetFunction(&dispatch, cuMod, SYMM_STR("symm_dispatch")));

        CUfunction dispatchBatch = nullptr;
        cuModuleGetFunction(&dispatchBatch, cuMod, SYMM_STR("symm_dispatch_batch"));

        mod.handle = reinterpret_cast<void*>(cuMod);
        mod.ptxSource = nullptr;
        mod.ptxSize = 0;
        mod.hipSource = nullptr;
        mod.hipSize = 0;

        // Cache the module
        _modules[mod.handle] = cuMod;

        return mod;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return mod;
    }
}

void* CudaBackend::getKernel(const CompiledModule& module,
                                     const char* kernelName) {
    if (!module.handle || !kernelName) return nullptr;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        auto it = _modules.find(module.handle);
        if (it == _modules.end()) {
            _lastError = "Module not found";
            return nullptr;
        }

        CUfunction func = nullptr;
        CU_CHECK(cuModuleGetFunction(&func, it->second, kernelName));

        void* handle = reinterpret_cast<void*>(func);
        _kernels[handle] = func;
        return handle;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return nullptr;
    }
}

void CudaBackend::freeModule(CompiledModule& module) {
    if (!module.handle) return;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        auto it = _modules.find(module.handle);
        if (it != _modules.end()) {
            CU_CHECK(cuModuleUnload(it->second));
            _modules.erase(it);
        }
        // Remove associated kernels
        auto kit = _kernels.begin();
        while (kit != _kernels.end()) {
            if (kit->second == reinterpret_cast<CUfunction>(module.handle)) {
                kit = _kernels.erase(kit);
            } else {
                ++kit;
            }
        }
        module.handle = nullptr;
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

// ============================================================================
// Kernel Execution
// ============================================================================

bool CudaBackend::launchKernel(void* kernel, const KernelConfig& config,
                                void** kernelArgs, size_t argCount) {
    if (!kernel || !kernelArgs || argCount == 0) return false;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUfunction func = reinterpret_cast<CUfunction>(kernel);
        CU_CHECK(cuLaunchKernel(func,
                                config.grid.gridX, config.grid.gridY, config.grid.gridZ,
                                config.block.blockX, config.block.blockY, config.block.blockZ,
                                config.sharedMemBytes,
                                reinterpret_cast<CUstream>(config.stream),
                                kernelArgs, nullptr));
        CU_CHECK(cuCtxSynchronize());
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::launchKernelAsync(void* kernel, const KernelConfig& config,
                                     void** kernelArgs, size_t argCount) {
    if (!kernel || !kernelArgs || argCount == 0) return false;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUfunction func = reinterpret_cast<CUfunction>(kernel);
        CU_CHECK(cuLaunchKernel(func,
                                config.grid.gridX, config.grid.gridY, config.grid.gridZ,
                                config.block.blockX, config.block.blockY, config.block.blockZ,
                                config.sharedMemBytes,
                                reinterpret_cast<CUstream>(config.stream),
                                kernelArgs, nullptr));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

// ============================================================================
// Stream Management
// ============================================================================

void* CudaBackend::createStream() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUstream stream = nullptr;
        CU_CHECK(cuStreamCreate(&stream, CU_STREAM_DEFAULT));
        return reinterpret_cast<void*>(stream);
    } catch (const std::exception& e) {
        _lastError = e.what();
        return nullptr;
    }
}

void* CudaBackend::createHighPriorityStream() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUstream stream = nullptr;
        CU_CHECK(cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING));
        return reinterpret_cast<void*>(stream);
    } catch (const std::exception& e) {
        _lastError = e.what();
        return nullptr;
    }
}

void CudaBackend::destroyStream(void* stream) {
    if (!stream) return;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuStreamDestroy(reinterpret_cast<CUstream>(stream)));
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

bool CudaBackend::streamSynchronize(void* stream) {
    if (!stream) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuStreamSynchronize(reinterpret_cast<CUstream>(stream)));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::streamIsComplete(void* stream) {
    if (!stream) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUresult result = cuStreamQuery(reinterpret_cast<CUstream>(stream));
        return result == CUDA_SUCCESS;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::addStreamCallback(void* stream, HostCallback callback,
                                     void* userData) {
    if (!stream || !callback) return false;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuStreamAddCallback(reinterpret_cast<CUstream>(stream),
                                     reinterpret_cast<CUstreamCallback>(callback),
                                     userData, 0));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

// ============================================================================
// Memory Pools
// ============================================================================

bool CudaBackend::enableMemoryPool() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        // CUDA memory pooling would be configured here
        // For now, just mark as enabled
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

void CudaBackend::disableMemoryPool() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        // Memory pooling cleanup
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

void CudaBackend::trimMemoryPool() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        // Trim would be done here if memory pooling is enabled
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

// ============================================================================
// Event Management
// ============================================================================

void* CudaBackend::createEvent() {
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CUevent event = nullptr;
        CU_CHECK(cuEventCreate(&event, CU_EVENT_DEFAULT));
        return reinterpret_cast<void*>(event);
    } catch (const std::exception& e) {
        _lastError = e.what();
        return nullptr;
    }
}

bool CudaBackend::recordEvent(void* event, void* stream) {
    if (!event) return false;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuEventRecord(reinterpret_cast<CUevent>(event),
                               reinterpret_cast<CUstream>(stream)));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::waitEvent(void* event) {
    if (!event) return true;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuEventSynchronize(reinterpret_cast<CUevent>(event)));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

bool CudaBackend::getEventElapsedTime(void* start, void* end,
                                       float& elapsedMs) {
    if (!start || !end) return false;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuEventElapsedTime(&elapsedMs,
                                    reinterpret_cast<CUevent>(start),
                                    reinterpret_cast<CUevent>(end)));
        return true;
    } catch (const std::exception& e) {
        _lastError = e.what();
        return false;
    }
}

void CudaBackend::destroyEvent(void* event) {
    if (!event) return;
    try {
        CU_CHECK(cuCtxSetCurrent(_context));
        CU_CHECK(cuEventDestroy(reinterpret_cast<CUevent>(event)));
    } catch (const std::exception& e) {
        _lastError = e.what();
    }
}

// ============================================================================
// Debugging & Profiling
// ============================================================================

void CudaBackend::enableProfiling() {
    _profilingStats = "Profiling enabled";
}

void CudaBackend::disableProfiling() {
    _profilingStats = "";
}

const char* CudaBackend::getProfilingStats() {
    return _profilingStats.empty() ? "" : _profilingStats.c_str();
}

void CudaBackend::resetProfilingStats() {
    _profilingStats = "";
}

// ============================================================================
// Error Handling
// ============================================================================

const char* CudaBackend::getLastError() const {
    return _lastError.empty() ? "No error" : _lastError.c_str();
}

void CudaBackend::clearError() {
    _lastError.clear();
}

// ============================================================================
// Helper Methods
// ============================================================================

void CudaBackend::checkCu(CUresult result, const char* call) const {
    if (result != CUDA_SUCCESS) {
        const char* msg = nullptr;
        cuGetErrorString(result, &msg);
        _lastError = std::string("CUDA error in ") + call + ": " + (msg ? msg : "unknown");
        throw std::runtime_error(_lastError);
    }
}

void CudaBackend::checkNvrtc(nvrtcResult result, const char* call) const {
    if (result != NVRTC_SUCCESS) {
        _lastError = std::string("NVRTC error in ") + call + ": " + nvrtcGetErrorString(result);
        throw std::runtime_error(_lastError);
    }
}

} // namespace symm
