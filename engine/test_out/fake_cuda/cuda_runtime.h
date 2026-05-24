#pragma once
#include "cuda.h"
#define __device__
#define __global__
#define __host__
#define __shared__
#define __constant__
#define cudaError_t int
#define cudaSuccess 0
#define CUDA_ARCH 0
inline cudaError_t cudaGetLastError() { return 0; }
inline cudaError_t cudaDeviceSynchronize() { return 0; }
inline const char* cudaGetErrorString(cudaError_t e) { return ""; }
struct dim3 { unsigned int x,y,z; dim3(unsigned x=1,unsigned y=1,unsigned z=1):x(x),y(y),z(z){} };
