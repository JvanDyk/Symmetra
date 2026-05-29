#!/usr/bin/env pwsh
# Real performance testing for all Turing machine variants
# Measures actual HTTP throughput via load testing

$sym = "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe"
$variants = @(
    @{name = "lightning"; file = "releases/turing-machines/lightning/turing_lightning_server.ts"; port = 8001; duration = 30; workers = 16 }
    @{name = "hyper"; file = "releases/turing-machines/hyper/turing_hyper_server.ts"; port = 8002; duration = 30; workers = 16 }
    @{name = "extreme"; file = "releases/turing-machines/extreme/turing_extreme_server.ts"; port = 8003; duration = 30; workers = 16 }
    @{name = "ultra"; file = "releases/turing-machines/ultra/turing_ultra_server.ts"; port = 8004; duration = 30; workers = 16 }
    @{name = "balanced"; file = "releases/turing-machines/balanced/turing_balanced_server.ts"; port = 8005; duration = 30; workers = 16 }
    @{name = "superfast"; file = "releases/turing-machines/superfast/turing_superfast_server.ts"; port = 8006; duration = 30; workers = 16 }
    @{name = "simple"; file = "releases/turing-machines/simple/turing_simple_server.ts"; port = 8007; duration = 30; workers = 16 }
    @{name = "moderate"; file = "releases/turing-machines/moderate/turing_moderate_server.ts"; port = 8008; duration = 30; workers = 16 }
    @{name = "reference"; file = "releases/turing-machines/reference/turing_reference_server.ts"; port = 8009; duration = 30; workers = 16 }
)

$results = @()

foreach ($variant in $variants) {
    Write-Host "Testing $($variant.name)..." -ForegroundColor Cyan

    # Start server
    $proc = Start-Process $sym -ArgumentList "`"$($variant.file)`"", "--port", $variant.port -NoNewWindow -PassThru
    Start-Sleep -Seconds 2

    # Check if server is ready
    $ready = $false
    for ($i = 0; $i -lt 10; $i++) {
        try {
            $response = Invoke-WebRequest -Uri "http://localhost:$($variant.port)/" -TimeoutSec 1 -ErrorAction SilentlyContinue
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 500
        }
    }

    if (-not $ready) {
        Write-Host "  Failed to start server" -ForegroundColor Red
        Stop-Process -Id $proc.Id -Force
        continue
    }

    Write-Host "  Server running on port $($variant.port)" -ForegroundColor Green

    # Run load test
    Write-Host "  Running load test ($($variant.workers) workers, $($variant.duration)s)..."

    $startTime = Get-Date
    $requestCount = 0
    $successCount = 0
    $errorCount = 0
    $totalTime = 0

    # Simple load test: parallel requests
    $jobs = @()
    for ($w = 0; $w -lt $variant.workers; $w++) {
        $job = Start-Job -ScriptBlock {
            param($url, $duration, $workerId)
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            $count = 0
            while ($sw.Elapsed.TotalSeconds -lt $duration) {
                try {
                    $r = Invoke-WebRequest -Uri $url -TimeoutSec 5 -ErrorAction Stop
                    $count++
                } catch {
                    # Continue on error
                }
            }
            $sw.Stop()
            return $count
        } -ArgumentList "http://localhost:$($variant.port)/", $variant.duration, $w
        $jobs += $job
    }

    # Wait for all jobs
    $jobResults = $jobs | Wait-Job | Receive-Job
    $totalRequests = ($jobResults | Measure-Object -Sum).Sum
    $elapsedSeconds = $variant.duration
    $rps = [math]::Round($totalRequests / $elapsedSeconds, 2)

    Write-Host "  Results: $totalRequests requests in ${elapsedSeconds}s = $rps RPS" -ForegroundColor Yellow

    # Stop server
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500

    $results += @{
        variant = $variant.name
        rps = $rps
        totalRequests = $totalRequests
        duration = $elapsedSeconds
        workers = $variant.workers
    }
}

# Generate report
Write-Host "`n=== TURING MACHINE PERFORMANCE TEST RESULTS ===" -ForegroundColor Green
Write-Host "Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`n"

# Sort by RPS descending
$sorted = $results | Sort-Object -Property rps -Descending

Write-Host "Ranking by Throughput:"
Write-Host ""
foreach ($r in $sorted) {
    Write-Host "$($r.variant.PadRight(12)) : $($r.rps.ToString().PadLeft(10)) RPS ($($r.totalRequests) requests)"
}

# Save detailed results
$reportPath = "D:\GITHUB\SYMMETRA\TURING_BENCHMARK_RESULTS.md"
$report = @"
# Turing Machine Performance Test Results

**Date:** $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
**GPU:** RTX 5070
**CUDA Driver:** 13.2

## Measured Throughput (RPS)

| Variant | Throughput (RPS) | Total Requests | Duration | Workers |
|---------|-----------------|-----------------|----------|---------|
"@

foreach ($r in $sorted) {
    $report += "`n| $($r.variant) | $($r.rps) | $($r.totalRequests) | $($r.duration)s | $($r.workers) |"
}

$report += "`n`n## Analysis`n`nActual measured HTTP throughput via load testing with $($variants[0].workers) concurrent workers per variant.`n"

$report | Out-File -FilePath $reportPath -Encoding UTF8
Write-Host "`nDetailed results saved to: $reportPath"
