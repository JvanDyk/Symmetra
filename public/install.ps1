#!/usr/bin/env pwsh
# Symmetra installer for Windows
# Usage: iwr https://symmetra.dev/install.ps1 | iex

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SYMMETRA_VERSION = "1.0.0"
$GITHUB_REPO      = "JvanDyk/symmetra"
$INSTALL_DIR      = "$env:USERPROFILE\.symmetra\bin"
$BINARY_URL       = "https://github.com/$GITHUB_REPO/releases/download/v$SYMMETRA_VERSION/sym-windows-x64.exe"

function Write-Step { param([string]$m) Write-Host "  →  $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "  ✓  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "  ⚠  $m" -ForegroundColor Yellow }
function Write-Fail { param([string]$m) Write-Host "  ✗  $m" -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "  Symmetra $SYMMETRA_VERSION  ·  TypeScript for the GPU" -ForegroundColor White
Write-Host ""

# ── 1. Windows version ─────────────────────────────────────────────────────────
Write-Step "Checking Windows version..."
$build = [System.Environment]::OSVersion.Version.Build
if ($build -lt 19041) { Write-Fail "Windows 10 build 19041+ required. Found: $build" }
Write-Ok "Windows build $build"

# ── 2. NVIDIA GPU ──────────────────────────────────────────────────────────────
Write-Step "Checking NVIDIA GPU..."
try {
    $gpu = Get-WmiObject Win32_VideoController | Where-Object { $_.Name -like "*NVIDIA*" } | Select-Object -First 1
    if (-not $gpu) { throw "no NVIDIA GPU" }
    Write-Ok $gpu.Name
} catch {
    Write-Warn "NVIDIA GPU not detected: $_"
    Write-Warn "Symmetra requires an NVIDIA GPU (RTX 2070 or newer, sm_75+)"
}

# ── 3. CUDA driver ─────────────────────────────────────────────────────────────
Write-Step "Checking CUDA driver..."
$nvSmi = Get-Command "nvidia-smi.exe" -ErrorAction SilentlyContinue
if ($nvSmi) {
    try {
        $info = & nvidia-smi --query-gpu=driver_version,compute_cap --format=csv,noheader 2>$null | Select-Object -First 1
        if ($info) { Write-Ok "Driver $($info.Trim())" }
    } catch { Write-Warn "Could not read driver version from nvidia-smi" }
} else {
    Write-Warn "nvidia-smi not found — driver 525+ required"
    Write-Warn "Download from: https://developer.nvidia.com/cuda-downloads"
}

# ── 4. Download ────────────────────────────────────────────────────────────────
Write-Step "Downloading sym.exe..."
if (-not (Test-Path $INSTALL_DIR)) {
    New-Item -ItemType Directory -Path $INSTALL_DIR -Force | Out-Null
}

$dest = Join-Path $INSTALL_DIR "sym.exe"
$downloaded = $false

try {
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($BINARY_URL, $dest)
    if ((Get-Item $dest -ErrorAction SilentlyContinue).Length -gt 0) {
        $downloaded = $true
        Write-Ok "Downloaded to $dest"
    }
} catch {
    # fall through to friendly error
}

if (-not $downloaded) {
    if (Test-Path $dest) { Remove-Item $dest -Force }
    Write-Host ""
    Write-Warn "Binary not yet available for automatic download."
    Write-Warn "The v$SYMMETRA_VERSION release has not been published to GitHub yet."
    Write-Host ""
    Write-Host "  Manual install:" -ForegroundColor White
    Write-Host "    1. Download the binary from:"
    Write-Host "       https://github.com/$GITHUB_REPO/releases"
    Write-Host "    2. Place sym.exe in: $INSTALL_DIR"
    Write-Host ""
    Write-Host "  Docs: https://symmetra.dev/docs/getting-started"
    Write-Host ""
    exit 1
}

# ── 5. PATH ────────────────────────────────────────────────────────────────────
Write-Step "Configuring PATH..."
$userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$INSTALL_DIR*") {
    [System.Environment]::SetEnvironmentVariable("PATH", "$userPath;$INSTALL_DIR", "User")
    Write-Ok "Added $INSTALL_DIR to user PATH"
    Write-Warn "Restart your terminal for PATH to take effect"
} else {
    Write-Ok "$INSTALL_DIR already in PATH"
}
$env:PATH = "$env:PATH;$INSTALL_DIR"

# ── 6. Verify ──────────────────────────────────────────────────────────────────
Write-Step "Verifying..."
try {
    $ver = & "$dest" version 2>&1
    Write-Ok $ver
} catch {
    Write-Warn "Could not run sym.exe — open a new terminal and run: sym doctor"
}

Write-Host ""
Write-Host "  Symmetra installed!" -ForegroundColor Green
Write-Host ""
Write-Host "  sym doctor          # verify GPU + CUDA driver"
Write-Host "  sym ./server.ts     # compile and launch"
Write-Host "  sym check           # audit package.json compatibility"
Write-Host ""
Write-Host "  Docs: https://symmetra.dev/docs/getting-started"
Write-Host ""
