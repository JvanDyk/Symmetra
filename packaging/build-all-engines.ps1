#!/usr/bin/env pwsh
# Build all Turing Machine engines
# Usage: ./build-all-engines.ps1 -Version "1.0.0" -Parallel

param(
    [Parameter(Mandatory = $false)]
    [string]$Version = "1.0.0",

    [Parameter(Mandatory = $false)]
    [switch]$Parallel = $false,

    [Parameter(Mandatory = $false)]
    [int]$MaxParallel = 4,

    [Parameter(Mandatory = $false)]
    [switch]$BuildDocker = $true,

    [Parameter(Mandatory = $false)]
    [switch]$BuildZip = $true,

    [Parameter(Mandatory = $false)]
    [switch]$SkipFailures = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Colors for output
$colors = @{
    Success = "Green"
    Error = "Red"
    Info = "Cyan"
    Warning = "Yellow"
    Section = "Magenta"
}

function Write-Header { param([string]$msg) Write-Host "  🔨 $msg" -ForegroundColor $colors.Section }
function Write-Step { param([string]$msg) Write-Host "     →  $msg" -ForegroundColor $colors.Info }
function Write-Ok { param([string]$msg) Write-Host "     ✓  $msg" -ForegroundColor $colors.Success }
function Write-Fail { param([string]$msg) Write-Host "     ✗  $msg" -ForegroundColor $colors.Error }
function Write-Warn { param([string]$msg) Write-Host "     ⚠  $msg" -ForegroundColor $colors.Warning }

Write-Host ""
Write-Host "  🚀 Building ALL Symmetra Engines v$Version" -ForegroundColor Magenta
Write-Host ""

# 1. Discover all engines
Write-Header "Discovering engines..."
$engineDirs = Get-ChildItem -Path "releases/turing-machines" -Directory -ErrorAction SilentlyContinue
$engines = @()

foreach ($dir in $engineDirs) {
    $tsFiles = Get-ChildItem -Path $dir.FullName -Filter "turing_*.ts"
    if ($tsFiles) {
        $engines += $dir.Name
    }
}

if ($engines.Count -eq 0) {
    Write-Fail "No engines found in releases/turing-machines/"
}

Write-Ok "Found $($engines.Count) engines:"
$engines | ForEach-Object { Write-Host "     - $_" -ForegroundColor DarkGray }
Write-Host ""

# 2. Build engines
Write-Header "Building engines..."
Write-Step "Mode: $(if ($Parallel) { 'Parallel (' + $MaxParallel + ' workers)' } else { 'Sequential' })"
Write-Host ""

$results = @{
    Success = 0
    Failed = 0
    Warnings = 0
}

$failedEngines = @()
$startTime = Get-Date

if ($Parallel) {
    # Parallel build using ForEach-Object -Parallel
    $engines | ForEach-Object -ThrottleLimit $MaxParallel -Parallel {
        $engine = $_
        $version = $using:Version
        $buildDocker = $using:BuildDocker
        $buildZip = $using:BuildZip

        Write-Host "  ⏱️  Building: $engine" -ForegroundColor Cyan

        try {
            & "$PSScriptRoot\build-engine.ps1" `
                -EngineName $engine `
                -Version $version `
                -BuildDocker:$buildDocker `
                -BuildZip:$buildZip

            Write-Host "  ✓  $engine completed" -ForegroundColor Green
        }
        catch {
            Write-Host "  ✗  $engine failed: $_" -ForegroundColor Red
            if (-not $using:SkipFailures) {
                throw
            }
        }
    }
}
else {
    # Sequential build
    foreach ($engine in $engines) {
        Write-Host "  ⏱️  Building: $engine ($(($engines.IndexOf($engine) + 1)) of $($engines.Count))" -ForegroundColor Cyan

        try {
            & "$PSScriptRoot\build-engine.ps1" `
                -EngineName $engine `
                -Version $Version `
                -BuildDocker:$BuildDocker `
                -BuildZip:$BuildZip

            $results.Success++
            Write-Ok "$engine completed"
        }
        catch {
            $results.Failed++
            $failedEngines += $engine
            Write-Fail "$engine failed: $_"

            if (-not $SkipFailures) {
                throw
            }
        }

        Write-Host ""
    }
}

# 3. Summary
$endTime = Get-Date
$duration = $endTime - $startTime

Write-Host ""
Write-Host "  ════════════════════════════════════════" -ForegroundColor Magenta
Write-Host "  📊 BUILD SUMMARY" -ForegroundColor Magenta
Write-Host "  ════════════════════════════════════════" -ForegroundColor Magenta
Write-Host ""
Write-Host "     Total Engines: $($engines.Count)" -ForegroundColor White
Write-Host "     Successful: $($results.Success)" -ForegroundColor Green
Write-Host "     Failed: $($results.Failed)" -ForegroundColor Red
Write-Host "     Duration: $($duration.TotalMinutes.ToString('F1')) minutes" -ForegroundColor Cyan
Write-Host ""

if ($failedEngines.Count -gt 0) {
    Write-Host "     Failed Engines:" -ForegroundColor Red
    $failedEngines | ForEach-Object { Write-Host "       - $_" -ForegroundColor Red }
    Write-Host ""
}

# 4. Build artifacts summary
Write-Header "Artifacts generated:"
Write-Host ""

$buildDirs = Get-ChildItem -Path "build/engines" -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
if ($buildDirs) {
    Write-Host "     Build Directories: $($buildDirs.Count)" -ForegroundColor Cyan
    $buildDirs | ForEach-Object { Write-Host "       - $_" -ForegroundColor DarkGray }
}

$packages = Get-ChildItem -Path "release-packages" -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "symmetra-tm-*" } | Select-Object -ExpandProperty Name
if ($packages) {
    Write-Host "     Packages Created: $($packages.Count)" -ForegroundColor Cyan
    $packages | ForEach-Object { Write-Host "       - $_" -ForegroundColor DarkGray }
}

Write-Host ""
Write-Host "  Next steps:" -ForegroundColor Green
Write-Host "     1. Review build artifacts in: ./build/engines/"
Write-Host "     2. Run tests: ./test-all-engines.ps1 -Version $Version"
Write-Host "     3. Push to registry: ./push-all-engines.ps1 -Version $Version"
Write-Host "     4. Generate releases: ./create-release.ps1 -Version $Version"
Write-Host ""

# 5. Exit with appropriate code
if ($results.Failed -gt 0 -and -not $SkipFailures) {
    exit 1
}

exit 0
