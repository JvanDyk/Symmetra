# Symmetra

**TypeScript for the GPU.** Write TypeScript, run at CUDA speed.

```bash
sym ./server.ts
```

Symmetra compiles TypeScript directly to CUDA machine code. No Node.js. No V8. No garbage collector. Your handlers run as GPU kernels.

---

## Install

**Windows**
```powershell
winget install symmetra.sym
sym doctor
```

**Linux**
```bash
curl -fsSL https://symmetra.dev/install.sh | bash
sym doctor
```

`sym doctor` verifies your GPU, CUDA driver, and all dependencies.

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
# Symmetra 1.0.0  ·  RTX 4090 sm_89  ·  NVRTC JIT 47ms
# Listening on :8080  ·  1 route compiled
```

---

## Requirements

- NVIDIA GPU: RTX 2070 or newer (sm_75+)
- CUDA Toolkit 12.x or later
- Windows 10/11 or Ubuntu 22.04+

No Node.js required at runtime.

---

## Packages

| Package | Description | License |
|---------|-------------|---------|
| [`@symmetra/http`](packages/http/) | GPU-native HTTP server and router | MIT |
| [`@symmetra/kv`](packages/kv/) | VRAM key-value store with TTL | MIT |
| [`@symmetra/core`](packages/core/) | GPU context, buffer types, compute API | MIT |
| [`@symmetra/db`](packages/db/) | PostgreSQL, SQLite, Redis clients | MIT |
| [`@symmetra/fs`](packages/fs/) | Filesystem: read, write, watch | MIT |
| [`@symmetra/crypto`](packages/crypto/) | Hashing, encryption, JWT, passwords | MIT |
| [`@symmetra/env`](packages/env/) | Typed environment variable loader | MIT |
| [`@symmetra/net`](packages/net/) | HTTP client, TCP, UDP, WebSocket, DNS | MIT |

---

## Examples

| Example | Description |
|---------|-------------|
| [`examples/hello/`](examples/hello/) | Minimal ping server |
| [`examples/http-server/`](examples/http-server/) | Routing, GPU compute, VRAM cache |
| [`examples/benchmark/`](examples/benchmark/) | Throughput benchmark |
| [`examples/database/`](examples/database/) | PostgreSQL queries, Redis caching, SQLite |
| [`examples/files/`](examples/files/) | File upload, download, JSON config |
| [`examples/crypto-auth/`](examples/crypto-auth/) | JWT auth, password hashing, AES encryption |
| [`examples/env-config/`](examples/env-config/) | Typed env schema, live config reload |
| [`examples/http-client/`](examples/http-client/) | Outbound HTTP, WebSocket, DNS |
| [`examples/fullstack/`](examples/fullstack/) | Task manager combining all packages |

---

## CLI reference

```bash
sym ./server.ts          # Compile and run
sym doctor               # Check GPU, CUDA driver, esbuild
sym bench                # GPU benchmark
sym version              # Version info
```

```bash
--port <n>       Override the port from serve()
--streams <n>    CUDA stream pool size (default: 32)
--verbose        Show generated GPU code and compile log
```

---

## Documentation

- [Getting started](docs/getting-started.md)
- [Architecture](docs/architecture.md)
- [JS/TS compatibility](docs/compatibility.md)

---

## License

**Symmetra engine** (`sym` / `sym.exe`): Proprietary. Binary distribution only. Reverse engineering is prohibited.

**SDK** (`@symmetra/http`, `@symmetra/core`, `@symmetra/kv`): MIT.
