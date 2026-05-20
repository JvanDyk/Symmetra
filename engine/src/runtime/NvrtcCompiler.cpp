#include "NvrtcCompiler.h"
#include <stdexcept>
#include <cstdio>
#include <cuda_runtime.h>

#define NVRTC_CHECK(call) checkNvrtc((call), #call)
#define CU_CHECK(call)    checkCu((call), #call)

namespace symm {

NvrtcCompiler::NvrtcCompiler() {
    CU_CHECK(cuInit(0));
    CU_CHECK(cuDeviceGet(&_device, 0));
    CU_CHECK(cuCtxCreate(&_context, 0, _device));
}

NvrtcCompiler::~NvrtcCompiler() {
    if (_context) cuCtxDestroy(_context);
}

std::string NvrtcCompiler::archFlag() const {
    int major = 0, minor = 0;
    cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, _device);
    cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, _device);
    return "--gpu-architecture=sm_" + std::to_string(major) + std::to_string(minor);
}

CompiledModule NvrtcCompiler::compile(const std::string& cudaSrc, bool verbose) {
    nvrtcProgram prog;
    NVRTC_CHECK(nvrtcCreateProgram(&prog,
                                   cudaSrc.c_str(),
                                   "symmetra_user.cu",
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
        throw std::runtime_error(
            std::string("Symmetra NVRTC compilation failed:\n") + log);
    }
    if (verbose && logSize > 1) {
        std::fprintf(stderr, "── NVRTC log ──\n%s\n────\n", log.c_str());
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

    // Retrieve the dispatch function handle
    CUfunction dispatch = nullptr;
    CUresult r = cuModuleGetFunction(&dispatch, cuMod, "symm_dispatch");
    if (r != CUDA_SUCCESS) {
        cuModuleUnload(cuMod);
        throw std::runtime_error("NVRTC: could not find 'symm_dispatch' kernel in compiled module");
    }

    CompiledModule mod;
    mod.cuModule = cuMod;
    mod.dispatch = dispatch;
    mod.ptx      = std::move(ptx);
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
