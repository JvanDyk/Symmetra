#!/usr/bin/env pwsh
# Windows ZIP package builder
# Creates: symmetra-{version}-windows-x64.zip
# Contents: sym.exe + install.ps1 + docs

param(
    [string]$SymExePath = "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe",
    [string]$OutputDir = "D:\GITHUB\SYMMETRA\dist",
    [string]$Version = "1.0.0"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step { Write-Host "  $args" -ForegroundColor Cyan }
function Write-Ok   { Write-Host "  [ok] $args" -ForegroundColor Green }
function Write-Fail { Write-Host "  [x]  $args" -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "  Symmetra Windows ZIP Builder v$Version" -ForegroundColor White
Write-Host ""

# Verify inputs
Write-Step "Checking inputs..."
if (-not (Test-Path $SymExePath)) {
    Write-Fail "sym.exe not found at: $SymExePath"
}
Write-Ok "sym.exe found"

# Create staging directory
$StagingDir = Join-Path $env:TEMP "symmetra-zip-$([guid]::NewGuid().ToString().Substring(0,8))"
Write-Step "Creating staging directory: $StagingDir"
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

try {
    # Copy main binary
    Write-Step "Copying sym.exe..."
    Copy-Item $SymExePath (Join-Path $StagingDir "sym.exe")
    Write-Ok "Copied"

    # Copy install script
    Write-Step "Copying install.ps1..."
    $ScriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $ScriptRoot)
    $InstallPs1 = Join-Path $RepoRoot "public\install.ps1"
    if (-not (Test-Path $InstallPs1)) {
        # Fallback: look relative to CWD
        $InstallPs1 = "public\install.ps1"
    }
    Copy-Item $InstallPs1 (Join-Path $StagingDir "install.ps1")
    Write-Ok "Copied"

    # Copy README
    Write-Step "Copying documentation..."
    $readme = @"
# Symmetra $Version — Windows x64

## Quick Start

1. Extract this ZIP to a location (e.g., `C:\Program Files\Symmetra`)
2. Add the folder to your PATH or run:
   ```
   .\install.ps1
   ```
3. Open a new PowerShell window and verify:
   ```
   sym version
   sym doctor
   ```

## Installation Methods

### Method 1: PowerShell Script (Recommended)
```powershell
.\install.ps1
```
This adds Symmetra to your PATH and configures your environment.

### Method 2: Manual
- Copy `sym.exe` to any location (e.g., `C:\Program Files\Symmetra\`)
- Add that folder to your PATH environment variable
- Restart your terminal

### Method 3: Use Windows Installer (.msi)
Download the `.msi` version for automatic installation with Start Menu shortcuts.

## Requirements

- **Windows 10 or 11** (build 19041+)
- **NVIDIA GPU**: RTX 2070 or newer (sm_75+)
- **NVIDIA Driver**: Version 525 or later
- **Visual C++ Redistributable** (x64): https://aka.ms/vs/17/release/vc_redist.x64.exe

## Verify Installation

After installation, open a new PowerShell window:

```powershell
sym version
# Output: Symmetra 1.0.0 · Windows · RTX 4090 sm_89

sym doctor
# Checks GPU, driver, and dependencies
```

## Next Steps

- **Getting started guide**: https://symmetra.dev/docs/getting-started
- **Create your first server**: https://symmetra.dev/docs/examples
- **API documentation**: https://symmetra.dev/docs/api

## Support

- **Issues**: https://github.com/JvanDyk/symmetra/issues
- **Discussions**: https://github.com/JvanDyk/symmetra/discussions

---

© 2026 Symmetra. Licensed under MIT.
"@
    $readme | Out-File -FilePath (Join-Path $StagingDir "README.md") -Encoding UTF8
    Write-Ok "Created README.md"

    # Create the ZIP
    Write-Step "Creating ZIP package..."
    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }

    $ZipPath = Join-Path $OutputDir "symmetra-$Version-windows-x64.zip"
    Compress-Archive -Path $StagingDir -DestinationPath $ZipPath -Force
    Write-Ok "Created: $ZipPath"

    # Calculate SHA256
    Write-Step "Calculating SHA256 hash..."
    $hash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash
    Write-Ok "SHA256: $hash"

    # Save hash to file
    "$hash  symmetra-$Version-windows-x64.zip" | Out-File -FilePath (Join-Path $OutputDir "symmetra-$Version-windows-x64.zip.sha256") -Encoding UTF8
    Write-Ok "Hash saved to .sha256 file"

    Write-Host ""
    Write-Host "  ✓ Windows package created successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Package: $(Split-Path -Leaf $ZipPath)" -ForegroundColor White
    Write-Host "  Size: $([math]::Round((Get-Item $ZipPath).Length / 1MB, 2)) MB" -ForegroundColor Gray
    Write-Host "  SHA256: $hash" -ForegroundColor Gray
    Write-Host ""

} finally {
    # Clean up staging directory
    if (Test-Path $StagingDir) {
        Write-Step "Cleaning up..."
        Remove-Item $StagingDir -Recurse -Force
    }
}
