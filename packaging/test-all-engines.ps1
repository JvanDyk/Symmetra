#!/usr/bin/env pwsh
# Test all built Turing Machine engines
# Usage: ./test-all-engines.ps1 -Version "1.0.0" -Parallel

param(
    [Parameter(Mandatory = $false)]
    [string]$Version = "1.0.0",

    [Parameter(Mandatory = $false)]
    [switch]$Parallel = $false,

    [Parameter(Mandatory = $false)]
    [int]$MaxParallel = 4,

    [Parameter(Mandatory = $false)]
    [switch]$TestPerformance = $true,

    [Parameter(Mandatory = $false)]
    [switch]$TestCorrectness = $true,

    [Parameter(Mandatory = $false)]
    [switch]$TestDocker = $false
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

function Write-Header { param([string]$msg) Write-Host "  🧪 $msg" -ForegroundColor $colors.Section }
function Write-Step { param([string]$msg) Write-Host "     →  $msg" -ForegroundColor $colors.Info }
function Write-Ok { param([string]$msg) Write-Host "     ✓  $msg" -ForegroundColor $colors.Success }
function Write-Fail { param([string]$msg) Write-Host "     ✗  $msg" -ForegroundColor $colors.Error }

Write-Host ""
Write-Host "  🧪 Testing ALL Engines v$Version" -ForegroundColor Magenta
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

# Run tests
Write-Header "Running tests..."
Write-Step "Mode: $(if ($Parallel) { 'Parallel (' + $MaxParallel + ' workers)' } else { 'Sequential' })"
Write-Host ""

$results = @{
    Passed = 0
    Failed = 0
    Skipped = 0
}

$failedEngines = @()
$startTime = Get-Date

if ($Parallel) {
    $buildDirs | ForEach-Object -ThrottleLimit $MaxParallel -Parallel {
        $engine = $_.Name
        $version = $using:Version
        $testPerf = $using:TestPerformance
        $testCorr = $using:TestCorrectness
        $testDocker = $using:TestDocker

        Write-Host "  ⏱️  Testing: $engine"

        try {
            & "$PSScriptRoot\test-engine.ps1" `
                -EngineName $engine `
                -Version $version `
                -TestPerformance:$testPerf `
                -TestCorrectness:$testCorr `
                -TestDocker:$testDocker

            Write-Host "  ✓  $engine passed"
        }
        catch {
            Write-Host "  ✗  $engine failed: $_"
        }
    }
}
else {
    foreach ($dir in $buildDirs) {
        $engine = $dir.Name
        Write-Host "  ⏱️  Testing: $engine ($(($buildDirs.IndexOf($dir) + 1)) of $($buildDirs.Count))"

        try {
            & "$PSScriptRoot\test-engine.ps1" `
                -EngineName $engine `
                -Version $Version `
                -TestPerformance:$TestPerformance `
                -TestCorrectness:$TestCorrectness `
                -TestDocker:$TestDocker

            $results.Passed++
            Write-Ok "$engine passed"
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
Write-Host "  📊 TEST SUMMARY" -ForegroundColor Magenta
Write-Host "  ════════════════════════════════════════" -ForegroundColor Magenta
Write-Host ""
Write-Host "     Total Engines: $($buildDirs.Count)" -ForegroundColor White
Write-Host "     Passed: $($results.Passed)" -ForegroundColor Green
Write-Host "     Failed: $($results.Failed)" -ForegroundColor Red
Write-Host "     Duration: $($duration.TotalMinutes.ToString('F1')) minutes" -ForegroundColor Cyan
Write-Host ""

if ($failedEngines.Count -gt 0) {
    Write-Host "     Failed Engines:" -ForegroundColor Red
    $failedEngines | ForEach-Object { Write-Host "       - $_" -ForegroundColor Red }
    Write-Host ""
}

Write-Host "  Next steps:" -ForegroundColor Green
Write-Host "     1. Review failed engines (if any)"
Write-Host "     2. Push to registry: ./push-all-engines.ps1 -Version $Version"
Write-Host "     3. Create release: ./create-release.ps1 -Version $Version"
Write-Host ""

exit $(if ($results.Failed -gt 0) { 1 } else { 0 })
