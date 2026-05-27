#!/usr/bin/env pwsh
# Windows MSI package builder
# Creates: symmetra-{version}-windows-x64.msi
# Requires: WiX Toolset v4+

param(
    [string]$SymExePath = "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe",
    [string]$WorkDir = "D:\GITHUB\SYMMETRA\packaging\windows",
    [string]$OutputDir = "D:\GITHUB\SYMMETRA\dist",
    [string]$Version = "1.0.0"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step { Write-Host "  $args" -ForegroundColor Cyan }
function Write-Ok   { Write-Host "  [ok] $args" -ForegroundColor Green }
function Write-Fail { Write-Host "  [x]  $args" -ForegroundColor Red; exit 1 }

Write-Host ""
Write-Host "  Symmetra Windows MSI Builder v$Version" -ForegroundColor White
Write-Host ""

# Check for WiX toolset
Write-Step "Checking for WiX Toolset..."
$WixPath = Get-Command "wix" -ErrorAction SilentlyContinue
if (-not $WixPath) {
    Write-Fail "WiX Toolset not found. Install from: https://github.com/wixtoolset/wix/releases"
}
Write-Ok "WiX found: $($WixPath.Source)"

# Verify inputs
Write-Step "Checking inputs..."
if (-not (Test-Path $SymExePath)) {
    Write-Fail "sym.exe not found at: $SymExePath"
}
Write-Ok "sym.exe found"

if (-not (Test-Path (Join-Path $WorkDir "symmetra.wxs"))) {
    Write-Fail "symmetra.wxs not found in: $WorkDir"
}
Write-Ok "symmetra.wxs found"

# Create temporary build directory
$BuildDir = Join-Path $env:TEMP "symmetra-msi-$([guid]::NewGuid().ToString().Substring(0,8))"
Write-Step "Creating build directory: $BuildDir"
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

try {
    # Copy files to build directory
    Write-Step "Staging files..."
    Copy-Item $SymExePath (Join-Path $BuildDir "sym.exe")
    Copy-Item (Join-Path $WorkDir "symmetra.wxs") (Join-Path $BuildDir "symmetra.wxs")
    Write-Ok "Files staged"

    # Create license file (RTF format stub)
    Write-Step "Creating license file..."
    $licenseRtf = @"
{\rtf1\ansi\ansicpg1252\cocoartf2\cuc
{\fonttbl{\f0\fswiss Helvetica;}}
{\colortbl;\red255\green255\blue255;}
{\*\expandedcolortbl;;}
\margl1440\margr1440\margtsxn1440\margtbxn1440\vieww11900\viewh8605\viewkind0
\pard\tx720\tx1440\pardirnatural\partightenfactor100

\f0\fs24 \cf0 MIT License\
\
Copyright (c) 2026 Symmetra Contributors\
\
Permission is hereby granted, free of charge, to any person obtaining a copy\
of this software and associated documentation files (the "Software"), to deal\
in the Software without restriction, including without limitation the rights\
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\
copies of the Software, and to permit persons to whom the Software is\
furnished to do so, subject to the following conditions:\
\
The above copyright notice and this permission notice shall be included in all\
copies or substantial portions of the Software.\
\
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.}
"@
    $licenseRtf | Out-File -FilePath (Join-Path $BuildDir "license.rtf") -Encoding ASCII
    Write-Ok "Created license.rtf"

    # Build MSI with WiX
    Write-Step "Building MSI with WiX..."
    Push-Location $BuildDir
    try {
        # Use wix build to create the MSI
        # Output goes to bin\Release\symmetra.msi by default
        & wix build `
            -out "symmetra-$Version-windows-x64.msi" `
            -arch x64 `
            symmetra.wxs

        if ($LASTEXITCODE -ne 0) {
            Write-Fail "WiX build failed with exit code $LASTEXITCODE"
        }
        Write-Ok "MSI built successfully"
    } finally {
        Pop-Location
    }

    # Copy MSI to output directory
    Write-Step "Copying to output directory..."
    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }

    $MsiPath = Join-Path $BuildDir "symmetra-$Version-windows-x64.msi"
    $OutputPath = Join-Path $OutputDir "symmetra-$Version-windows-x64.msi"
    Copy-Item $MsiPath $OutputPath -Force
    Write-Ok "Copied to: $OutputPath"

    # Calculate SHA256
    Write-Step "Calculating SHA256 hash..."
    $hash = (Get-FileHash -Path $OutputPath -Algorithm SHA256).Hash
    Write-Ok "SHA256: $hash"

    # Save hash to file
    "$hash  symmetra-$Version-windows-x64.msi" | Out-File -FilePath (Join-Path $OutputDir "symmetra-$Version-windows-x64.msi.sha256") -Encoding UTF8
    Write-Ok "Hash saved to .sha256 file"

    Write-Host ""
    Write-Host "  ✓ MSI package created successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Package: $(Split-Path -Leaf $OutputPath)" -ForegroundColor White
    Write-Host "  Size: $([math]::Round((Get-Item $OutputPath).Length / 1MB, 2)) MB" -ForegroundColor Gray
    Write-Host "  SHA256: $hash" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Installation directory: %USERPROFILE%\.symmetra\bin" -ForegroundColor Gray
    Write-Host "  PATH will be configured automatically" -ForegroundColor Gray
    Write-Host ""

} finally {
    # Clean up build directory
    if (Test-Path $BuildDir) {
        Write-Step "Cleaning up..."
        Remove-Item $BuildDir -Recurse -Force
    }
}
