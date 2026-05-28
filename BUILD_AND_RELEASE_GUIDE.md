# Complete Build & Release Guide - Step by Step

**Objective:** Build Symmetra engine and publish first release to GitHub

---

## 📋 STEP 1: Verify Prerequisites

### Check if dependencies are installed:

```powershell
# Open PowerShell as Administrator and run these checks:

# 1. Check CMake
cmake --version

# 2. Check Visual Studio (MSVC compiler)
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>&1 | head -1

# 3. Check CUDA Toolkit
nvcc --version

# 4. Check OpenSSL
openssl version
```

**If any are missing, install them:**

```powershell
# Install CMake (if missing)
winget install cmake

# Install OpenSSL (if missing)
winget install openssl

# CUDA Toolkit - Download from: https://developer.nvidia.com/cuda-downloads
# Visual Studio Build Tools - Download from: https://visualstudio.microsoft.com/downloads/
```

---

## 📋 STEP 2: Prepare Build Environment

```powershell
# Navigate to engine directory
cd D:\GITHUB\SYMMETRA\engine

# Clean previous build (if it exists)
if (Test-Path "build") {
    Remove-Item "build" -Recurse -Force
}

# Create fresh build directory
New-Item -ItemType Directory -Path "build" -Force

# Navigate to build directory
cd build
```

---

## 📋 STEP 3: Generate Build Files (CMake Configuration)

```powershell
# From: D:\GITHUB\SYMMETRA\engine\build

# Configure with CMake for Visual Studio 2022
cmake -G "Visual Studio 17 2022" `
       -A x64 `
       -DCMAKE_BUILD_TYPE=Release `
       -DCMAKE_CUDA_ARCHITECTURES=75 `
       -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL" `
       ..

# Expected output should show:
# -- Build type: Release
# -- CUDA toolkit found
# -- OpenSSL found (or fallback mode)
# -- Configuring done
```

**Troubleshooting:**
- If OpenSSL not found: Make sure it's installed in Program Files
- If CUDA not found: Install CUDA Toolkit from NVIDIA
- If Visual Studio not found: Install Visual Studio 2022 Community Edition

---

## 📋 STEP 4: Build the Engine (Windows - sym.exe)

```powershell
# From: D:\GITHUB\SYMMETRA\engine\build

cmake --build . `
       --config Release `
       --parallel 8

# Expected time: 10-30 minutes depending on CPU
# Expected output: Build succeeded, sym.exe created
```

**What to expect:**
```
Compiling CUDA files...
Compiling C++ files...
Linking sym.exe...
[100%] Built target sym
```

**Troubleshooting:**
- If build fails: Check build logs in `engine\build\CMakeFiles\`
- If memory error: Reduce `--parallel` to 4 or 2
- If CUDA errors: Make sure NVIDIA drivers are up to date

---

## 📋 STEP 5: Verify Windows Binary Built

```powershell
# From: D:\GITHUB\SYMMETRA\engine\build

# Check if sym.exe exists
Get-ChildItem -Path "Release\sym.exe" -ErrorAction SilentlyContinue

# If found, run a quick test
.\Release\sym.exe --version

# Or run the test suite
ctest -C Release --output-on-failure
```

---

## 📋 STEP 6: Build Distribution Packages (Windows)

```powershell
# Navigate to packaging scripts
cd D:\GITHUB\SYMMETRA\packaging\windows

# Create ZIP package
.\create-zip.ps1

# Create MSI installer (requires WiX toolset)
# Install WiX first: winget install wixtoolset
.\create-msi.ps1
```

**Expected outputs:**
```
symmetra-1.0.0-windows-x64.zip
symmetra-1.0.0-windows-x64.msi
```

---

## 📋 STEP 7: Prepare Linux Build (Cross-Compile)

**Note:** Linux binary requires Linux environment. Options:

### Option A: Use WSL (Windows Subsystem for Linux)
```bash
# Enable WSL if not already
wsl --install

# In WSL bash:
cd /mnt/d/GITHUB/SYMMETRA/engine
mkdir build-linux
cd build-linux
cmake -G "Unix Makefiles" \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_CUDA_ARCHITECTURES=75 \
       ..
cmake --build . --parallel 8
```

### Option B: Skip Linux Build (GitHub Actions will build it)
Let the CI/CD pipeline (`release.yml`) handle the Linux build automatically when you push the tag.

---

## 📋 STEP 8: Verify All Assets

```powershell
# Check what you have built so far
Get-ChildItem "D:\GITHUB\SYMMETRA\packaging\windows\*.{zip,msi}" -ErrorAction SilentlyContinue

# You should have:
# - symmetra-1.0.0-windows-x64.zip
# - symmetra-1.0.0-windows-x64.msi (if WiX build succeeded)
```

---

## 📋 STEP 9: Tag Version in Git

```powershell
# Navigate to private repository
cd D:\GITHUB\SYMMETRA

# Configure git (if not already done)
git config user.email "your-email@example.com"
git config user.name "Your Name"

# Create version tag
git tag -a v1.0.0 -m "Release version 1.0.0: GPU-accelerated TypeScript compiler

Features:
- CUDA code generation from TypeScript
- 3.1M RPS sustained throughput
- <20μs P99 latency
- Zero garbage collection overhead
- TLS 1.3 PSK encryption
- Multiple distribution formats
"

# Verify tag created
git tag | grep v1.0.0

# Push tag to GitHub (triggers CI/CD)
git push origin v1.0.0
```

---

## 📋 STEP 10: Monitor GitHub Actions

After pushing the tag, the release workflow starts automatically:

1. **Go to:** https://github.com/JvanDyk/Symmetra/actions
2. **Watch:** `.github/workflows/release.yml` running
3. **Monitor:**
   - Windows build job
   - Linux build job
   - Packaging jobs
   - Publish job

**Expected completion time:** 30-45 minutes

---

## 📋 STEP 11: Verify GitHub Release Created

Once CI/CD completes:

1. **Go to:** https://github.com/JvanDyk/Symmetra/releases
2. **Verify:** Release tag `v1.0.0` created
3. **Check assets:**
   ```
   ✓ symmetra-1.0.0-windows-x64.zip
   ✓ symmetra-1.0.0-windows-x64.msi
   ✓ symmetra-1.0.0-linux-x64.deb
   ✓ symmetra-1.0.0-linux-x64.rpm
   ✓ symmetra-1.0.0-linux-x64.snap
   ✓ sym-linux-x64
   ✓ SHA256SUMS.txt
   ```

---

## 📋 STEP 12: Test Installation Scripts

```powershell
# Test Windows installer (downloads from GitHub Releases)
iwr https://symmetra.dev/install.ps1 | iex

# Or test Linux installer (if on WSL)
# curl -fsSL https://symmetra.dev/install.sh | bash
```

---

## ✅ Success Criteria

- [x] CMake configuration succeeds
- [x] Windows binary (sym.exe) builds successfully
- [x] Tests pass (ctest)
- [x] Distribution packages created
- [x] Version tag pushed to GitHub
- [x] GitHub Actions workflow completes
- [x] GitHub Release published with all assets
- [x] Installation scripts download from latest release

---

## 🔧 Troubleshooting

### CMake Can't Find CUDA
```powershell
# Set CUDA path explicitly
$env:CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6"
cmake -G "Visual Studio 17 2022" -A x64 ..
```

### CMake Can't Find OpenSSL
```powershell
# Set OpenSSL path explicitly
cmake -G "Visual Studio 17 2022" `
       -A x64 `
       -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL" `
       ..
```

### Build Fails with Memory Error
```powershell
# Reduce parallel jobs
cmake --build . --config Release --parallel 2
```

### Git Tag Push Fails
```powershell
# Ensure you're in the correct directory
cd D:\GITHUB\SYMMETRA

# Verify remote is set
git remote -v

# If origin is wrong, fix it
git remote set-url origin https://github.com/JvanDyk/Symmetra.git
```

---

## 📞 Quick Reference

| Step | Command | Expected Time |
|------|---------|---------------|
| 1. Prerequisites | `cmake --version` | 1 min |
| 2. Build Prepare | `cmake -G ...` | 2-5 min |
| 3. Build Engine | `cmake --build ...` | 15-30 min |
| 4. Create Packages | `.\create-zip.ps1` | 2-5 min |
| 5. Tag Version | `git tag v1.0.0` | 1 min |
| 6. Push Tag | `git push origin v1.0.0` | 1 min |
| 7. CI/CD Complete | Monitor actions | 30-45 min |

**Total time: ~1-2 hours**

---

**Next:** Follow the steps in order. Start with STEP 1 (prerequisites check).
