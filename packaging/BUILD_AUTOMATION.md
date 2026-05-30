# Symmetra Engine Build Automation

Automated build, packaging, and distribution scripts for Turing Machine engines.

## Overview

Two automation scripts handle single and batch engine building:

1. **`build-engine.ps1` / `build-engine.sh`** - Build a single engine
2. **`build-all-engines.ps1` / `build-all-engines.sh`** - Build all engines (sequential or parallel)

## Prerequisites

### Windows (PowerShell)
```powershell
# Required:
# - PowerShell 5.1+ or PowerShell 7+
# - NVIDIA CUDA 12.1+ (for CUDA compilation)
# - CMake 3.20+ (for building)

# Optional:
# - Docker Desktop (for Docker image builds)
# - Git (for version control)

# Verify installation:
$PSVersionTable.PSVersion
nvcc --version
cmake --version
docker --version
```

### Linux/Mac (Bash)
```bash
# Required:
# - Bash 4.0+
# - NVIDIA CUDA 12.1+
# - CMake 3.20+

# Optional:
# - Docker (for Docker image builds)
# - GNU Parallel (for parallel builds)

# Verify installation:
bash --version
nvcc --version
cmake --version
docker --version
parallel --version
```

## Single Engine Build

### PowerShell

```powershell
# Build a single engine with all artifacts
./packaging/build-engine.ps1 -EngineName "rayk-ultra" -Version "1.0.0"

# Build without Docker images
./packaging/build-engine.ps1 -EngineName "bitwise-rule60" -Version "1.0.0" -BuildDocker $false

# Build only CUDA, skip packaging
./packaging/build-engine.ps1 -EngineName "prism-x" -BuildZip $false
```

**Parameters:**
- `-EngineName` (required): Engine name (e.g., "rayk-ultra", "bitwise-rule60")
- `-Version` (optional): Version number (default: "1.0.0")
- `-BuildDocker` (optional): Create Docker image (default: true)
- `-BuildZip` (optional): Create Windows ZIP package (default: true)
- `-BuildDeb` (optional): Create Linux DEB package (default: false)

### Bash

```bash
# Build a single engine
./packaging/build-engine.sh -e rayk-ultra -v 1.0.0

# Build without Docker
./packaging/build-engine.sh -e bitwise-rule60 --no-docker

# Build only CUDA, no packaging
./packaging/build-engine.sh -e prism-x --no-tarball
```

**Parameters:**
- `-e|--engine` (required): Engine name
- `-v|--version` (optional): Version number (default: "1.0.0")
- `--no-docker`: Skip Docker image build
- `--deb`: Create Linux DEB package
- `--no-tarball`: Skip Linux tarball packaging

## Batch Build All Engines

### PowerShell - Sequential

```powershell
# Build all engines sequentially
./packaging/build-all-engines.ps1 -Version "1.0.0"

# Skip failures and continue
./packaging/build-all-engines.ps1 -Version "1.0.0" -SkipFailures

# Build without Docker
./packaging/build-all-engines.ps1 -Version "1.0.0" -BuildDocker $false
```

### PowerShell - Parallel

```powershell
# Build all engines in parallel (4 workers)
./packaging/build-all-engines.ps1 -Version "1.0.0" -Parallel

# Parallel with custom worker count
./packaging/build-all-engines.ps1 -Version "1.0.0" -Parallel -MaxParallel 8
```

**Parameters:**
- `-Version` (optional): Version number (default: "1.0.0")
- `-Parallel` (optional): Enable parallel builds (default: sequential)
- `-MaxParallel` (optional): Number of parallel workers (default: 4)
- `-BuildDocker` (optional): Build Docker images (default: true)
- `-BuildZip` (optional): Build Windows packages (default: true)
- `-SkipFailures` (optional): Continue on build failure (default: stop)

### Bash - Sequential

```bash
# Build all engines sequentially
./packaging/build-all-engines.sh -v 1.0.0

# Skip failures and continue
./packaging/build-all-engines.sh -v 1.0.0 --skip-failures

# Build without Docker
./packaging/build-all-engines.sh -v 1.0.0 --no-docker
```

### Bash - Parallel

```bash
# Build all engines in parallel (4 workers, requires GNU parallel)
./packaging/build-all-engines.sh -v 1.0.0 --parallel

# Parallel with custom worker count
./packaging/build-all-engines.sh -v 1.0.0 --parallel -j 8
```

**Parameters:**
- `-v|--version` (optional): Version number (default: "1.0.0")
- `--parallel` (optional): Enable parallel builds
- `-j|--jobs` (optional): Number of parallel workers (default: 4)
- `--no-docker`: Skip Docker image builds
- `--skip-failures`: Continue on build failure

## Build Output

### Directory Structure

```
build/
└── engines/
    ├── rayk-ultra/
    │   ├── turing_machine.ts      # TypeScript source
    │   ├── engine.cu              # Compiled CUDA code
    │   ├── engine.o               # CUDA object file
    │   └── Dockerfile             # Docker image definition
    ├── bitwise-rule60/
    │   ├── turing_machine.ts
    │   ├── engine.cu
    │   ├── engine.o
    │   └── Dockerfile
    └── ...

release-packages/
├── SHA256SUMS.txt                 # Checksum manifest
├── symmetra-tm-rayk-ultra-1.0.0-windows-x64.zip
├── symmetra-tm-rayk-ultra-1.0.0-linux-x64.tar.gz
├── symmetra-tm-bitwise-rule60-1.0.0-windows-x64.zip
└── ...
```

### Artifacts

Each build generates:
1. **Source Files**
   - `turing_machine.ts` - TypeScript source
   - `Dockerfile` - Container definition

2. **Compiled Artifacts**
   - `engine.cu` - CUDA C++ code
   - `engine.o` - Compiled object file

3. **Distribution Packages**
   - `symmetra-tm-{name}-{version}-windows-x64.zip`
   - `symmetra-tm-{name}-{version}-linux-x64.tar.gz`

4. **Docker Images** (if enabled)
   - `symmetra:tm-{name}:{version}`

## Next Steps

After building:

### Test Engines
```bash
# Test single engine
./packaging/test-engine.ps1 -EngineName "rayk-ultra"

# Test all engines
./packaging/test-all-engines.ps1 -Version "1.0.0"
```

### Push to Registry
```bash
# Push Docker images
./packaging/push-all-engines.ps1 -Version "1.0.0"

# Upload to GitHub Releases
./packaging/create-release.ps1 -Version "1.0.0"
```

### Generate Checksums
```bash
# Generate SHA256SUMS
./packaging/generate-checksums.ps1 -Version "1.0.0"
```

## Configuration

### Build Settings

Create `packaging/.env` to customize defaults:

```powershell
# .env (PowerShell syntax)
$DOCKER_REGISTRY = "docker.io"
$DOCKER_ORG = "symmetra"
$BUILD_THREADS = 8
$ENABLE_CACHE = $true
$SKIP_TESTS = $false
```

### Advanced Options

```powershell
# Custom CUDA architecture
./packaging/build-engine.ps1 -EngineName "rayk-ultra" -CudaArch "sm_90"

# Production build (strip symbols, optimize)
./packaging/build-all-engines.ps1 -Version "1.0.0" -Production

# Dry run (show what would be built)
./packaging/build-all-engines.ps1 -Version "1.0.0" -DryRun
```

## Troubleshooting

### CUDA Compilation Errors
```
Error: nvcc: command not found
```
Install NVIDIA CUDA Toolkit or add to PATH:
```powershell
$env:PATH += ";C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.1\bin"
```

### Docker Build Failures
```
Error: Cannot connect to Docker daemon
```
Ensure Docker Desktop is running:
```powershell
docker ps  # Verify connection
```

### Out of Memory
Reduce parallel worker count:
```powershell
./packaging/build-all-engines.ps1 -MaxParallel 2
```

## Performance

### Build Times (Typical)

| Configuration | Time per Engine | 9 Engines | 351 Engines |
|---|---|---|---|
| Sequential | 2-5 min | 20-45 min | 11-29 hours |
| Parallel (4) | 2-5 min | 5-15 min | 3-8 hours |
| Parallel (8) | 2-5 min | 3-8 min | 1.5-4 hours |

### Optimization Tips

1. **Parallel Builds**: Use `--parallel` for multi-engine builds
2. **Disable Docker**: Skip Docker if not needed (`--no-docker`)
3. **Skip Tests**: Use `--skip-tests` during development
4. **Cache Layers**: Enable build cache for faster incremental builds
5. **Dedicated GPU**: Ensure no other CUDA processes running

## CI/CD Integration

### GitHub Actions

```yaml
- name: Build All Engines
  run: |
    if [ "$RUNNER_OS" = "Windows" ]; then
      .\packaging\build-all-engines.ps1 -Version ${{ github.ref_name }} -Parallel
    else
      ./packaging/build-all-engines.sh -v ${{ github.ref_name }} --parallel
    fi
```

### GitLab CI

```yaml
build_engines:
  stage: build
  script:
    - ./packaging/build-all-engines.sh -v $CI_COMMIT_TAG --parallel -j 4
  artifacts:
    paths:
      - release-packages/
```

## Version Management

### Release Naming Convention

```
symmetra-tm-{family}-{variant}-{version}-{platform}.{ext}

Examples:
- symmetra-tm-rayk-ultra-1.0.0-windows-x64.zip
- symmetra-tm-bitwise-rule60-1.0.1-linux-x64.tar.gz
- symmetra:tm-prism-x:1.0.0  (Docker)
```

### Semantic Versioning

- **1.0.0**: Initial release
- **1.1.0**: Performance improvements (backward compatible)
- **1.2.0**: New features (backward compatible)
- **2.0.0**: Breaking changes

## Support

For issues or questions:
- Check build logs in `build/engines/{name}/build.log`
- Review performance reports in `release-packages/PERFORMANCE_REPORTS/`
- See [ENGINE_NAMING_STANDARD.md](../ENGINE_NAMING_STANDARD.md) for version info

---

**Last Updated**: May 31, 2026  
**Version**: 1.0.0
