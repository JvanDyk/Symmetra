#pragma once
#include <string>
#include <vector>
#include <cuda.h>
#include <nvrtc.h>

namespace symm {

struct CompiledModule {
    CUmodule   cuModule   = nullptr;
    CUfunction dispatch   = nullptr;   // symm_dispatch kernel
    std::string ptx;                   // PTX source (kept for debugging)
    std::vector<std::string> mapKernels; // names of map kernels to load
};

class NvrtcCompiler {
public:
    NvrtcCompiler();
    ~NvrtcCompiler();

    // Compile CUDA C++ source to PTX, then load into a CUmodule.
    // Throws std::runtime_error on failure.
    CompiledModule compile(const std::string& cudaSrc, bool verbose = false);

private:
    CUdevice  _device  = 0;
    CUcontext _context = nullptr;

    std::string archFlag() const;
    void checkNvrtc(nvrtcResult r, const char* call) const;
    void checkCu(CUresult r, const char* call) const;
};

} // namespace symm
