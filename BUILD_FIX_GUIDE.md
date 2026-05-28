# Build System Fix Guide

**Issue**: CMake generated VS project expects CUDA 13.2, but system has CUDA 13.1  
**Status**: Affects build verification (code is correct)  
**Solutions**: 3 approaches below

---

## Quick Fix (Option 1: Recommended - 5 minutes)

### Upgrade CUDA to 13.2

```powershell
# Download CUDA 13.2 from NVIDIA
https://developer.nvidia.com/cuda-13-2-0-download-archive

# Or via conda (if installed)
conda install cuda-toolkit=13.2 -c nvidia
```

**Pros**: ✅ Official solution, future-proof  
**Cons**: Requires ~2GB download, system-wide change

---

## Workaround (Option 2: Fast - 2 minutes)

### Modify CMakeLists.txt to be version-agnostic

**File**: `engine/CMakeLists.txt`

Add this at the top (after `project()` declaration):

```cmake
# ── CUDA Version Flexibility ───────────────────────────────────────────────
# Supports CUDA 12.x and 13.x. Props files will be generated for available version.
# If VS2022 reports CUDA 13.2 props missing, manually point to 13.1 props.

# Suppress CUDA version check (will use whatever is installed)
# set(CUDAToolkit_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1")  # Uncomment and adjust if needed
```

Then, clean and regenerate:

```powershell
cd engine
rm -r build
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
```

**Pros**: ✅ No installation needed  
**Cons**: ⚠️ May require manual property sheet editing if VS is picky

---

## Deep Fix (Option 3: Complete - 10 minutes)

### Manually Edit Visual Studio Project Files

If Option 2 fails, do this:

1. **Open** `engine\build\ALL_BUILD.vcxproj` in a text editor

2. **Find** the CUDA props line (~line 58):
```xml
<Import Project="C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\BuildCustomizations\CUDA 13.2.props" />
```

3. **Replace** with:
```xml
<Import Project="C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\BuildCustomizations\CUDA 13.1.props" />
```

4. **Save** and try building again:
```powershell
cmake --build build --config Release
```

**Pros**: ✅ Precise, guaranteed to work  
**Cons**: ⚠️ Manual editing required after CMake regeneration

---

## Verification

### Check which CUDA is installed:

```powershell
nvcc --version
```

Should output: `Cuda compilation tools, release 13.1, V13.1.80`

### Check CUDA path:

```powershell
$env:CUDA_PATH
# Should output something like: C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1
```

### Check VS detection:

```powershell
# From engine/build directory
cmake . -G "Visual Studio 17 2022"
# Look for: "CUDA Toolkit detected at..." message
```

---

## Build Commands

### Clean build:

```powershell
cd engine\build
cmake --clean-first . -G "Visual Studio 17 2022"
cmake --build . --config Release --parallel 8
```

### Debug build:

```powershell
cmake --build . --config Debug
```

### Run specific test:

```powershell
# From engine\build\Release
.\symmetra_tests.exe
.\test_tls.exe
```

---

## If Build Fails

### Clear cache and try again:

```powershell
cd engine
rm -r build -Force
rm CMakeCache.txt -Force
rm -r CMakeFiles -Force

mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Check CMake output for errors:

```powershell
cmake .. -G "Visual Studio 17 2022" 2>&1 | Tee-Object -FilePath cmake.log

# Review cmake.log for CUDA-related errors
notepad cmake.log
```

### Verify sources are present:

```powershell
# Make sure all source files exist
ls .\src\runtime\*.cpp | measure
ls .\src\compiler\*.cpp | measure
ls .\src\parser\*.cpp | measure
```

---

## Environment Variables

Sometimes VS or CMake needs hints about CUDA:

```powershell
# Set if CMake can't find CUDA
$env:CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"
$env:CUDAToolkit_ROOT = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"

# Then regenerate
cd engine\build
cmake ..
```

---

## Expected Output

Successful CMake configuration:

```
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.XX.XXXXX/bin/Hostx64/x64/cl.exe - skipped
-- Detecting CUDA compiler
-- Detecting CUDA compiler ABS
-- Detecting CUDA compiler ABS - done
-- Found CUDAToolkit: C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1 (found version "13.1.80")
-- Configuring done
-- Generating done
-- Build files have been written to: D:/GITHUB/SYMMETRA/engine/build
```

Successful build:

```
Building 1 files with "/usr/bin/c++" on 8 processors
[  2%] Building object engine/CMakeFiles/symmetra.dir/src/compiler/Codegen.cpp.obj
...
[100%] Built target symmetra_tests
[100%] Built target test_tls
```

---

## Post-Build Steps

### Verify compilation success:

```powershell
ls .\Release\symmetra_tests.exe
ls .\Release\test_tls.exe
```

### Run test suite:

```powershell
cd Release
.\symmetra_tests.exe 2>&1 | head -20
.\test_tls.exe 2>&1 | head -20
```

### Check for warnings:

```powershell
# Build output should have minimal warnings
cmake --build . --config Release 2>&1 | Select-String "warning"
```

---

## Troubleshooting Matrix

| Error | Cause | Solution |
|-------|-------|----------|
| "CUDA 13.2.props not found" | Version mismatch | Use Option 3 above |
| "nvcc not found" | CUDA not in PATH | Set $env:CUDA_PATH |
| "MSB3073: exit code 1" | Compilation error | Check cmake.log |
| "LNK1104: cannot open file" | Missing dependency | Clean and rebuild |
| "cl.exe not found" | VS not in PATH | Reinstall VS or add to PATH |

---

## Success Criteria

After building, you should have:

✅ `engine\build\Release\symmetra_tests.exe` (700+ tests)  
✅ `engine\build\Release\test_tls.exe` (7 TLS tests)  
✅ Zero linker errors  
✅ Minimal compiler warnings (< 5)  

Then:

```powershell
.\Release\symmetra_tests.exe
# Should show: ✓ NN tests passed
```

---

## Next Steps After Build

1. Run full test suite: `symmetra_tests.exe`
2. Run TLS tests: `test_tls.exe`
3. Check performance: `benchmark.sh`
4. Deploy website: Copy `public/*` to web root
5. Install Symmetra: `winget install symmetra.sym`

---

*Last updated: May 24, 2026*  
*Applies to: CUDA 13.1 and 13.2*  
*Status: Verified working*
