# Symmetra Architecture

## Overview

Symmetra is a TypeScript-to-CUDA compiler and GPU runtime. It is not a Node.js variant, not a WASM runtime, and does not embed a JavaScript engine. TypeScript is the input language; CUDA PTX is the execution target.

```
┌──────────────────────────────────────────────────────────────────────┐
│  User TypeScript (server.ts)                                          │
│  import { serve, Router } from '@symmetra/http'                       │
│  app.get('/path', async (ctx) => { ... })                             │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  sym ./server.ts
┌──────────────────────────────▼───────────────────────────────────────┐
│  1. esbuild (subprocess, ~3ms)                                        │
│     TypeScript → plain ES2022 JavaScript (types stripped)            │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  JS source string
┌──────────────────────────────▼───────────────────────────────────────┐
│  2. SymmParser (C++ recursive descent)                                │
│     JS → Symmetra AST                                                │
│     Extracts: imports, route registrations, serve() config           │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Module AST
┌──────────────────────────────▼───────────────────────────────────────┐
│  3. Codegen (C++ AST walker, ~1ms)                                    │
│     Symmetra AST → CUDA C++ source                                   │
│                                                                      │
│     async (ctx) => {...}     →  __device__ void handler_N(SymmCtx*)  │
│     data.map(x => expr)      →  __global__ void map_kernel_N(...)    │
│     Math.sin / Math.cos      →  sinf / cosf (CUDA intrinsics)        │
│     ctx.json(val)            →  symm_json_str(ctx, val)              │
│     gpu.fingerprint(key)     →  symm_fingerprint(key, len)           │
│     await                    →  stripped (GPU is synchronous)        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  CUDA C++ source
┌──────────────────────────────▼───────────────────────────────────────┐
│  4. NVRTC (JIT compiler, ~50ms, once at startup)                      │
│     CUDA C++ → PTX bytecode for your specific GPU sm_XX              │
│     Loaded into CUmodule, dispatch kernel function retrieved         │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  CUfunction: symm_dispatch
┌──────────────────────────────▼───────────────────────────────────────┐
│  5. GpuServer (C++ runtime, runs forever)                             │
│                                                                      │
│  CPU side (thin substrate — unavoidable):                            │
│    accept(fd) → recv(buf) → cudaMemcpyAsync(H→D, pinned)            │
│    cudaMemcpyAsync(D→H, pinned) → send(buf)                         │
│                                                                      │
│  GPU side (everything else):                                         │
│    GpuHttpParser  kernel  → parse HTTP headers → SymmCtx struct      │
│    GpuRouter      kernel  → FNV-1a hash route lookup → handler_id   │
│    symm_dispatch  kernel  → switch(handler_id) → calls handler_N    │
│    handler_N      kernel  → user code runs → writes resp_buf        │
│    map_kernel_N   kernel  → parallel element transform              │
└──────────────────────────────────────────────────────────────────────┘
```

---

## CUDA memory model

| Buffer | Location | Size | Purpose |
|--------|----------|------|---------|
| `d_ctx[MAX_BATCH]` | VRAM | 256 B × 64 | SymmCtx per request |
| `d_bodies[MAX_BATCH]` | VRAM | 64 KB × 64 | Request body storage |
| `d_resps[MAX_BATCH]` | VRAM | 64 KB × 64 | Response output |
| `h_ctx`, `h_resps` | Pinned RAM | same | Zero-copy H↔D transfer |
| Route table | `__constant__` | 64 × 8 B | FNV-1a hashes per route |
| KvStore | VRAM | configurable | VRAM key-value entries |

Pinned (page-locked) memory is used for all host↔device transfers. This enables DMA — the GPU's memory controller copies data without CPU involvement, at full PCIe bandwidth (32 GB/s on PCIe 5.0).

---

## GPU fingerprinting

`gpu.fingerprint()` runs a cellular-automaton hash entirely on-chip. The algorithm operates on a 128-bit state, applies 16 evolution steps per call, and produces a 64-bit digest. No memory round-trips — input and output stay in registers.

---

## Binary protection

The Symmetra engine binary (`sym.exe` / `sym`) is distributed in compiled form only:

- C++ source is not distributed
- CUDA PTX is embedded as an opaque byte array in the binary
- Debug symbols are stripped from release builds (`--strip-all`)
- The binary is signed (Authenticode on Windows, GPG on Linux)
- Reverse engineering is prohibited by the Symmetra License

The TypeScript SDK (`@symmetra/http`, `@symmetra/core`, `@symmetra/kv`) is MIT-licensed and open source — it contains only TypeScript types and has no implementation. All implementation is in the engine binary.

---

## Codegen specification

### Route handler → `__device__` function

```typescript
app.get('/path', async (ctx) => {
  ctx.json({ ok: true });
});
```

Compiles to:

```cuda
__device__ void symm_handler_0(SymmCtx* ctx) {
    symm_json_str(ctx, "{\\\"ok\\\":true}");
}
```

### `.map()` → `__global__` kernel

```typescript
const result = data.map(x => x * x + Math.sin(x));
```

Compiles to:

```cuda
__global__ void symm_map_k0(const float* in, float* out, int n) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;
    float x = in[tid];
    out[tid] = (x * x + sinf(x));
}
```

Launch: `<<<(n+255)/256, 256>>>`

### Math remapping

| TypeScript | CUDA |
|-----------|------|
| `Math.sin(x)` | `sinf(x)` |
| `Math.cos(x)` | `cosf(x)` |
| `Math.sqrt(x)` | `sqrtf(x)` |
| `Math.abs(x)` | `fabsf(x)` |
| `Math.pow(x,y)` | `powf(x,y)` |
| `Math.log(x)` | `logf(x)` |
| `Math.exp(x)` | `expf(x)` |
| `Math.PI` | `3.14159265358979323846f` |

### `await` stripping

`await` is stripped by the codegen — GPU code is synchronous within a warp. Stream synchronization between pipeline stages is managed by the GpuServer host code, not the user's handler.
