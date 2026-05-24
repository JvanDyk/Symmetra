#pragma once
typedef unsigned long long CUdeviceptr;
typedef void* CUstream;
typedef int CUresult;
typedef int CUdevice;
typedef void* CUcontext;
typedef void* CUmodule;
typedef void* CUfunction;
#define CUDA_SUCCESS 0
inline CUresult cuMemAlloc(CUdeviceptr* p, size_t s) { *p = 0; return 0; }
inline CUresult cuMemFree(CUdeviceptr p) { return 0; }
inline CUresult cuMemsetD8(CUdeviceptr p, unsigned char v, size_t s) { return 0; }
inline CUresult cuMemcpyHtoDAsync(CUdeviceptr d, const void* h, size_t s, CUstream st) { return 0; }
inline CUresult cuMemcpyDtoHAsync(void* h, CUdeviceptr d, size_t s, CUstream st) { return 0; }
inline CUresult cuMemcpyDtoH(void* h, CUdeviceptr d, size_t s) { return 0; }
inline CUresult cuStreamSynchronize(CUstream s) { return 0; }
inline CUresult cuInit(unsigned int f) { return 0; }
inline CUresult cuDeviceGet(CUdevice* d, int i) { return 0; }
inline CUresult cuCtxCreate(CUcontext* c, unsigned int f, CUdevice d) { return 0; }
inline CUresult cuCtxDestroy(CUcontext c) { return 0; }
inline CUresult cuModuleLoadDataEx(CUmodule* m, const void* img, unsigned int n, int* opts, void** vals) { return 0; }
inline CUresult cuModuleUnload(CUmodule m) { return 0; }
inline CUresult cuModuleGetFunction(CUfunction* hf, CUmodule m, const char* name) { return 0; }
inline CUresult cuModuleGetGlobal(CUdeviceptr* dptr, size_t* bytes, CUmodule m, const char* name) { return 0; }
inline CUresult cuLaunchKernel(CUfunction f, unsigned int gx, unsigned int gy, unsigned int gz, unsigned int bx, unsigned int by, unsigned int bz, unsigned int sm, CUstream s, void** args, void** extra) { return 0; }
inline CUresult cuDeviceGetAttribute(int* pi, int attrib, CUdevice dev) { return 0; }
inline CUresult cuStreamCreate(CUstream* s, unsigned int f) { return 0; }
inline CUresult cuStreamDestroy(CUstream s) { return 0; }
inline CUresult cuMemcpyHtoD(CUdeviceptr d, const void* h, size_t s) { return 0; }
inline const char* cuGetErrorString(CUresult r, const char** pStr) { if(pStr) *pStr = ""; return ""; }
