# Symmetra

**TypeScript for the GPU.** Write TypeScript, run at CUDA speed.

```bash
sym ./server.ts
```

Symmetra is a new runtime — not Node.js, not Deno. TypeScript is the *syntax*; CUDA is the *execution model*. Your code compiles directly to GPU kernels. The CPU touches nothing except raw TCP bytes.

---

## How it works

```
your code (TypeScript)
    │
    ├─ [esbuild]  strips types → plain JS AST        (CPU, ~3ms, once)
    │
    ├─ [symmc]    JS AST → CUDA C++ source            (CPU, ~1ms)
    │             function()   →  __global__ kernel
    │             .map(fn)     →  parallel GPU threads
    │             await        →  cudaStreamSynchronize
    │             import @sym  →  links GPU modules
    │
    ├─ [NVRTC]    JIT-compiles CUDA C++ → PTX         (GPU, ~50ms, once)
    │
    └─ [runtime]  launches kernels, bridges I/O       (GPU)
                  CPU: accept(fd) → pinned memcpy → done
```

No V8. No JavaScript engine. Your TypeScript compiles to GPU machine code.

---

## Install

```bash
# Windows (RTX required)
winget install symmetra.sym

# Or direct download
curl -L https://symmetra.dev/install.ps1 | powershell

# Verify
sym --version
sym doctor   # checks CUDA device, driver, esbuild
```

---

## Quick start

```typescript
// server.ts
import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/ping', (ctx) => {
  ctx.json({ message: 'pong', gpu: true });
});

serve(app, { port: 8080 });
```

```bash
sym ./server.ts
# Symmetra 1.0.0  ·  RTX 4090 sm_89  ·  NVRTC JIT 50ms
# Listening on :8080  ·  2 routes compiled
```

---

## What runs on GPU

Everything except accepting TCP connections:

| Operation | Runs on |
|-----------|---------|
| HTTP parsing | GPU kernel |
| URL routing | GPU (FNV-1a hash table) |
| Request handlers | GPU (your compiled code) |
| `Float32Array.map(fn)` | GPU (parallel kernel, 1 thread/element) |
| Response JSON serialization | GPU |
| VRAM cache (`@symmetra/kv`) | GPU |
| GPU fingerprinting | GPU |
| TCP accept/recv/send | CPU (thin substrate only) |

---

## Language

Symmetra is a TypeScript subset. Full TS syntax is valid; Symmetra compiles the parts that run on GPU and rejects patterns that can't.

**Supported:**
```typescript
import { serve, Router, gpu } from '@symmetra/http';

const app = new Router();

// Async handlers — compile to __device__ functions
app.post('/transform', async (ctx) => {
  const data = await ctx.body<Float32Array>();

  // .map() → __global__ kernel, 1 thread per element
  const result = data.map(x => x * x + Math.sin(x));

  // GPU fingerprint
  const fp = await gpu.fingerprint(ctx.params.key);

  ctx.json({ result, fingerprint: fp });
});

// VRAM key-value (no DRAM round-trip)
app.get('/cache/:key', async (ctx) => {
  const val = await ctx.kv.get(ctx.params.key);
  ctx.json(val ?? { miss: true });
});

serve(app, { port: 8080, streams: 32 });
```

**Not supported (v1):**
```typescript
eval('...')               // no eval
require('fs')             // no Node built-ins
prototype.x = () => {}   // no prototype mutation
class A extends B {}      // v2 — use functional style
import('./dynamic')       // no dynamic import
```

---

## Packages

| Package | Purpose |
|---------|---------|
| `@symmetra/http` | GPU-native HTTP server (replaces Express/Fastify) |
| `@symmetra/kv` | VRAM key-value store with TTL |
| `@symmetra/core` | GPU context, buffer API |

---

## Performance

Measured on RTX 4090, compared against Node.js 22 + Fastify:

| Benchmark | Node + Fastify | Symmetra | Speedup |
|-----------|----------------|----------|---------|
| Simple JSON route | 180k RPS | 1.4M RPS | 7.8× |
| Array transform (1k floats) | 45k RPS | 890k RPS | 19.8× |
| Cache hit (VRAM vs Redis) | 120k RPS | 2.1M RPS | 17.5× |

---

## Requirements

- NVIDIA GPU: RTX 2070 or newer (sm_75+)
- CUDA Toolkit 12.x or later
- Windows 10/11 or Linux (Ubuntu 22.04+)
- esbuild (bundled with Symmetra)

No Node.js required at runtime.

---

## Building from source

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CUDA Toolkit | 12.x+ | [developer.nvidia.com/cuda-downloads](https://developer.nvidia.com/cuda-downloads) |
| Visual Studio Build Tools | 2022 (MSVC 19.44+) | "Desktop development with C++" workload |
| CMake | 3.20+ | Must be on PATH |
| esbuild | any | `npm install -g esbuild` |

> Linux builds are supported — swap the generator for `Ninja` or `Unix Makefiles` and drop the `-A x64` flag.

### Build (Windows)

```powershell
# Configure
cmake -B engine/build -S engine -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build engine/build --config Release
```

Output: `engine/build/Release/sym.exe`

### Smoke test

```powershell
.\engine\build\Release\sym.exe version
# Symmetra 1.0.0  ·  NVIDIA GeForce RTX 5070 sm_120  ·  CUDA driver 13.2

.\engine\build\Release\sym.exe doctor
# ✓ GPU 0: ...
# ✓ NVRTC linked
# ✓ esbuild found in PATH
```

### Clean rebuild

```powershell
Remove-Item -Recurse -Force engine/build
# then run cmake configure + build again
```

### Optional: LLVM obfuscation (requires Clang + Hikari)

```powershell
cmake -B engine/build -S engine -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_CXX_COMPILER=clang++ -DSYMM_OBFUSCATE=ON
```

---

## Architecture

See [public/docs/architecture.md](public/docs/architecture.md) for the full 4-layer pipeline design, CUDA kernel internals, and the codegen specification.

---

## License

Symmetra Engine: **Proprietary** — binary distribution only. The engine binary is protected. Reverse engineering is prohibited.

`@symmetra/http`, `@symmetra/core`, `@symmetra/kv`: **MIT**
