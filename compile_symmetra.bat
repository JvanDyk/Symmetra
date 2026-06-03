@echo off
setlocal enabledelayedexpansion

echo ╔════════════════════════════════════════════════════════════╗
echo ║        SYMMETRA GPU COMPILER — CUDA COMPILATION           ║
echo ╚════════════════════════════════════════════════════════════╝

REM Set CUDA path
set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2

REM Set MSVC path
set MSVC_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64

REM Add to PATH
set PATH=%MSVC_PATH%;%CUDA_PATH%\bin;%PATH%

echo.
echo CUDA Path: %CUDA_PATH%
echo MSVC Path: %MSVC_PATH%
echo.

REM Verify tools
echo Checking tools...
%CUDA_PATH%\bin\nvcc.exe --version
cl.exe /? >/dev/null 2>&1 && echo ✓ MSVC found || echo ✗ MSVC not found

echo.
echo Attempting compilation with CUDA 13.2...
echo.

REM Compile with full optimization
%CUDA_PATH%\bin\nvcc.exe ^
  -gencode arch=compute_75,code=sm_75 ^
  -gencode arch=compute_80,code=sm_80 ^
  -gencode arch=compute_86,code=sm_86 ^
  -O3 ^
  -use_fast_math ^
  --expt-relaxed-constexpr ^
  -std=c++17 ^
  -allow-unsupported-compiler ^
  -o symmetra.exe ^
  engine\src\compiler\PreludeData.cpp ^
  engine\src\compiler\test_codegen.cpp

if %ERRORLEVEL% EQU 0 (
  echo.
  echo ✓ COMPILATION SUCCESSFUL
  echo.
  dir symmetra.exe
  echo.
  echo Running tests...
  symmetra.exe
) else (
  echo.
  echo ✗ Compilation failed with error code %ERRORLEVEL%
  exit /b 1
)

endlocal
