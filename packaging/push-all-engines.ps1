#!/usr/bin/env pwsh
# Push all built engines' Docker images to registry
# Usage: ./push-all-engines.ps1 -Version "1.0.0"

param(
    [Parameter(Mandatory = $false)]
    [string]$Version = "1.0.0",

    [Parameter(Mandatory = $false)]
    [string]$Registry = "docker.io",

    [Parameter(Mandatory = $false)]
    [string]$Org = "symmetra",

    [Parameter(Mandatory = $false)]
    [switch]$Parallel = $false,

    [Parameter(Mandatory = $false)]
    [int]$MaxParallel = 4,

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
    Section = "Magenta"
}

function Write-Header { param([string]$msg) Write-Host "  📤 $msg" -ForegroundColor $colors.Section }
function Write-Step { param([string]$msg) Write-Host "     →  $msg" -ForegroundColor $colors.Info }
function Write-Ok { param([string]$msg) Write-Host "     ✓  $msg" -ForegroundColor $colors.Success }
function Write-Fail { param([string]$msg) Write-Host "     ✗  $msg" -ForegroundColor $colors.Error }

Write-Host ""
Write-Host "  📤 Pushing ALL Engines to Registry v$Version" -ForegroundColor Magenta
Write-Host ""

# Discover all built engines
Write-Header "Discovering engines..."
$buildDirs = Get-ChildItem -Path "build/engines" -Directory -ErrorAction SilentlyContinue
if (-not $buildDirs) {
    Write-Fail "No built engines found in build/engines/"
}

Write-Ok "Found $($buildDirs.Count) engines:"
$buildDirs | ForEach-Object { Write-Host "     - $($_.Name)" -ForegroundColor DarkGray }
Write-Host ""

# Push engines
Write-Header "Pushing engines..."
Write-Step "Mode: $(if ($Parallel) { 'Parallel (' + $MaxParallel + ' workers)' } else { 'Sequential' })"
if ($DryRun) { Write-Step "Mode: DRY-RUN (no actual pushes)" }
Write-Host ""

$results = @{
    Success = 0
    Failed = 0
}

$failedEngines = @()
$startTime = Get-Date

if ($Parallel) {
    $buildDirs | ForEach-Object -ThrottleLimit $MaxParallel -Parallel {
        $engine = $_.Name
        $version = $using:Version
        $registry = $using:Registry
        $org = $using:Org
        $dryRun = $using:DryRun

        Write-Host "  ⏱️  Pushing: $engine"

        try {
            & "$PSScriptRoot\push-engine.ps1" `
                -EngineName $engine `
                -Version $version `
                -Registry $registry `
                -Org $org `
                -DryRun:$dryRun

            Write-Host "  ✓  $engine pushed"
        }
        catch {
            Write-Host "  ✗  $engine failed: $_"
        }
    }
}
else {
    foreach ($dir in $buildDirs) {
        $engine = $dir.Name
        Write-Host "  ⏱️  Pushing: $engine ($(($buildDirs.IndexOf($dir) + 1)) of $($buildDirs.Count))"

        try {
            & "$PSScriptRoot\push-engine.ps1" `
                -EngineName $engine `
                -Version $Version `
                -Registry $Registry `
                -Org $Org `
                -DryRun:$DryRun

            $results.Success++
            Write-Ok "$engine pushed"
        }
        catch {
            $results.Failed++
            $failedEngines += $engine
            Write-Fail "$engine failed: $_"
        }

        Write-Host ""
    }
}

# Summary
$endTime = Get-Date
$duration = $endTime - $startTime

Write-Host ""
Write-Host "  ════════════════════════════════════════" -ForegroundColor Magenta
Write-Host "  📊 PUSH SUMMARY" -ForegroundColor Magenta
Write-Host "  ════════════════════════════════════════" -ForegroundColor Magenta
Write-Host ""
Write-Host "     Total Engines: $($buildDirs.Count)" -ForegroundColor White
Write-Host "     Successful: $($results.Success)" -ForegroundColor Green
Write-Host "     Failed: $($results.Failed)" -ForegroundColor Red
Write-Host "     Duration: $($duration.TotalMinutes.ToString('F1')) minutes" -ForegroundColor Cyan
Write-Host ""

if ($failedEngines.Count -gt 0) {
    Write-Host "     Failed Engines:" -ForegroundColor Red
    $failedEngines | ForEach-Object { Write-Host "       - $_" -ForegroundColor Red }
    Write-Host ""
}

Write-Host "  Registry Information:" -ForegroundColor Green
Write-Host "     Registry: $Registry" -ForegroundColor Cyan
Write-Host "     Organization: $Org" -ForegroundColor Cyan
Write-Host "     URL: https://hub.docker.com/r/$Org" -ForegroundColor Cyan
Write-Host ""

Write-Host "  Next steps:" -ForegroundColor Green
Write-Host "     1. Verify images in registry: docker search $Org/tm-*"
Write-Host "     2. Create release: ./create-release.ps1 -Version $Version"
Write-Host ""

if ($DryRun) {
    Write-Warn "This was a DRY-RUN. No images were actually pushed."
}

exit $(if ($results.Failed -gt 0) { 1 } else { 0 })
