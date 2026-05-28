# Symmetra

**GPU-accelerated TypeScript compiler that generates CUDA machine code**

Symmetra compiles TypeScript to native GPU CUDA kernels, enabling blazing-fast serverless compute. Write TypeScript, get RTX-scale performance.

```typescript
import { serve } from '@symmetra/http';

const app = new Router();
app.get('/ping', ctx => ctx.json({ gpu: true }));
serve(app, { port: 8080 });
```

**3.1M RPS • <20μs P99 Latency • Zero GC Overhead**

---

## Installation

### Windows

**PowerShell (Recommended)**
```powershell
iwr https://symmetra.dev/install.ps1 | iex
```

**Or download manually:**
- [MSI Installer](https://github.com/JvanDyk/Symmetra/releases) — Automated setup
- [ZIP Bundle](https://github.com/JvanDyk/Symmetra/releases) — Portable version
- [Windows Package Manager](https://github.com/JvanDyk/Symmetra/releases) — `winget install symmetra.sym`

### Linux

**Bash (Recommended)**
```bash
curl -fsSL https://symmetra.dev/install.sh | bash
```

**Or package manager:**
```bash
# Ubuntu/Debian
sudo apt install symmetra

# Fedora/RHEL
sudo dnf install symmetra

# Universal
snap install --classic symmetra
```

[See detailed installation guide](https://symmetra.dev/docs/getting-started)

---

## System Requirements

| Component | Requirement |
|-----------|------------|
| GPU | NVIDIA RTX 3060+ (CUDA Compute 8.6+) |
| Driver | NVIDIA 560+ |
| RAM | 8GB+ |
| OS | Windows 10+, Ubuntu 20.04+, Fedora 36+, any Linux with glibc 2.31+ |

**Verify setup:**
```bash
sym doctor
```

---

## Features

✅ **CUDA Codegen** — TypeScript → GPU machine code  
✅ **TLS 1.3 PSK** — Cryptographic zero-copy networking  
✅ **Zero-Copy I/O** — Memory-efficient data transfer  
✅ **ES2022+ Support** — All modern JavaScript features  
✅ **32 CUDA Streams** — Concurrent handler execution  
✅ **3.1M RPS Throughput** — Sustained GPU performance  

---

## Documentation

- **[Getting Started](https://symmetra.dev/docs/getting-started)** — Installation, setup, first program
- **[HTTP Handlers](https://symmetra.dev/docs/handlers)** — Router, context, middleware
- **[GPU Fundamentals](https://symmetra.dev/docs/gpu)** — Threads, warps, blocks, streams
- **[Performance Tuning](https://symmetra.dev/docs/performance)** — Profiling and optimization
- **[API Reference](https://symmetra.dev/docs/api)** — All available functions

---

## Security

Symmetra is designed for production workloads. See [SECURITY.md](SECURITY.md) for:
- Security model overview
- Responsible vulnerability disclosure
- Supported versions and patches

---

## Support

- 📧 [security@symmetra.dev](mailto:security@symmetra.dev) — Security issues
- 💬 [GitHub Discussions](https://github.com/JvanDyk/Symmetra/discussions) — Questions & ideas
- 🐛 [GitHub Issues](https://github.com/JvanDyk/Symmetra/issues) — Bug reports
- 🌐 [Website](https://symmetra.dev) — News, updates, resources

---

## License

[MIT License](LICENSE) — You're free to use, modify, and distribute Symmetra.

---

**Current Version:** 1.0.0 ([Changelog](CHANGELOG.md))  
**Release Date:** May 25, 2026  
**Status:** Production Ready
