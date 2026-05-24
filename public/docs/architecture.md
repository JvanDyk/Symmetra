# Symmetra Architecture

## Overview

Symmetra is a TypeScript-to-CUDA compiler and GPU runtime. It is not a Node.js variant, not a WASM runtime, and does not embed a JavaScript engine. TypeScript is the input language; CUDA PTX is the execution target.

## The compilation pipeline

```
┌──────────────────────────────────────────────────────────────────────┐
│  Your TypeScript (server.ts)                                          │
│  import { serve, Router } from '@symmetra/http'                       │
│  app.get('/path', async (ctx) => { ... })                             │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  sym ./server.ts
┌──────────────────────────────▼───────────────────────────────────────┐
│  Stage 1 — esbuild (~3ms)                                             │
│  Strips TypeScript types → plain ES2022 JavaScript                   │
└──────────────────────────────┬───────────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────────┐
│  Stage 2 — Symmetra compiler (~1ms)                                   │
│  Walks the JS AST and extracts: imports, route registrations,        │
│  handler bodies, serve() configuration                               │
└──────────────────────────────┬───────────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────────┐
│  Stage 3 — CUDA C++ codegen (~1ms)                                    │
│  Emits GPU source for every handler and parallel operation           │
│                                                                      │
│     async (ctx) => { ... }       →  GPU device function             │
│     data.map(x => expr)          →  parallel GPU kernel             │
│     Math.sin / Math.cos          →  sinf / cosf (CUDA intrinsics)   │
│     ctx.json(val)                →  GPU JSON serializer             │
│     gpu.fingerprint(key)         →  GPU hash function               │
│     await                        →  stripped (GPU is synchronous)   │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  CUDA C++ source
┌──────────────────────────────▼───────────────────────────────────────┐
│  Stage 4 — NVRTC JIT compiler (~50ms, runs once at startup)           │
│  Compiles CUDA C++ to PTX bytecode for your specific GPU             │
└──────────────────────────────┬───────────────────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────────────────┐
│  Stage 5 — GPU runtime (runs forever)                                 │
│                                                                      │
│  CPU side (unavoidable — TCP is a CPU operation):                    │
│    accept(fd) → recv(buf) → copy to VRAM → GPU dispatch             │
│    copy from VRAM → send(buf)                                       │
│                                                                      │
│  GPU side (everything else):                                         │
│    HTTP parsing → URL routing → your handler → JSON response        │
└──────────────────────────────────────────────────────────────────────┘
```

Total startup time: under 60ms. After that, your TypeScript runs as CUDA machine code.

---

## Code transformations

### Route handler → GPU device function

```typescript
app.get('/path', async (ctx) => {
  ctx.json({ ok: true });
});
```

Compiles to a GPU device function invoked on the correct CUDA thread for each matched request. The `async` and `await` keywords are stripped — GPU code is synchronous.

### `.map()` → parallel GPU kernel

```typescript
const result = data.map(x => x * x + Math.sin(x));
```

Compiles to a GPU kernel that launches one thread per array element:

```cuda
// one thread per element — thousands run simultaneously
__global__ void transform(const float* in, float* out, int n) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;
    out[tid] = in[tid] * in[tid] + sinf(in[tid]);
}
```

Launch configuration: `<<<(n + 255) / 256, 256>>>` — automatically sized by the runtime.

### Math remapping

| TypeScript | GPU intrinsic |
|-----------|---------------|
| `Math.sin(x)` | `sinf(x)` |
| `Math.cos(x)` | `cosf(x)` |
| `Math.sqrt(x)` | `sqrtf(x)` |
| `Math.abs(x)` | `fabsf(x)` |
| `Math.pow(x, y)` | `powf(x, y)` |
| `Math.log(x)` | `logf(x)` |
| `Math.exp(x)` | `expf(x)` |
| `Math.PI` | `3.14159265358979323846f` |

### `await` and `async`

`async`/`await` is preserved as a programming model. Within a single CUDA thread, execution is sequential — there is no event loop and no microtask queue. When `await` appears inside a handler, the compiler emits sequential GPU code that reads naturally. When `await` is used with `Promise.all()`, the compiler emits parallel CUDA stream dispatch — multiple operations run simultaneously on separate streams, then synchronize.

---

## Full language compatibility

Symmetra supports the complete TypeScript / JavaScript language. The sections below describe how features that require special handling are compiled.

### Class inheritance

Classes compile to C++ struct hierarchies in CUDA. Method resolution uses CRTP (Curiously Recurring Template Pattern), which resolves dispatch at compile time — faster than C++ virtual dispatch. `instanceof` is emitted as a compile-time type tag check.

### `Map<K, V>` and `Set<T>`

Compile to `GpuMap<K, V>` and `GpuSet<T>` — VRAM-resident open-addressing hash tables implemented as CUDA device memory structures. Operations are inlined into the calling kernel, not dispatched to a separate kernel, so lookup cost is O(1) within a thread.

### `RegExp`

Regular expression literals are analyzed by the codegen phase and converted to DFAs (Deterministic Finite Automata) emitted inline in the CUDA kernel. There is no runtime regex engine — the DFA is generated at kernel compile time and costs only a state table lookup per character during execution.

### `eval()`

When `eval()` is called at runtime, the string argument is forwarded to a background runtime JIT thread that runs the standard compilation pipeline (esbuild → codegen → NVRTC) on it. The resulting module is loaded via `cuModuleLoadData` and cached by content hash. Subsequent calls with the same string have zero compilation overhead. The spawned kernel runs in a dedicated CUDA stream and its result is marshalled back to the calling thread.

### Dynamic imports

`import('./path')` is handled by expanding the module graph at startup. The compiler performs a static reachability analysis and compiles every module it can reach. At runtime, `import()` resolves synchronously from a compiled module registry. Modules that are truly unknown at startup (e.g., user-supplied path at runtime) fall back to the same runtime JIT path as `eval()`.

### Node.js built-in remapping

The import resolver intercepts standard Node.js module specifiers and redirects them to Symmetra equivalents before codegen:

| Import | Maps to |
|--------|---------|
| `fs`, `fs/promises` | `@symmetra/fs` |
| `crypto` | `@symmetra/crypto` |
| `events` | `@symmetra/core` EventEmitter |
| `path` | Built-in GPU path utilities |
| `os` | GPU system info API |
| `http`, `https`, `node-fetch` | `@symmetra/net` |
| `buffer` | `GpuBuffer<Uint8Array>` |
| `url`, `querystring` | Built-in URL / QS parsing |
| `stream` | GPU async iterator |

### Generators

`function*` and `yield` compile to GPU state machines. The compiler assigns each yield point a state index. The generator object is a CUDA struct holding the current state and all live variables. Calling `.next()` dispatches to the correct state via a switch statement.

### Prototype mutation

The AST walker makes two passes: the first pass collects all prototype assignments (`Foo.prototype.x = fn`), the second pass emits CUDA with the complete, final class shape. No prototype mutation occurs at runtime — the type is fully resolved at compile time.

### `try / catch / finally`

The compiler wraps try blocks in a result type: functions that can throw return a tagged union of `{ok, value}` or `{err, message}`. Catch blocks are emitted as conditional branches on the error tag. Finally blocks are duplicated after both the success and error paths. There is no C++ exception machinery in device code — error propagation is zero-overhead branching.

### `console.log`

`console.*` calls write to a GPU-side ring buffer in VRAM. The buffer is flushed to stdout by the runtime after the response is dispatched. Structured objects are serialized on GPU using the same JSON serializer as `ctx.json()`. There is no host round-trip in the hot path.

### `Promise.all / race / any / allSettled`

Each promise in the argument array is dispatched to a separate CUDA stream. `Promise.all` inserts a `cuStreamSynchronize` barrier on all streams. `Promise.race` uses CUDA events to detect the first completion and cancels the remaining streams. `Promise.any` is implemented equivalently, accepting the first non-rejected result.

---

## Concurrency and scheduling

### CPU thread pool

`accept()` and `recv()` are TCP operations — they run on CPU. Symmetra runs a thread pool with one worker per CPU core. Each worker independently accepts connections and feeds them into the GPU dispatch layer. With 16 CPU cores and 32 GPU streams, 32 requests can be in-flight simultaneously at all times.

```
CPU core 0 ──► accept → recv → copy to VRAM → GPU stream 0  ──► handler kernel
CPU core 1 ──► accept → recv → copy to VRAM → GPU stream 1  ──► handler kernel
CPU core 2 ──► accept → recv → copy to VRAM → GPU stream 2  ──► handler kernel
...                                                          ──► (all run in parallel on GPU)
```

### CUDA stream pool

Each request is assigned a CUDA stream from a pre-created pool (default: 32 streams). Requests on different streams execute concurrently — the GPU scheduler interleaves their kernels across SMs while preserving ordering within each stream. The pool blocks acquisition when all streams are busy, applying natural backpressure.

### Warp-level route matching

Route matching uses `__ballot_sync` — a warp-level primitive that runs a boolean test across all 32 lanes of a warp simultaneously and returns a 32-bit bitmask in one clock cycle. Up to 32 routes are compared in parallel per warp; for more routes the loop iterates in rounds of 32.

```cuda
// 32 routes compared simultaneously — one clock cycle
uint32_t ballot = __ballot_sync(0xFFFFFFFF, routeHash[lane] == reqHash);
if (ballot) return __ffs(ballot) - 1;   // first matching route
```

### Parallel header parsing

HTTP headers are parsed by a kernel that assigns one GPU thread per header line. Threads run concurrently across the header block — a 20-header request is parsed in the time it takes to scan one header serially.

### Auto-tuned kernel launch

Every `Array.map()`, `filter()`, or `reduce()` kernel is launched with dimensions computed by `cudaOccupancyMaxPotentialBlockSize` — the CUDA driver's own occupancy calculator. This finds the block size that maximises the number of active warps per SM for the specific kernel and the actual GPU. The result is cached — the calculation only runs once per unique kernel.

### How many threads run simultaneously

The exact number depends on your GPU:

| GPU | SMs | Max threads/SM | Max parallel threads |
|-----|-----|---------------|----------------------|
| RTX 2070 | 36 | 2048 | 73,728 |
| RTX 3080 | 68 | 1536 | 104,448 |
| RTX 4090 | 128 | 1536 | 196,608 |

A single `data.map(x => x * x)` on a 200,000-element array launches 200,000 threads. On an RTX 4090, all 196,608 that fit run simultaneously; the remaining ~3,000 run in a second wave. Total time: under 100 microseconds.

---

## Memory model and VRAM garbage collection

### Request-scoped arena (young generation GC)

Each GPU stream slot owns a fixed 4 MB VRAM slab with a bump-pointer allocator. All allocations within a request — body buffer, parsed headers, output buffer, intermediate results — come from this slab. When the response is sent, the bump pointer resets in O(1): a single integer assignment that logically frees every object the request ever allocated. No object-by-object tracking. No stop-the-world pause. No fragmentation within a request.

```
Request arrives         Request completes
     │                       │
     ▼                       ▼
[slab: ██████████░░░░]  →  [slab: ░░░░░░░░░░░░░░]
       used  free              freed in one instruction
```

### Persistent heap (old generation)

Objects that outlive a request — KV store entries, compiled eval kernels, cached query results — live in a separate 512 MB VRAM heap with a sorted free-list allocator. Adjacent free blocks are coalesced on every `free()` call. This region is expected to be cold in the hot path; most per-request work hits the slab.

### Pinned host memory

Request bodies arrive from TCP into CPU pinned (page-locked) memory. From there, the CUDA DMA engine transfers directly to VRAM without any CPU involvement — the `memcpy` runs asynchronously while the CPU thread is already accepting the next connection.

---

## Binary protection

The `sym` binary is distributed in compiled form only:

- CUDA kernels are embedded as encrypted byte arrays — no readable bytecode in the binary
- Debug symbols are stripped from release builds
- Reverse engineering is prohibited by the Symmetra License

The TypeScript SDK (`@symmetra/http`, `@symmetra/core`, `@symmetra/kv`) is MIT-licensed and open source.
