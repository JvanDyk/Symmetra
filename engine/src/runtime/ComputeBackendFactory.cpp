#include "IComputeBackend.h"
#include "backends/CudaBackend.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <vector>

namespace symm {

// ============================================================================
// Factory Functions
// ============================================================================

IComputeBackend* createComputeBackend(const char* backendName) {
    if (!backendName) {
        return createDefaultBackend();
    }

    std::string name(backendName);
    // Normalize to lowercase
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (name == "cuda" || name == "nvidia") {
        return new CudaBackend();
    }
    // Additional backends (ROCM, HIP, OpenCL) can be added here in future
    // else if (name == "rocm" || name == "amd") {
    //     return new RocmBackend();
    // }
    // else if (name == "hip") {
    //     return new HipBackend();
    // }

    throw std::runtime_error(std::string("Unsupported compute backend: ") + backendName);
}

IComputeBackend* createDefaultBackend() {
    // For now, default to CUDA (the only implemented backend)
    return new CudaBackend();
}

std::vector<const char*> getAvailableBackends() {
    return {"cuda"};
    // In the future: {"cuda", "rocm", "hip", "opencl"}
}

} // namespace symm
