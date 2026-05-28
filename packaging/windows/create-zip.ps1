#!/usr/bin/env pwsh
# Creates Windows ZIP package for Symmetra
# Usage: .\create-zip.ps1 -Version 1.0.0 -BinaryPath .\sym.exe

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$BinaryPath,
    
    [string]$OutputDir = "."
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BinaryPath)) {
    Write-Host "❌ Binary not found: $BinaryPath" -ForegroundColor Red
    exit 1
}

Write-Host "📦 Creating Windows ZIP package v$Version" -ForegroundColor Cyan

# Create temp directory
$tempDir = Join-Path $env:TEMP "symmetra-package-$([System.Guid]::NewGuid())"
New-Item -ItemType Directory -Path $tempDir | Out-Null

try {
    # Copy binary
    Copy-Item -Path $BinaryPath -Destination (Join-Path $tempDir "sym.exe") -Force
    
    # Copy install script
    if (Test-Path "public/install.ps1") {
        Copy-Item -Path "public/install.ps1" -Destination (Join-Path $tempDir "install.ps1") -Force
    }
    
    # Copy README
    if (Test-Path "README.md") {
        Copy-Item -Path "README.md" -Destination (Join-Path $tempDir "README.md") -Force
    }
    
    # Create ZIP
    $zipPath = Join-Path $OutputDir "symmetra-v$Version-windows-x64.zip"
    Compress-Archive -Path $tempDir -DestinationPath $zipPath -Force
    
    $size = (Get-Item $zipPath).Length / 1MB
    Write-Host "✅ Created: $(Split-Path $zipPath -Leaf) ($([math]::Round($size, 1))MB)" -ForegroundColor Green
    
    # Calculate SHA256
    $hash = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash
    Write-Host "SHA256: $hash" -ForegroundColor Gray
    
    Write-Host ""
    Write-Host "✓ Package ready for release" -ForegroundColor Green
    
} finally {
    # Cleanup
    Remove-Item -Path $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
