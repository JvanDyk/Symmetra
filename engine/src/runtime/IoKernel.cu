// Symmetra I/O Kernels
// These kernels run on GPU to process raw TCP data after it arrives via pinned memory.
// They operate before the user's handler kernels.
#include <cuda_runtime.h>
#include <stdint.h>

// ── HTTP Status helpers ────────────────────────────────────────────────────────

__global__ void symm_io_clear_responses(char* resp_bufs, int n, int resp_size) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;
    char* buf = resp_bufs + (size_t)tid * resp_size;
    for (int i = 0; i < resp_size; i++) buf[i] = 0;
}

// Batch-copy route IDs computed by GpuRouter into a compact array for dispatch
__global__ void symm_io_pack_route_ids(const int* route_ids_in,
                                        int*       route_ids_out,
                                        int        n) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;
    route_ids_out[tid] = route_ids_in[tid];
}
