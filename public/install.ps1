#!/usr/bin/env pwsh
# Symmetra installer for Windows
# Usage: iwr https://symmetra.dev/install.ps1 | iex

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SYMMETRA_VERSION = "1.0.0"
$INSTALL_DIR = "$env:USERPROFILE\.symmetra\bin"
$BINARY_URL  = "https://github.com/YOUR_USERNAME/symmetra/releases/download/v$SYMMETRA_VERSION/sym-windows-x64.exe"

function Write-Step { param([string]$msg) Write-Host "  $msg" -ForegroundColor Cyan }
function Write-Ok   { param([string]$msg) Write-Host "  [ok] $msg" -ForegroundColor Green }
function Write-Warn { param([string]$msg) Write-Host "  [!]  $msg" -ForegroundColor Yellow }
function Write-Fail { param([string]$msg) Write-Host "  [x]  $msg" -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "  Symmetra $SYMMETRA_VERSION installer" -ForegroundColor White
Write-Host "  TypeScript for the GPU" -ForegroundColor DarkGray
Write-Host ""

# ── 1. Check Windows version ────────────────────────────────────────────────
Write-Step "Checking Windows version..."
$build = [System.Environment]::OSVersion.Version.Build
if ($build -lt 19041) {
    Write-Fail "Windows 10 version 2004 (build 19041) or later required. You have build $build."
}
Write-Ok "Windows build $build"

# ── 2. Check for NVIDIA GPU ─────────────────────────────────────────────────
Write-Step "Checking for NVIDIA GPU..."
try {
    $gpus = Get-WmiObject Win32_VideoController | Where-Object { $_.Name -like "*NVIDIA*" }
    if (-not $gpus) { throw "no NVIDIA GPU found" }
    $gpu = $gpus | Select-Object -First 1
    Write-Ok "Found: $($gpu.Name)"
} catch {
    Write-Warn "Could not detect NVIDIA GPU: $_"
    Write-Warn "Symmetra requires an NVIDIA GPU (RTX 2070 or newer, sm_75+)"
}

# ── 3. Check CUDA driver ─────────────────────────────────────────────────────
Write-Step "Checking CUDA driver..."
$nvidiaSmi = Get-Command "nvidia-smi.exe" -ErrorAction SilentlyContinue
if ($nvidiaSmi) {
    $smiOut = & nvidia-smi --query-gpu=driver_version,compute_cap --format=csv,noheader 2>$null | Select-Object -First 1
    if ($smiOut) {
        Write-Ok "CUDA driver: $($smiOut.Trim())"
    }
} else {
    Write-Warn "nvidia-smi not found — CUDA driver may not be installed."
    Write-Warn "Download CUDA Toolkit 12.x from: https://developer.nvidia.com/cuda-downloads"
    Write-Warn "(Symmetra bundles NVRTC — only the driver is required at runtime)"
}

# ── 4. Download sym.exe ──────────────────────────────────────────────────────
Write-Step "Downloading sym.exe..."
if (-not (Test-Path $INSTALL_DIR)) {
    New-Item -ItemType Directory -Path $INSTALL_DIR -Force | Out-Null
}

$dest = Join-Path $INSTALL_DIR "sym.exe"
try {
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($BINARY_URL, $dest)
    Write-Ok "Downloaded to $dest"
} catch {
    Write-Fail "Download failed: $_`nCheck your internet connection and try again."
}

# ── 5. Add to PATH ───────────────────────────────────────────────────────────
Write-Step "Adding to PATH..."
$userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$INSTALL_DIR*") {
    [System.Environment]::SetEnvironmentVariable("PATH", "$userPath;$INSTALL_DIR", "User")
    Write-Ok "Added $INSTALL_DIR to user PATH"
    Write-Warn "Restart your terminal for PATH changes to take effect"
} else {
    Write-Ok "$INSTALL_DIR already in PATH"
}

# Update PATH in current session
$env:PATH = "$env:PATH;$INSTALL_DIR"

# ── 6. Verify ────────────────────────────────────────────────────────────────
Write-Step "Verifying installation..."
try {
    $ver = & "$dest" version 2>&1
    Write-Ok $ver
} catch {
    Write-Warn "Could not run sym.exe — try opening a new terminal and running 'sym doctor'"
}

Write-Host ""
Write-Host "  Symmetra installed successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host "    sym doctor          # verify GPU, CUDA driver"
Write-Host "    sym ./server.ts     # compile and run your first server"
Write-Host ""
Write-Host "  Docs: https://symmetra.dev/docs/getting-started"
Write-Host ""
