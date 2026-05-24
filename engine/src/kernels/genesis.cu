#include <cuda_runtime.h>
#include <stdint.h>

extern "C" __global__ void symm_core(const uint32_t* __restrict__ curr,
                           uint32_t* __restrict__ next,
                           int word_count)
{
    for (int w = 0; w < word_count; w++) {
        uint32_t C  = curr[w];
        uint32_t Lw = curr[(w == 0) ? word_count - 1 : w - 1];
        uint32_t Rw = curr[(w == word_count - 1) ? 0 : w + 1];
        uint32_t l  = (C << 1) | (Lw >> 31);
        uint32_t r  = (C >> 1) | ((Rw & 1u) << 31);
        next[w] = (C | r) & ~(l & C & r);
    }
}
