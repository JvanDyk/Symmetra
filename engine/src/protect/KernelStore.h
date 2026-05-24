#pragma once
#include <cuda.h>

namespace symm::protect {

struct KernelHandles {
    CUmodule  genesis_mod = nullptr;
    CUfunction symm_core  = nullptr;  // Rule 110 CA kernel

    CUmodule  http_mod    = nullptr;  // GPU HTTP routing
    CUmodule  io_mod      = nullptr;  // GPU I/O helpers
};

// Decrypt all embedded kernel blobs and load them into CUDA modules.
// Must be called after a driver context is current (after cudaSetDevice).
// Decrypted PTX is zeroed immediately after cuModuleLoadData — never persists in memory.
KernelHandles loadKernels();

// Access the globally loaded handles after loadKernels() returns.
const KernelHandles& kernels();

} // namespace symm::protect
