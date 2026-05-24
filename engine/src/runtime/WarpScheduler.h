#pragma once
#include <cuda.h>
#include <cuda_runtime.h>

namespace symm {

// LaunchConfig holds the auto-tuned grid/block dimensions for a kernel.
struct LaunchConfig {
    int gridSize;    // number of blocks
    int blockSize;   // threads per block (multiple of 32 — one warp)
    int sharedBytes; // dynamic shared memory per block
};

// WarpScheduler — automatic kernel launch configuration and warp-level utilities.
//
// GPU throughput is determined by SM occupancy: how many warps are resident
// per SM simultaneously. Under-sized blocks leave SMs idle; over-sized blocks
// exhaust register file and reduce occupancy.
//
// optimal<Kernel>() queries cudaOccupancyMaxPotentialBlockSize() — the CUDA
// driver's own occupancy calculator — to find the block size that maximises
// active warps per SM for the given kernel and GPU.
//
class WarpScheduler {
public:
    // Query the driver for the block size that maximises SM occupancy.
    // Kernel must be a __global__ function pointer.
    template<typename KernelFn>
    static LaunchConfig optimal(KernelFn kernel, int n, size_t sharedMem = 0) {
        int blockSize = 0, minGrid = 0;
        cudaError_t e = cudaOccupancyMaxPotentialBlockSize(
            &minGrid, &blockSize, kernel, sharedMem, 0);
        if (e != cudaSuccess || blockSize <= 0) blockSize = 256;
        // Round n up to next full block
        int gridSize = (n + blockSize - 1) / blockSize;
        if (gridSize < 1) gridSize = 1;
        return { gridSize, blockSize, (int)sharedMem };
    }

    // Occupancy fraction [0, 1] for a given block size.
    template<typename KernelFn>
    static float occupancy(KernelFn kernel, int blockSize) {
        int maxActive = 0;
        cudaOccupancyMaxActiveBlocksPerMultiprocessor(
            &maxActive, kernel, blockSize, 0);
        int maxThr = maxThreadsPerMP();   // cached — one cudaGetDeviceProperties ever
        if (maxThr <= 0) return 0.f;
        return static_cast<float>(maxActive * blockSize)
             / static_cast<float>(maxThr);
    }

    // Round n up to the next warp boundary (multiple of 32).
    static constexpr int warpAlign(int n) { return (n + 31) & ~31; }

    // Number of warps needed to cover n elements.
    static constexpr int warpsFor(int n) { return (n + 31) / 32; }

    // Number of blocks needed for n elements at blockSize threads/block.
    static constexpr int blocksFor(int n, int blockSize) {
        return (n + blockSize - 1) / blockSize;
    }

    // SM count on device 0 (cached after first call).
    static int smCount();

    // Max threads currently schedulable across all SMs.
    static int maxParallelism();

private:
    // maxThreadsPerMultiProcessor cached at first call — avoids repeated
    // cudaGetDeviceProperties() inside the hot occupancy() path.
    static int maxThreadsPerMP();
};

} // namespace symm
