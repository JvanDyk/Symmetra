# Getting Started with Symmetra

## Prerequisites

### 1. NVIDIA GPU

You need an NVIDIA GPU with compute capability **sm_75 or higher** (RTX 2070 or newer).

To check your GPU:
```bash
nvidia-smi
```
Look for your GPU's compute capability. RTX 2070 = sm_75, RTX 3080 = sm_86, RTX 4090 = sm_89.

---

### 2. NVIDIA CUDA driver

Symmetra bundles its own compiler (NVRTC) — you **do not** need the full CUDA Toolkit installed.
You only need the NVIDIA driver, version **525 or later** (released 2022+).

**Check your driver version:**
```bash
nvidia-smi
```
Top-right shows `Driver Version: 5xx.xxx`. If it's below 525, update from:
https://www.nvidia.com/drivers

**Windows**: Use NVIDIA GeForce Experience or download directly from the link above.

**Linux (Ubuntu)**:
```bash
sudo apt-get update
sudo ubuntu-drivers autoinstall
sudo reboot
```

---

### 3. Visual C++ Redistributable (Windows only)

Download and install the latest **Microsoft Visual C++ Redistributable (x64)**:
https://aka.ms/vs/17/release/vc_redist.x64.exe

Most modern Windows systems already have this installed. If `sym doctor` reports a missing DLL, install it from that link.

---

### 4. glibc 2.31+ (Linux only)

Symmetra requires **Ubuntu 20.04** (glibc 2.31) or newer. Check:
```bash
ldd --version
```

---

## Install

**Windows — script:**
```powershell
iwr https://symmetra.dev/install.ps1 | iex
```

**Windows — winget:**
```powershell
winget install symmetra.sym
```

**Linux:**
```bash
curl -fsSL https://symmetra.dev/install.sh | bash
```

After installing, open a **new terminal** (the PATH update requires a fresh shell):
```bash
sym doctor
```

`sym doctor` checks your GPU, CUDA driver, and reports anything missing with clear instructions.

---

## Verify installation

```bash
sym version
# Symmetra 1.0.0 · Windows · RTX 4090 sm_89

sym doctor
# [ok] NVIDIA driver 560.94
# [ok] CUDA compute sm_89 (RTX 4090)  — minimum required: sm_75
# [ok] NVRTC available
# [ok] sym is on PATH
```

If `sym doctor` reports an error, follow the instructions it prints — it identifies exactly what is missing.

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

The generated CUDA kernel runs one thread per element across the GPU:
```cuda
__global__ void transform(const float* in, float* out, int n) {
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

## Using the standard libraries

All packages ship as TypeScript type definitions. The implementations run inside `sym` — no `npm install` required at runtime.

### Database — `@symmetra/db`

```typescript
import { postgres, sqlite, redis } from '@symmetra/db';

const pg = postgres({ host: 'localhost', database: 'myapp', user: 'u', password: 'p' });

app.get('/users', async (ctx) => {
  const result = await pg.query<{ id: number; name: string }>('SELECT id, name FROM users');
  ctx.json(result.rows);
});
```

Full example: [`examples/database/`](../examples/database/)

---

### Filesystem — `@symmetra/fs`

```typescript
import { fs, jsonFs } from '@symmetra/fs';

app.post('/upload/:name', async (ctx) => {
  const data = await ctx.body<Uint8Array>();
  await fs.write(`./uploads/${ctx.params.name}`, data);
  ctx.json({ ok: true });
});

app.get('/config', async (ctx) => {
  const cfg = await jsonFs.read('./config.json');
  ctx.json(cfg);
});
```

Full example: [`examples/files/`](../examples/files/)

---

### Cryptography & Auth — `@symmetra/crypto`

```typescript
import { jwt, password, hash, random } from '@symmetra/crypto';

app.post('/login', async (ctx) => {
  const { email, pass } = await ctx.json<{ email: string; pass: string }>();
  // ... load user from db
  if (!await password.verify(pass, user.passwordHash)) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }
  const token = await jwt.sign({ sub: String(user.id) }, process.env.JWT_SECRET!, { expiresIn: 86400 });
  ctx.json({ token });
});
```

Full example: [`examples/crypto-auth/`](../examples/crypto-auth/)

---

### Environment config — `@symmetra/env`

```typescript
import { env } from '@symmetra/env';

const cfg = env.load({
  DATABASE_URL: { type: 'string',  required: true },
  PORT:         { type: 'port',    default: 8080 },
  DEBUG:        { type: 'boolean', default: false },
} as const);

// cfg.PORT is typed as number
// cfg.DATABASE_URL is typed as string
```

Full example: [`examples/env-config/`](../examples/env-config/)

---

### HTTP client & networking — `@symmetra/net`

```typescript
import { http, dns } from '@symmetra/net';

app.get('/proxy', async (ctx) => {
  const response = await http.get('https://api.example.com/data', {
    headers: { Authorization: `Bearer ${cfg.API_KEY}` },
    timeout: 5000,
  });
  ctx.json(response.json());
});
```

Full example: [`examples/http-client/`](../examples/http-client/)

---

### VRAM key-value store — `@symmetra/kv`

```typescript
import { KvStore } from '@symmetra/kv';

const kv = new KvStore({ maxMb: 512 });

app.get('/item/:id', async (ctx) => {
  const cached = await kv.get(ctx.params.id);
  if (cached) { ctx.json(cached); return; }

  // ... fetch from db
  await kv.set(ctx.params.id, item, { ttlMs: 60_000 });
  ctx.json(item);
});
```

---

## Full language support

Symmetra supports the complete TypeScript / JavaScript language. For a detailed breakdown of features that are planned but not yet available, and workarounds for each, see [JS/TS compatibility](compatibility.md). Every feature either compiles directly to CUDA or is handled transparently by the runtime layer. The sections below explain how the non-obvious ones work.

---

### `eval()`

Supported via **runtime JIT**. When a handler calls `eval()`, the string is routed through the same compilation pipeline (esbuild → codegen → NVRTC) on a background thread. The resulting kernel is cached — identical eval strings only compile once. Subsequent calls have zero overhead.

```typescript
app.post('/compute', async (ctx) => {
  const { formula } = await ctx.json<{ formula: string }>();
  const result = eval(`(x) => ${formula}`)(42);
  ctx.json({ result });
});
```

---

### Node.js built-in imports

The compiler's import resolver automatically remaps Node.js built-ins to Symmetra equivalents. Existing Node.js code usually works without modification:

| Node.js import | Remapped to |
|----------------|-------------|
| `import fs from 'fs'` | `@symmetra/fs` |
| `import { createHash } from 'crypto'` | `@symmetra/crypto` |
| `import { EventEmitter } from 'events'` | `@symmetra/core` GpuEventEmitter |
| `import path from 'path'` | Built-in GPU path utilities |
| `import { fetch } from 'node-fetch'` | `@symmetra/net` |
| `import os from 'os'` | GPU system info API |

```typescript
// This works — no changes needed from Node.js code
import fs from 'fs';
import { createHash } from 'crypto';

app.get('/hash/:file', async (ctx) => {
  const content = await fs.promises.readFile(ctx.params.file);
  const digest  = createHash('sha256').update(content).digest('hex');
  ctx.json({ digest });
});
```

---

### Class inheritance

Full class inheritance compiles to C++ struct hierarchies in CUDA. Method dispatch uses static CRTP (Curiously Recurring Template Pattern) — which is actually *faster* than C++ virtual dispatch because it resolves at compile time.

```typescript
class Animal {
  name: string;
  constructor(name: string) { this.name = name; }
  speak(): string { return `${this.name} makes a sound`; }
}

class Dog extends Animal {
  speak(): string { return `${this.name} barks`; }
}

app.get('/animal', (ctx) => {
  const d = new Dog('Rex');
  ctx.json({ sound: d.speak() });   // "Rex barks"
});
```

---

### Dynamic imports

All reachable modules in the import graph are compiled **at startup** during the NVRTC JIT phase. At runtime, `import('./module')` is a synchronous registry lookup — the code is already compiled. The `await` is preserved syntactically for compatibility but resolves immediately.

```typescript
app.get('/plugin/:name', async (ctx) => {
  const plugin = await import(`./plugins/${ctx.params.name}`);
  const result = plugin.run(ctx);
  ctx.json(result);
});
```

---

### Prototype mutation

The AST walker collects **all prototype mutations** in the file before generating CUDA code. The complete, final type shape is assembled first, then emitted as a single CUDA C++ definition. There is no runtime cost.

```typescript
function Shape() {}
Shape.prototype.area = function() { return 0; };

function Circle(r: number) { this.r = r; }
Circle.prototype = Object.create(Shape.prototype);
Circle.prototype.area = function() { return Math.PI * this.r * this.r; };
```

---

### Map / Set

`Map<K, V>` and `Set<T>` compile to `GpuMap<K, V>` and `GpuSet<T>` — VRAM-resident open-addressing hash tables. Operations run on GPU with no host round-trips.

```typescript
app.post('/count', async (ctx) => {
  const words = await ctx.json<string[]>();
  const counts = new Map<string, number>();

  for (const word of words) {
    counts.set(word, (counts.get(word) ?? 0) + 1);
  }

  ctx.json(Object.fromEntries(counts));
});
```

---

### RegExp

Regular expressions are compiled to **GPU DFAs at kernel compile time** — not at runtime. The pattern is analyzed during codegen and emitted as a state machine in the CUDA kernel. Matching has zero JIT overhead.

```typescript
const emailRe = /^[^@]+@[^@]+\.[^@]+$/;

app.post('/validate', async (ctx) => {
  const { email } = await ctx.json<{ email: string }>();
  ctx.json({ valid: emailRe.test(email) });
});
```

---

### Generators

`function*` and `yield` compile to **GPU state machines**. Each `yield` point becomes an explicit state transition. The state object lives in VRAM.

```typescript
function* fibonacci() {
  let [a, b] = [0, 1];
  while (true) {
    yield a;
    [a, b] = [b, a + b];
  }
}

app.get('/fib/:n', (ctx) => {
  const n   = parseInt(ctx.params.n, 10);
  const gen = fibonacci();
  let   val = 0;
  for (let i = 0; i <= n; i++) val = gen.next().value as number;
  ctx.json({ n, value: val });
});
```

---

### try / catch / finally

Full error handling is supported. Exceptions are encoded as typed result codes and propagated through the CUDA call chain. `catch` blocks compile to conditional branches. `finally` blocks are always emitted after both paths.

```typescript
app.get('/safe/:id', async (ctx) => {
  try {
    const data = await riskyOperation(ctx.params.id);
    ctx.json({ ok: true, data });
  } catch (err) {
    ctx.status(500).json({ error: String(err) });
  } finally {
    cleanupResources();
  }
});
```

---

### Promise.all / race / any

`Promise.all()` compiles to **parallel CUDA stream dispatch** — each promise runs in a separate CUDA stream and a synchronization barrier waits for all of them. This is faster than sequential execution.

```typescript
app.get('/parallel', async (ctx) => {
  const [users, products, orders] = await Promise.all([
    fetchUsers(),
    fetchProducts(),
    fetchOrders(),
  ]);
  ctx.json({ users, products, orders });
});
```

---

### console.log

`console.log()` writes to a **GPU-side ring buffer** in VRAM. The buffer is flushed to stdout by the runtime after each request. Structured data is serialized on GPU — no CPU involvement in the hot path.

```typescript
app.get('/debug', (ctx) => {
  console.log('request from:', ctx.headers['x-forwarded-for']);
  ctx.json({ ok: true });
});
```

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
