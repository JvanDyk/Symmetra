# Symmetra

**TypeScript for the GPU.** Write TypeScript, run at CUDA speed.

```bash
symmetra ./server.ts
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
winget install symmetra.symmetra

# Or direct download
curl -L https://symmetra.dev/install.ps1 | powershell

# Verify
symmetra --version
symmetra doctor   # checks CUDA device, driver, esbuild
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
symmetra ./server.ts
# Symmetra 1.0.0  ·  RTX 4090 sm_89  ·  NVRTC JIT 50ms
# Listening on :8080  ·  2 routes compiled
```

---

## What runs on GPU

Everything except accepting TCP connections:

| Operation | Runs on |
|-----------|---------|
| HTTP parsing | GPU (HYBRID_SPATIAL kernel) |
| URL routing | GPU (RAYK_ADAPTIVE kernel) |
| Request handlers | GPU (your compiled code) |
| `Float32Array.map(fn)` | GPU (parallel kernel, 1 thread/element) |
| Response JSON serialization | GPU |
| VRAM cache (`@symmetra/kv`) | GPU |
| TM fingerprinting | GPU (APEX ~16 GH/s) |
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

  // GPU fingerprint via APEX engine (16 GH/s)
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
| `@symmetra/core` | GPU context, buffer API, engine selection |

---

## Performance

Measured on RTX 4090, compared against Node.js 22 + Fastify:

| Benchmark | Node + Fastify | Symmetra | Speedup |
|-----------|----------------|----------|---------|
| Simple JSON route | 180k RPS | 1.4M RPS | 7.8× |
| Array transform (1k floats) | 45k RPS | 890k RPS | 19.8× |
| Cache hit (VRAM vs Redis) | 120k RPS | 2.1M RPS | 17.5× |
| TM fingerprint | 8 GH/s | 16 GH/s (APEX) | 2× |

---

## Requirements

- NVIDIA GPU: RTX 2070 or newer (sm_75+)
- CUDA Toolkit 12.x
- Windows 10/11 or Linux (Ubuntu 22.04+)
- esbuild (bundled with Symmetra)

No Node.js required at runtime.

---

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full 4-layer pipeline design, CUDA kernel internals, and the codegen specification.

---

## License

Symmetra Engine: **Proprietary** — binary distribution only. The engine binary is signed and protected. Reverse engineering is prohibited.

`@symmetra/http`, `@symmetra/core`, `@symmetra/kv`: **MIT**
