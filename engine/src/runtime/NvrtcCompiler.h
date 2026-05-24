#pragma once
#include <string>
#include <vector>
#include <cuda.h>
#include <nvrtc.h>

namespace symm {

struct CompiledModule {
    CUmodule   cuModule       = nullptr;
    CUfunction dispatch       = nullptr;   // symm_dispatch  (single-thread, legacy)
    CUfunction dispatchBatch  = nullptr;   // symm_dispatch_batch (warp-aligned, preferred)
    std::string ptx;                       // PTX source (kept for debugging)
    std::vector<std::string> mapKernels;   // names of map kernels to load

    // Device pointers to time globals inside the compiled module.
    // Populated by NvrtcCompiler::compile(); updated before each kernel launch.
    CUdeviceptr d_nowMs   = 0;   // _symm_now_ms  — refreshed each dispatch
    CUdeviceptr d_startMs = 0;   // _symm_start_ms — set once at load time
};

class NvrtcCompiler {
public:
    NvrtcCompiler();
    ~NvrtcCompiler();

    // Compile CUDA C++ source to PTX, then load into a CUmodule.
    // srcName is shown in error messages so developers see their own filename.
    // Throws std::runtime_error on failure.
    CompiledModule compile(const std::string& cudaSrc,
                           const std::string& srcName = "program",
                           bool verbose = false);

private:
    CUdevice  _device  = 0;
    CUcontext _context = nullptr;

    std::string archFlag() const;
    void checkNvrtc(nvrtcResult r, const char* call) const;
    void checkCu(CUresult r, const char* call) const;
};

} // namespace symm
