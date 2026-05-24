# Getting Started with Symmetra

## Requirements

- NVIDIA GPU: RTX 2070 or newer (compute capability sm_75+)
- CUDA Toolkit 12.x or later — [download](https://developer.nvidia.com/cuda-downloads)
- Windows 10/11 (64-bit) or Ubuntu 22.04+
- esbuild (bundled with Symmetra — no separate install needed)

## Install

**Windows:**
```powershell
# Option A: winget
winget install symmetra.sym

# Option B: direct download
curl -L https://symmetra.dev/install.ps1 | powershell

# Verify
sym --version
sym doctor
```

**Linux:**
```bash
curl -fsSL https://symmetra.dev/install.sh | bash
sym --version
sym doctor
```

`sym doctor` checks your GPU, CUDA driver, and esbuild. If anything is missing, it tells you exactly what to install.

---

## Your first Symmetra server

Create `server.ts`:

```typescript
import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/ping', (ctx) => {
  ctx.json({ message: 'pong', gpu: true });
});

serve(app, { port: 8080 });
```

Run it:

```bash
sym ./server.ts
```

Output:
```
Symmetra 1.0.0  ·  RTX 4090 sm_89  ·  NVRTC JIT 47ms
Listening on :8080  ·  1 route compiled
Press Ctrl-C to stop.
```

Test it:
```bash
curl http://localhost:8080/ping
# {"message":"pong","gpu":true}
```

---

## What just happened

1. `sym` called **esbuild** to strip the TypeScript types from your file (~3ms)
2. The **Symmetra compiler** walked the resulting JS AST and generated CUDA C++ source
3. **NVRTC** JIT-compiled the CUDA C++ to PTX for your specific GPU (~47ms, once)
4. The GPU kernel was launched — your handler now runs entirely in VRAM
5. TCP `accept()` + `recv()` runs on CPU (unavoidable), data is immediately pinned to VRAM

Nothing runs on CPU after startup except TCP I/O.

---

## GPU compute in handlers

`.map()`, `.filter()`, `.reduce()` on `GpuBuffer` compile to `__global__` CUDA kernels.
Each call creates one thread per element:

```typescript
app.post('/transform', async (ctx) => {
  // body<Float32Array>() returns a GpuBuffer — already in VRAM, zero-copy
  const data = await ctx.body<Float32Array>();

  // This compiles to a __global__ kernel with (data.length / 256) blocks
  const result = data.map(x => x * x + Math.sin(x));

  ctx.json(result);
});
```

The generated CUDA kernel:
```cuda
__global__ void symm_map_k0(const float* in, float* out, int n) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;
    float x = in[tid];
    out[tid] = x * x + sinf(x);   // Math.sin → sinf (CUDA intrinsic)
}
```

---

## VRAM cache

```typescript
import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';

const kv  = new KvStore({ maxMb: 512 });
const app = new Router();

app.get('/item/:id', async (ctx) => {
  // kv.get reads from VRAM — no Redis, no DRAM round-trip
  const item = await kv.get<Item>(ctx.params.id);
  if (!item) { ctx.status(404).json({ error: 'not found' }); return; }
  ctx.json(item);
});

app.put('/item/:id', async (ctx) => {
  const body = await ctx.json<Item>();
  await kv.set(ctx.params.id, body, { ttlMs: 60_000 });
  ctx.json({ ok: true });
});

serve(app, { port: 8080 });
```

---

## GPU fingerprinting

`gpu.fingerprint()` runs a cellular-automaton hash entirely on the GPU — no DRAM round-trip:

```typescript
app.get('/fp/:key', async (ctx) => {
  const fp = await ctx.gpu.fingerprint(ctx.params.key);
  ctx.json({ fp: fp.toString(16) });
});
```

---

## What Symmetra TypeScript does NOT support

These patterns cause a compile-time error from the Symmetra engine:

| Pattern | Reason |
|---------|--------|
| `eval('...')` | Cannot compile dynamic strings to CUDA |
| `require('fs')` | No Node.js built-ins — GPU has no filesystem |
| `class A extends B {}` | v2 feature — use functional style |
| `import('./dynamic')` | Dynamic imports unresolvable at GPU compile time |
| `prototype.x = fn` | Prototype mutation incompatible with CUDA type system |

---

## CLI reference

```bash
sym ./server.ts          # Compile + run
sym build ./server.ts    # Compile to sym.ptx (ahead-of-time)
sym doctor               # Check GPU, CUDA, esbuild
sym bench                # GPU benchmark
sym version              # Version info
```

Options:
```bash
--port <n>       Override the port from serve()
--streams <n>    CUDA stream pool size (default: 32)
--verbose        Show generated CUDA C++ and NVRTC output
```
