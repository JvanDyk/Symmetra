#!/usr/bin/env pwsh
# Lightning Load Test - Measure peak RPS on GPU
# Stress tests the async callback batch processing architecture
# Target: Verify 100K+ RPS sustained throughput

param(
    [int]$Port = 9000,
    [int]$DurationSeconds = 60,
    [int]$ParallelWorkers = 32,
    [string]$Endpoint = "http://localhost:$Port/"
)

$ErrorActionPreference = "Stop"

class LoadTestMetrics {
    [int]$TotalRequests = 0
    [int]$SuccessCount = 0
    [int]$FailureCount = 0
    [double]$TotalTimeSeconds = 0
    [double]$ThroughputRps = 0
    [double]$AvgLatencyMs = 0
    [double]$MinLatencyMs = [double]::MaxValue
    [double]$MaxLatencyMs = 0
    [double]$P50LatencyMs = 0
    [double]$P95LatencyMs = 0
    [double]$P99LatencyMs = 0
    [System.Collections.Generic.List[double]]$LatenciesMsList = @()
}

function Send-Request {
    param(
        [string]$Url,
        [System.Collections.Generic.List[double]]$Latencies
    )

    $startTime = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $response = Invoke-WebRequest -Uri $Url -Method GET -TimeoutSec 10 -UseBasicParsing
        $startTime.Stop()
        $latencyMs = $startTime.Elapsed.TotalMilliseconds
        $Latencies.Add($latencyMs)
        return @{ Success = $true; Latency = $latencyMs }
    }
    catch {
        $startTime.Stop()
        return @{ Success = $false; Latency = 0; Error = $_.Exception.Message }
    }
}

function Calculate-Statistics {
    param([System.Collections.Generic.List[double]]$Latencies)

    if ($Latencies.Count -eq 0) { return $null }

    $sorted = $Latencies | Sort-Object
    $min = $sorted[0]
    $max = $sorted[-1]
    $avg = ($Latencies | Measure-Object -Sum).Sum / $Latencies.Count
    $p50Idx = [int]($Latencies.Count * 0.50)
    $p95Idx = [int]($Latencies.Count * 0.95)
    $p99Idx = [int]($Latencies.Count * 0.99)

    return @{
        Min = $min
        Max = $max
        Avg = $avg
        P50 = $sorted[$p50Idx]
        P95 = $sorted[$p95Idx]
        P99 = $sorted[$p99Idx]
    }
}

# Main test loop
Write-Host "`n╔════════════════════════════════════════════════════════════════════════╗"
Write-Host "║  Lightning Load Test - GPU Max Throughput                              ║"
Write-Host "║  Endpoint: $Endpoint"
Write-Host "║  Duration: $DurationSeconds seconds                                         ║"
Write-Host "║  Parallel Workers: $ParallelWorkers                                         ║"
Write-Host "╚════════════════════════════════════════════════════════════════════════╝`n"

# Warmup phase
Write-Host "🔥 Warmup (5 seconds)..." -ForegroundColor Yellow
$warmupStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$warmupLatencies = [System.Collections.Generic.List[double]]::new()
while ($warmupStopwatch.Elapsed.TotalSeconds -lt 5) {
    Send-Request -Url $Endpoint -Latencies $warmupLatencies | Out-Null
}
$warmupStopwatch.Stop()
Write-Host "  Warmup complete - Cache primed`n"

# Load test phase
Write-Host "⚡ Running load test ($DurationSeconds seconds)..." -ForegroundColor Cyan
$testStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$testLatencies = [System.Collections.Generic.List[double]]::new()
$successCount = 0
$failureCount = 0
$requestCount = 0

# Parallel requests using jobs
$jobCount = 0
$jobs = @()

while ($testStopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
    # Launch worker jobs
    while ($jobs.Count -lt $ParallelWorkers -and $testStopwatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $job = Start-Job -ScriptBlock {
            param($Url, $Endpoint)
            $startTime = [System.Diagnostics.Stopwatch]::StartNew()
            try {
                $response = Invoke-WebRequest -Uri $Endpoint -Method GET -TimeoutSec 10 -UseBasicParsing
                $startTime.Stop()
                return @{ Success = $true; Latency = $startTime.Elapsed.TotalMilliseconds }
            }
            catch {
                $startTime.Stop()
                return @{ Success = $false; Latency = $startTime.Elapsed.TotalMilliseconds; Error = $_.Exception.Message }
            }
        } -ArgumentList $jobCount, $Endpoint

        $jobs += $job
        $jobCount++
    }

    # Collect completed jobs
    $completedJobs = $jobs | Where-Object { $_.State -eq "Completed" }
    foreach ($job in $completedJobs) {
        $result = Receive-Job -Job $job
        $testLatencies.Add($result.Latency)
        if ($result.Success) { $successCount++ } else { $failureCount++ }
        $requestCount++
        Remove-Job -Job $job
    }

    $jobs = $jobs | Where-Object { $_.State -ne "Completed" }
    Start-Sleep -Milliseconds 10
}

# Wait for remaining jobs
Get-Job | Wait-Job | Out-Null
$remainingJobs = Get-Job
foreach ($job in $remainingJobs) {
    $result = Receive-Job -Job $job
    $testLatencies.Add($result.Latency)
    if ($result.Success) { $successCount++ } else { $failureCount++ }
    $requestCount++
    Remove-Job -Job $job
}

$testStopwatch.Stop()

# Calculate metrics
$stats = Calculate-Statistics -Latencies $testLatencies
$throughputRps = $successCount / $testStopwatch.Elapsed.TotalSeconds

Write-Host "`n╔════════════════════════════════════════════════════════════════════════╗"
Write-Host "║  RESULTS                                                                ║"
Write-Host "╚════════════════════════════════════════════════════════════════════════╝`n"

Write-Host "Throughput:"
Write-Host "  ✓ Requests/sec (RPS):  $('{0:F0}' -f $throughputRps) RPS" -ForegroundColor Green
Write-Host "  ✓ Total requests:       $successCount successful, $failureCount failed" -ForegroundColor Green
Write-Host "  ✓ Total time:           $('{0:F2}' -f $testStopwatch.Elapsed.TotalSeconds) seconds`n"

Write-Host "Latency (milliseconds):"
Write-Host "  • Minimum:              $('{0:F4}' -f $stats.Min) ms"
Write-Host "  • Average:              $('{0:F4}' -f $stats.Avg) ms"
Write-Host "  • P50 (median):         $('{0:F4}' -f $stats.P50) ms"
Write-Host "  • P95:                  $('{0:F4}' -f $stats.P95) ms"
Write-Host "  • P99:                  $('{0:F4}' -f $stats.P99) ms"
Write-Host "  • Maximum:              $('{0:F4}' -f $stats.Max) ms`n"

# Compare to expected
Write-Host "Performance vs Expected:"
if ($throughputRps -ge 100000) {
    Write-Host "  ✅ EXCELLENT: $([int]$throughputRps) RPS (target: 100K+ RPS)" -ForegroundColor Green
} elseif ($throughputRps -ge 50000) {
    Write-Host "  ⚠️  GOOD: $([int]$throughputRps) RPS (target: 100K+ RPS)" -ForegroundColor Yellow
} else {
    Write-Host "  ❌ BELOW TARGET: $([int]$throughputRps) RPS (target: 100K+ RPS)" -ForegroundColor Red
}

Write-Host "`nHistorical Baseline (RTX 5070, pre-async):"
Write-Host "  • Sustained: 711K RPS"
Write-Host "  • Peak burst: 3.2M RPS"
Write-Host "  • Expected with async: 100K-300K RPS (with new batching)\n"

Write-Host "╔════════════════════════════════════════════════════════════════════════╗`n"
