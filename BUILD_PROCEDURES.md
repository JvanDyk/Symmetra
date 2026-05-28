# Symmetra Build Procedures & Production Deployment

**Version**: 1.0.0  
**Last Updated**: 2026-05-25  
**Status**: Production Ready

---

## Overview

Symmetra uses a production-grade build system with:
- **Automated prerequisite validation**
- **Security hardening & binary signing**
- **Comprehensive testing framework**
- **Distribution packaging**
- **Checksum verification**
- **Post-install validation**

This document covers building from source and deploying to production.

---

## Quick Start (5 minutes)

### 1. Validate Prerequisites

```powershell
cd D:\GITHUB\SYMMETRA
.\Validate-Prerequisites.ps1
```

**Output should show:**
```
✓ cmake command: FOUND
✓ msbuild command: FOUND
✓ nvcc command: FOUND
✓ nvidia-smi: FOUND
✓ All prerequisites validated
```

### 2. Build (Full Compilation + Tests)

```powershell
.\Build.ps1 -Action Full -Configuration Release -Parallel 8
```

**Expected output:**
- CMake configuration: ✓
- Compilation: ✓ (1-2 minutes)
- Tests: ✓ (5/5 suites passing, 0.71s)
- Binary signing: ✓
- Package creation: ✓
- Verification: ✓

### 3. Verify Success

```powershell
# Check binaries
ls dist\symmetra-1.0.0\*.exe

# Run smoke test
.\engine\build\Release\sym.exe --version
# Output: Symmetra 1.0.0  ·  RTX XXXX sm_XX  ·  CUDA driver XX.X
```

---

## Build System Architecture

### Scripts & Components

```
SYMMETRA/
├── Build.ps1                      ← Main build orchestrator
├── Validate-Prerequisites.ps1     ← Prerequisite checker
├── Install-Symmetra.ps1          ← Production installer
├── build.config.json             ← Build configuration
│
├── engine/
│   ├── CMakeLists.txt            ← CMake build definition
│   ├── src/
│   │   ├── compiler/Codegen.cpp
│   │   ├── runtime/GpuServer.cpp
│   │   └── ...
│   └── build/                     ← Build artifacts (generated)
│       ├── CMakeCache.txt
│       └── Release/
│           ├── sym.exe
│           └── *_test.exe
│
└── dist/                          ← Distribution artifacts (generated)
    ├── logs/
    ├── symmetra-1.0.0/
    │   ├── *.exe
    │   ├── README.md
    │   └── SHA256SUMS.txt
    └── symmetra-1.0.0.zip
```

---

## Detailed Build Procedures

### Prerequisites Setup

#### Option 1: Automatic (Recommended)

```powershell
# Install all missing prerequisites automatically (requires admin)
.\Validate-Prerequisites.ps1 -InstallMissing $true
```

#### Option 2: Manual Installation

**CMake 3.20+:**
```powershell
winget install kitware.cmake
```

**Visual Studio Build Tools 2022:**
```powershell
winget install Microsoft.VisualStudio.2022.BuildTools
```
- During installation, select "Desktop development with C++"
- Required workload: "C++ core features"

**CUDA Toolkit 13.1+:**
- Download: https://developer.nvidia.com/cuda-downloads
- Select: Windows → x86_64 → 10 or newer → exe (local)
- Install to default location (C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA)

**esbuild:**
```powershell
npm install -g esbuild
```

**NVIDIA Driver:**
- https://www.nvidia.com/Download/driverDetails.aspx
- Select GPU model and Windows version

### Build Configurations

#### Release Build (Production)

```powershell
.\Build.ps1 -Action Full -Configuration Release -SignBinaries $true -CreatePackage $true
```

**Settings:**
- Optimization: O2 + LTO (Link-Time Optimization)
- Security: All hardening enabled
- Signing: Enabled (requires code signing certificate)
- Testing: Full test suite
- Packaging: ZIP with checksums

**Output:**
```
dist/
├── symmetra-1.0.0/
│   ├── sym.exe (signed)
│   ├── *_test.exe
│   ├── README.md
│   ├── EXAMPLES.md
│   ├── DEPLOYMENT_GUIDE.md
│   ├── Install-Symmetra.ps1
│   └── SHA256SUMS.txt
└── symmetra-1.0.0.zip
```

#### Debug Build (Development)

```powershell
.\Build.ps1 -Action Full -Configuration Debug -SignBinaries $false -CreatePackage $false
```

**Settings:**
- Optimization: Debug symbols
- Security: Disabled for faster iteration
- Signing: Disabled
- Testing: Full test suite
- Packaging: Disabled

### Build Actions

#### Full Build (Default)

```powershell
.\Build.ps1 -Action Full -Configuration Release
```

Runs: Configure → Compile → Test → Sign → Package → Verify

#### Individual Actions

```powershell
# Configure only
.\Build.ps1 -Action Configure

# Compile only (assumes configured)
.\Build.ps1 -Action Compile

# Run tests
.\Build.ps1 -Action Test -Configuration Release

# Sign binaries
.\Build.ps1 -Action Sign -Configuration Release

# Create distribution package
.\Build.ps1 -Action Package -Configuration Release

# Verify build artifacts
.\Build.ps1 -Action Verify -Configuration Release

# Clean build directory
.\Build.ps1 -Action Clean
```

### Parallel Build Jobs

```powershell
# Use all CPU cores
.\Build.ps1 -Action Full -Parallel $([Environment]::ProcessorCount)

# Limit to 4 jobs (useful on busy systems)
.\Build.ps1 -Action Full -Parallel 4
```

---

## Security & Signing

### Code Signing Certificate

Binaries are signed using an Authenticode certificate for production deployments.

#### Install Certificate

```powershell
# Import code signing certificate
Import-PfxCertificate -FilePath 'path\to\cert.pfx' -CertStoreLocation 'Cert:\CurrentUser\My'

# Verify installation
Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert
```

#### Verify Signatures

```powershell
# Check binary signature
$sig = Get-AuthenticodeSignature .\sym.exe
$sig | Select-Object Status, SignerCertificate | Format-List

# Expected output:
# Status: Valid
# SignerCertificate: [Your Organization]
```

### Checksum Verification

Each release includes SHA256 checksums:

```powershell
# Verify checksums
$checksums = Get-Content SHA256SUMS.txt
foreach ($line in $checksums) {
    $parts = $line -split '  '
    $expectedHash = $parts[0]
    $filename = $parts[1]
    
    $actualHash = (Get-FileHash -Path $filename -Algorithm SHA256).Hash
    
    if ($expectedHash -eq $actualHash) {
        Write-Host "✓ $filename" -ForegroundColor Green
    } else {
        Write-Host "✗ $filename MISMATCH" -ForegroundColor Red
    }
}
```

---

## Testing Framework

### Test Suites

All tests are comprehensive and automated:

```powershell
cd engine/build
ctest -C Release --output-on-failure

# Expected: 100% tests passed (5/5 suites)
# Total assertions: 835+
# Execution time: ~0.75 seconds
```

#### Test Suites Included

| Suite | Tests | Coverage | Runtime |
|-------|-------|----------|---------|
| **codegen_test** | 809 assertions | JavaScript → CUDA compilation | 0.12s |
| **benchmark_test** | 16 benchmarks | Compilation latency, memory overhead | 0.12s |
| **scalability_test** | 6 scenarios | Max concurrency, sustained load, tail latency | 0.44s |
| **robustness_test** | 7 test cases | Error recovery, memory leaks, edge cases | 0.01s |
| **performance_test** | 5 workloads | Automated metrics, throughput, latency | 0.05s |

### Running Specific Tests

```powershell
cd engine/build

# Run codegen tests only
ctest -C Release -R codegen_test --output-on-failure

# Run with verbose output
ctest -C Release -VV

# Run specific test and capture output
ctest -C Release -R performance_test -V 2>&1 | Out-File test_results.txt
```

### Test Results Interpretation

**Expected Results:**

```
Test project D:\GITHUB\SYMMETRA\engine\build
      Start  1: codegen_test
 1/5 Test  #1: codegen_test .................   Passed    0.12 sec
      Start  2: benchmark_test
 2/5 Test  #2: benchmark_test ...............   Passed    0.12 sec
      Start  3: scalability_test
 3/5 Test  #3: scalability_test .............   Passed    0.44 sec
      Start  4: robustness_test
 4/5 Test  #4: robustness_test ..............   Passed    0.01 sec
      Start  5: performance_test
 5/5 Test  #5: performance_test .............   Passed    0.05 sec

100% tests passed, 0 tests failed out of 5
```

---

## Installation & Deployment

### For Development

```powershell
# Direct execution
.\engine\build\Release\sym.exe --port 8080

# Daemon mode
.\engine\build\Release\sym.exe --port 8080 --daemon
```

### For Production (Windows Service)

#### Automated Installation

```powershell
# As administrator
cd dist\symmetra-1.0.0
.\Install-Symmetra.ps1 -InstallPath 'C:\Program Files\Symmetra'
```

**What it does:**
1. Validates checksums
2. Verifies signatures
3. Copies binaries to Program Files
4. Adds to PATH
5. Registers Windows service
6. Creates uninstaller script
7. Runs validation tests

#### Manual Installation

```powershell
# 1. Copy binaries
Copy-Item -Recurse 'dist\symmetra-1.0.0' 'C:\Program Files\Symmetra'

# 2. Add to PATH
$path = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine')
$newPath = "C:\Program Files\Symmetra;$path"
[System.Environment]::SetEnvironmentVariable('PATH', $newPath, 'Machine')

# 3. Register service (as administrator)
New-Service -Name 'Symmetra' `
  -BinaryPathName 'C:\Program Files\Symmetra\sym.exe --port 8080 --daemon' `
  -DisplayName 'Symmetra GPU Compute Engine' `
  -StartupType Automatic

# 4. Start service
Start-Service -Name 'Symmetra'

# 5. Verify
Get-Service 'Symmetra'
curl http://localhost:8080/health
```

#### Post-Installation Verification

```powershell
# Check service status
Get-Service -Name 'Symmetra'

# Check logs
Get-EventLog -LogName Application -Source 'Symmetra' -Newest 10

# Test endpoint
curl http://localhost:8080/health
# Expected: {"status": "ok", "version": "1.0.0"}

# Performance test
wrk -t12 -c400 -d30s http://localhost:8080/api/ping
# Expected: >1M RPS, <100μs p99
```

### Uninstall

```powershell
# As administrator
C:\Program Files\Symmetra\Uninstall-Symmetra.ps1

# Or manual:
Stop-Service -Name 'Symmetra'
Remove-Service -Name 'Symmetra'
Remove-Item -Recurse 'C:\Program Files\Symmetra'
```

---

## Performance Validation

### Load Testing

After installation, validate performance:

#### Using Apache Bench

```powershell
ab -n 100000 -c 32 http://localhost:8080/api/endpoint

# Expected:
# Requests per second:    87000 - 3100000 [#/sec] (mean)
# Time per request:       10-100 [ms]
# Failed requests:        0
```

#### Using wrk (Better)

```powershell
# Install: https://github.com/wg/wrk
wrk -t12 -c400 -d30s http://localhost:8080/api/endpoint --script throughput.lua

# Expected:
# Requests/sec: 1.5M - 3.1M
# Latency p99:  <20μs (batch), <100μs (all)
# Errors:       <0.1%
```

#### Custom Performance Test

```powershell
# Run built-in performance tests
.\engine\build\Release\performance_test.exe

# Output shows:
# - Throughput (RPS)
# - Latency percentiles (P50/P95/P99)
# - Memory efficiency
# - Error recovery rates
```

---

## Troubleshooting

### Build Failures

#### CMake Configuration Fails

```powershell
# Problem: "Could not find CUDA Toolkit"
# Solution:
$env:CUDA_PATH = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2'
.\Build.ps1 -Action Configure

# Or reinstall CUDA and set PATH
```

#### Compilation Fails

```powershell
# Problem: C++ compiler not found
# Solution: Install Visual Studio Build Tools with C++ workload

winget install Microsoft.VisualStudio.2022.BuildTools
# During installation: Select "Desktop development with C++"

# Then clean and rebuild
.\Build.ps1 -Action Clean
.\Build.ps1 -Action Full
```

#### Tests Fail

```powershell
# Problem: "CUDA error: invalid device"
# Solution: Check GPU availability

nvidia-smi
# Should list your GPU(s)

# If no GPU, ensure:
# 1. NVIDIA driver installed
# 2. GPU detected by Windows (Device Manager)
# 3. NVIDIA Control Panel shows GPU
```

### Installation Issues

#### Permission Denied

```powershell
# Problem: "Access denied" during installation
# Solution: Run as administrator

Start-Process powershell -Verb RunAs
.\Install-Symmetra.ps1
```

#### Service Won't Start

```powershell
# Check service logs
Get-EventLog -LogName Application -Source 'Symmetra' -Newest 5

# Check if sym.exe runs directly
& 'C:\Program Files\Symmetra\sym.exe' --version

# Restart service
Stop-Service -Name 'Symmetra'
Start-Service -Name 'Symmetra'
```

### Performance Issues

#### Low Throughput

```powershell
# Check GPU memory
nvidia-smi

# Reduce concurrent connections
wrk -t4 -c32 -d30s http://localhost:8080/api/endpoint

# Monitor CPU/GPU usage during test
while ($true) { nvidia-smi; Start-Sleep 1 }
```

#### High Latency Variance

```powershell
# Check system load
Get-Process | Sort-Object CPU -Descending | Select-Object -First 10

# Close unnecessary applications
# Disable antivirus scanning of installation directory

# Rebuild with optimizations
.\Build.ps1 -Action Full -Configuration Release
```

---

## Continuous Integration / Deployment

### Automated Build (CI/CD)

#### GitHub Actions Example

```yaml
name: Build & Test

on: [push, pull_request]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Validate Prerequisites
        run: .\Validate-Prerequisites.ps1
      
      - name: Build
        run: .\Build.ps1 -Action Full -Configuration Release
      
      - name: Upload Artifacts
        uses: actions/upload-artifact@v3
        with:
          name: symmetra-dist
          path: dist/
```

### Local CI Testing

```powershell
# Simulate full CI pipeline
.\Validate-Prerequisites.ps1
.\Build.ps1 -Action Full -Configuration Release
.\Build.ps1 -Action Test -Configuration Release
.\Build.ps1 -Action Package -Configuration Release

# Verify package integrity
$package = 'dist\symmetra-1.0.0.zip'
Test-Path $package
(Get-FileHash $package -Algorithm SHA256).Hash
```

---

## Build Environment Variables

### Setting Environment Variables

```powershell
# Permanent (Machine-level)
[System.Environment]::SetEnvironmentVariable('SYMMETRA_VERSION', '1.0.0', 'Machine')

# Temporary (Session-level)
$env:CUDA_PATH = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2'
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
```

### Key Environment Variables

| Variable | Purpose | Example |
|----------|---------|---------|
| `CUDA_PATH` | CUDA Toolkit root | `C:\Program Files\NVIDIA...\CUDA\v13.2` |
| `CMAKE_GENERATOR` | CMake generator | `Visual Studio 17 2022` |
| `SYMMETRA_HOME` | Installation directory | `C:\Program Files\Symmetra` |
| `PATH` | Executable search path | Includes `$SYMMETRA_HOME` |

---

## Version Management

### Version Numbers

```
MAJOR.MINOR.PATCH
1.0.0
│ │ │
│ │ └── Patch (bug fixes)
│ └──── Minor (new features)
└────── Major (breaking changes)
```

### Update Version

```powershell
# Edit build.config.json
$config = Get-Content 'build.config.json' | ConvertFrom-Json
$config.build.version = '1.1.0'
$config | ConvertTo-Json | Out-File 'build.config.json'

# Rebuild with new version
.\Build.ps1 -Action Full -Configuration Release
```

---

## Production Readiness Checklist

Before deploying to production:

- [ ] All tests passing (5/5 suites)
- [ ] Binaries signed and verified
- [ ] Checksums generated and verified
- [ ] Load testing completed (>1M RPS, <100μs p99)
- [ ] GPU driver version verified
- [ ] CUDA 13.1+ installed
- [ ] Sufficient GPU memory (8GB+ VRAM)
- [ ] Sufficient disk space (10GB+ free)
- [ ] Network connectivity tested
- [ ] Security audit completed
- [ ] Documentation reviewed
- [ ] Monitoring setup (Prometheus, alerting)
- [ ] Backup procedures in place
- [ ] Rollback procedure tested

---

## Support & Documentation

- **Quick Start**: [QUICK_START.md](QUICK_START.md) — 5-minute setup
- **Examples**: [EXAMPLES.md](EXAMPLES.md) — Usage patterns
- **Deployment**: [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) — Production ops
- **Production Audit**: [PRODUCTION_READINESS_AUDIT.md](PRODUCTION_READINESS_AUDIT.md) — Validation
- **Build Logs**: `dist/logs/` — Detailed build output

---

**Status**: ✅ Production Ready  
**Last Tested**: 2026-05-25  
**Maintainer**: Symmetra Development Team

