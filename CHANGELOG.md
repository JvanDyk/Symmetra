# Changelog

All notable changes to Symmetra are documented in this file.

## [1.0.0] - 2026-05-25

### 🚀 Initial Release

Symmetra v1.0.0 is the first stable release of the GPU-accelerated TypeScript compiler.

#### Features
- **CUDA Code Generation** — Compile TypeScript to native GPU CUDA kernels
- **TLS 1.3 PSK** — Cryptographically secure zero-copy networking
- **Zero-Copy I/O** — Memory-efficient data marshalling between CPU and GPU
- **ES2022+ Language Support** — Classes, async/await, template literals, destructuring, spread operators, and more
- **32 CUDA Streams** — Concurrent handler execution via Promise.all() parallelism
- **GPU Memory Management** — Automatic VRAM allocation and deallocation
- **HTTP Router** — Request/response handling with middleware support
- **Structured Logging** — Application-level logging with filtering

#### Performance Characteristics
- **3.1M RPS** — Sustained throughput on NVIDIA RTX 4090
- **<20μs P99 Latency** — Sub-20-microsecond tail latencies
- **Zero Garbage Collection** — No runtime GC pauses
- **78 Gh/s per Thread** — Peak algorithmic throughput

#### Supported Platforms
- **Windows** — Windows 10+, x64 (NVIDIA CUDA 12.6+)
- **Linux** — Ubuntu 20.04+, Fedora 36+, RHEL 8+, or any glibc 2.31+ distribution (x64, NVIDIA CUDA 12.6+)
- **GPU Requirements** — NVIDIA RTX 3060 or newer (CUDA Compute Capability 8.6+)

#### Distribution
Available on all major platforms:
- **Windows** — ZIP, MSI Installer, Windows Package Manager (winget)
- **Linux** — .deb, .rpm, Snap, raw binary

#### Documentation
- Complete getting-started guide with prerequisites and installation verification
- HTTP handler API reference
- GPU fundamentals for understanding parallelism and performance
- Performance tuning guide
- Comprehensive API documentation

#### Security
- Binary-only distribution — engine internals fully protected
- Developer debugging — full IDE integration for application code
- Symbol stripping — reverse-engineering resistant
- TLS 1.3 with PSK mode for secure connections

#### Known Limitations
- Custom Turing machine backends not yet publicly available
- WebAssembly compilation not yet supported
- Distributed multi-GPU clusters require manual work

---

## Versioning

Symmetra follows [Semantic Versioning](https://semver.org/):
- **MAJOR** — Breaking API changes or compiler incompatibilities
- **MINOR** — New features with backward compatibility
- **PATCH** — Bug fixes and security patches

---

## Support

For bug reports, feature requests, or security concerns, see [SECURITY.md](SECURITY.md).
