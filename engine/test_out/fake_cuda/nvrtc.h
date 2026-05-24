#pragma once
typedef int nvrtcResult;
typedef void* nvrtcProgram;
#define NVRTC_SUCCESS 0
inline nvrtcResult nvrtcCreateProgram(nvrtcProgram* p, const char* s, const char* n, int nh, const char** h, const char** hn) { return 0; }
inline nvrtcResult nvrtcCompileProgram(nvrtcProgram p, int n, const char** opts) { return 0; }
inline nvrtcResult nvrtcGetPTXSize(nvrtcProgram p, size_t* s) { *s = 0; return 0; }
inline nvrtcResult nvrtcGetPTX(nvrtcProgram p, char* buf) { return 0; }
inline nvrtcResult nvrtcGetProgramLogSize(nvrtcProgram p, size_t* s) { *s = 0; return 0; }
inline nvrtcResult nvrtcGetProgramLog(nvrtcProgram p, char* buf) { return 0; }
inline nvrtcResult nvrtcDestroyProgram(nvrtcProgram* p) { return 0; }
inline const char* nvrtcGetErrorString(nvrtcResult r) { return ""; }
