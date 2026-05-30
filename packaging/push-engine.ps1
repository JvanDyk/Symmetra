#!/usr/bin/env pwsh
# Push a single engine's Docker image to registry
# Usage: ./push-engine.ps1 -EngineName "rayk-ultra" -Version "1.0.0" -Registry "docker.io"

param(
    [Parameter(Mandatory = $true)]
    [string]$EngineName,

    [Parameter(Mandatory = $false)]
    [string]$Version = "1.0.0",

    [Parameter(Mandatory = $false)]
    [string]$Registry = "docker.io",

    [Parameter(Mandatory = $false)]
    [string]$Org = "symmetra",

    [Parameter(Mandatory = $false)]
    [switch]$PushLatest = $true,

    [Parameter(Mandatory = $false)]
    [switch]$DryRun = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$colors = @{
    Success = "Green"
    Error = "Red"
    Info = "Cyan"
    Warning = "Yellow"
}

function Write-Step { param([string]$msg) Write-Host "  →  $msg" -ForegroundColor $colors.Info }
function Write-Ok { param([string]$msg) Write-Host "  ✓  $msg" -ForegroundColor $colors.Success }
function Write-Fail { param([string]$msg) Write-Host "  ✗  $msg" -ForegroundColor $colors.Error; exit 1 }
function Write-Warn { param([string]$msg) Write-Host "  ⚠  $msg" -ForegroundColor $colors.Warning }

Write-Host ""
Write-Host "  📤 Pushing Engine: $EngineName v$Version" -ForegroundColor Magenta
Write-Host ""

# Validate Docker
Write-Step "Checking Docker..."
try {
    $dockerVersion = docker --version
    Write-Ok "Docker available: $dockerVersion"
}
catch {
    Write-Fail "Docker not found. Please install Docker Desktop."
}

# Build tags
$localTag = "symmetra:tm-$($EngineName.ToLower()):$Version"
$registryTag = "$Registry/$Org/tm-$($EngineName.ToLower()):$Version"
$latestTag = "$Registry/$Org/tm-$($EngineName.ToLower()):latest"

Write-Host ""
Write-Step "Image tags:"
Write-Host "     Local: $localTag" -ForegroundColor DarkGray
Write-Host "     Registry: $registryTag" -ForegroundColor DarkGray
if ($PushLatest) { Write-Host "     Latest: $latestTag" -ForegroundColor DarkGray }
Write-Host ""

# Check if image exists
Write-Step "Checking if image exists..."
try {
    $imageExists = docker image inspect $localTag 2>$null
    Write-Ok "Image found: $localTag"
}
catch {
    Write-Fail "Image not found: $localTag. Build it first with build-engine.ps1"
}

# Tag for registry
Write-Step "Tagging image for registry..."
if ($DryRun) {
    Write-Host "  [DRY-RUN] docker tag $localTag $registryTag" -ForegroundColor DarkGray
} else {
    try {
        docker tag $localTag $registryTag
        Write-Ok "Tagged: $registryTag"
    }
    catch {
        Write-Fail "Failed to tag image: $_"
    }
}

# Tag as latest
if ($PushLatest) {
    Write-Step "Tagging as latest..."
    if ($DryRun) {
        Write-Host "  [DRY-RUN] docker tag $localTag $latestTag" -ForegroundColor DarkGray
    } else {
        try {
            docker tag $localTag $latestTag
            Write-Ok "Tagged: $latestTag"
        }
        catch {
            Write-Fail "Failed to tag latest: $_"
        }
    }
}

# Push to registry
Write-Host ""
Write-Step "Pushing to registry ($Registry)..."
if ($DryRun) {
    Write-Host "  [DRY-RUN] docker push $registryTag" -ForegroundColor DarkGray
    if ($PushLatest) {
        Write-Host "  [DRY-RUN] docker push $latestTag" -ForegroundColor DarkGray
    }
} else {
    try {
        Write-Host "     (This may take a minute...)" -ForegroundColor DarkGray
        docker push $registryTag
        Write-Ok "Pushed: $registryTag"
    }
    catch {
        Write-Fail "Failed to push image: $_"
    }

    if ($PushLatest) {
        try {
            docker push $latestTag
            Write-Ok "Pushed: $latestTag"
        }
        catch {
            Write-Fail "Failed to push latest tag: $_"
        }
    }
}

# Summary
Write-Host ""
Write-Host "  ✅ Push Complete!" -ForegroundColor Green
Write-Host ""
Write-Host "  📦 Images pushed:" -ForegroundColor Green
Write-Host "     - $registryTag" -ForegroundColor Green
if ($PushLatest) { Write-Host "     - $latestTag" -ForegroundColor Green }
Write-Host ""
Write-Host "  Registry URL:" -ForegroundColor Cyan
Write-Host "     $Registry/$Org/tm-$($EngineName.ToLower())" -ForegroundColor Cyan
Write-Host ""

if ($DryRun) {
    Write-Warn "This was a DRY-RUN. No images were actually pushed."
}
