#include "NvrtcCompiler.h"
#include "protect/Obfuscate.h"
#include "DateNow.h"
#include <stdexcept>
#include <cstdio>
#include <chrono>
#include <cuda_runtime.h>

#define NVRTC_CHECK(call) checkNvrtc((call), #call)
#define CU_CHECK(call)    checkCu((call), #call)

namespace symm {

NvrtcCompiler::NvrtcCompiler() {
    CU_CHECK(cuInit(0));
    CU_CHECK(cuDeviceGet(&_device, 0));
    CU_CHECK(cuDevicePrimaryCtxRetain(&_context, _device));
    CU_CHECK(cuCtxSetCurrent(_context));
}

NvrtcCompiler::~NvrtcCompiler() {
    if (_device) cuDevicePrimaryCtxRelease(_device);
}

std::string NvrtcCompiler::archFlag() const {
    int major = 0, minor = 0;
    cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, _device);
    cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, _device);
    return "--gpu-architecture=sm_" + std::to_string(major) + std::to_string(minor);
}

CompiledModule NvrtcCompiler::compile(const std::string& cudaSrc,
                                      const std::string& srcName,
                                      bool verbose) {
    nvrtcProgram prog;
    // srcName is the user's .ts filename — appears in their error messages.
    // The internal virtual path is kept short; the label is developer-facing only.
    std::string label = srcName.empty() ? SYMM_STR("program") : srcName;
    NVRTC_CHECK(nvrtcCreateProgram(&prog,
                                   cudaSrc.c_str(),
                                   label.c_str(),
                                   0, nullptr, nullptr));

    std::string arch = archFlag();
    const char* opts[] = {
        arch.c_str(),
        "--use_fast_math",
        "-default-device",
        "--std=c++17",
    };

    nvrtcResult compResult = nvrtcCompileProgram(prog, 4, opts);

    // Always fetch compile log (may contain warnings or errors)
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    nvrtcGetProgramLog(prog, log.data());

    if (compResult != NVRTC_SUCCESS) {
        nvrtcDestroyProgram(&prog);
        // Show the compilation errors against the user's source name.
        // Hint about --verbose so they can inspect the generated GPU code.
        std::string msg = "Error compiling " + srcName + ":\n" + log;
        if (!verbose)
            msg += "\nRun with --verbose to see the generated GPU code.\n";
        throw std::runtime_error(msg);
    }
    if (verbose && logSize > 1) {
        std::fprintf(stderr, "── compile log ──\n%s\n────\n", log.c_str());
    }

    // Extract PTX
    size_t ptxSize = 0;
    NVRTC_CHECK(nvrtcGetPTXSize(prog, &ptxSize));
    std::string ptx(ptxSize, '\0');
    NVRTC_CHECK(nvrtcGetPTX(prog, ptx.data()));
    nvrtcDestroyProgram(&prog);

    if (verbose) {
        std::printf("── PTX size: %zu bytes\n", ptxSize);
    }

    // Load PTX into a CUmodule
    CUmodule cuMod = nullptr;
    CU_CHECK(cuModuleLoadData(&cuMod, ptx.c_str()));

    // Zero plaintext PTX immediately after loading — the driver has already JIT-
    // compiled it into a cubin. Leaving it in heap memory allows it to be read
    // via a memory-dump attack even after the module is loaded.
    volatile char* pp = const_cast<volatile char*>(ptx.data());
    for (size_t i = 0; i < ptx.size(); ++i) pp[i] = 0;
    ptx.clear();

    // Retrieve the single-thread dispatch kernel (legacy path)
    CUfunction dispatch = nullptr;
    CUresult r = cuModuleGetFunction(&dispatch, cuMod, SYMM_STR("symm_dispatch"));
    if (r != CUDA_SUCCESS) {
        cuModuleUnload(cuMod);
        throw std::runtime_error(SYMM_STR("NVRTC: kernel load failed"));
    }

    // Retrieve the warp-aligned batch dispatch kernel (preferred path).
    // Non-fatal: older compiled modules may not have it; GpuServer falls back
    // to the single-thread path when dispatchBatch is nullptr.
    CUfunction dispatchBatch = nullptr;
    cuModuleGetFunction(&dispatchBatch, cuMod, SYMM_STR("symm_dispatch_batch"));

    // Bind time globals — locate _symm_now_ms and _symm_start_ms in the module.
    // Non-fatal: pre-compiled modules may lack these symbols.
    CompiledModule mod;
    mod.cuModule      = cuMod;
    mod.dispatch      = dispatch;
    mod.dispatchBatch = dispatchBatch;

    {
        size_t sz = 0;
        if (cuModuleGetGlobal(&mod.d_nowMs, &sz, cuMod, "_symm_now_ms") == CUDA_SUCCESS
                && sz == sizeof(uint64_t)) {
            // Seed with current time immediately
            uint64_t t = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            cuMemcpyHtoD(mod.d_nowMs, &t, sizeof(uint64_t));
        }
        if (cuModuleGetGlobal(&mod.d_startMs, &sz, cuMod, "_symm_start_ms") == CUDA_SUCCESS
                && sz == sizeof(uint64_t)) {
            uint64_t t = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            cuMemcpyHtoD(mod.d_startMs, &t, sizeof(uint64_t));
        }
        // Locate debug ring buffer global (will be set by GpuServer::ctor)
        cuModuleGetGlobal(&mod.d_debugRingPtr, &sz, cuMod, "_debug_ring_ptr");
    }

    // mod.ptx intentionally NOT stored — already zeroed above
    return mod;
}

void NvrtcCompiler::checkNvrtc(nvrtcResult r, const char* call) const {
    if (r != NVRTC_SUCCESS)
        throw std::runtime_error(std::string("NVRTC error in ") + call + ": " + nvrtcGetErrorString(r));
}

void NvrtcCompiler::checkCu(CUresult r, const char* call) const {
    if (r != CUDA_SUCCESS) {
        const char* msg = nullptr;
        cuGetErrorString(r, &msg);
        throw std::runtime_error(std::string("CUDA driver error in ") + call + ": " + (msg ? msg : "unknown"));
    }
}

} // namespace symm
