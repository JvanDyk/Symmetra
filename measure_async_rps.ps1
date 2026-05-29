# Measure actual RPS improvement with AsyncBatchLauncher
# Uses native PowerShell for reliable concurrent HTTP requests

param(
    [int]$Workers = 64,
    [int]$DurationSec = 30
)

Write-Host "========================================"
Write-Host "ASYNC BATCH LAUNCHING - RPS MEASUREMENT"
Write-Host "Workers: $Workers"
Write-Host "Duration: $DurationSec seconds"
Write-Host "========================================"

# Start server
Write-Host ""
Write-Host "Starting GPU server with AsyncBatchLauncher..."
$serverProc = Start-Process -FilePath "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe" `
    -ArgumentList "D:\GITHUB\SYMMETRA\gpu_async_load_test.ts" `
    -NoNewWindow -PassThru -RedirectStandardOutput "NUL" `
    -RedirectStandardError "D:\GITHUB\SYMMETRA\async_perf.log"

Start-Sleep -Seconds 4

# Wait for server to be responsive
$ready = $false
for ($i = 0; $i -lt 20; $i++) {
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:8010/" -TimeoutSec 2 -ErrorAction Stop
        if ($response.StatusCode -eq 200) {
            Write-Host "Server ready on port 8010"
            $ready = $true
            break
        }
    } catch {
        Write-Host "Waiting for server..."
        Start-Sleep -Seconds 1
    }
}

if (-not $ready) {
    Write-Host "Server failed to start"
    Stop-Process -Id $serverProc.Id -Force
    exit 1
}

# Warm up
Write-Host ""
Write-Host "Warm-up (5 requests)..."
for ($i = 0; $i -lt 5; $i++) {
    try {
        Invoke-WebRequest -Uri "http://localhost:8010/" -TimeoutSec 5 -ErrorAction Stop | Out-Null
    } catch { }
}

Write-Host "Running concurrent load test..."

# Measure throughput with multiple concurrent workers
$startTime = [System.Diagnostics.Stopwatch]::StartNew()
$deadline = $startTime.AddSeconds($DurationSec)

# Launch worker threads
$workers = @()
for ($w = 0; $w -lt $Workers; $w++) {
    $job = Start-Job -ScriptBlock {
        param($id, $deadline)
        $count = 0
        $http = New-Object System.Net.Http.HttpClient
        $http.Timeout = [System.TimeSpan]::FromSeconds(10)

        try {
            while ([System.Diagnostics.Stopwatch]::GetTimestamp() -lt $deadline.Ticks) {
                try {
                    $response = $http.GetAsync("http://localhost:8010/").Result
                    if ($response.IsSuccessStatusCode) {
                        $count++
                    }
                    $response.Dispose()
                } catch {
                    # Request failed, continue
                }
            }
        } finally {
            $http.Dispose()
        }

        return $count
    } -ArgumentList $w, $deadline

    $workers += $job
}

# Wait for all workers to complete
Write-Host "Waiting for test to complete..."
$results = @()
foreach ($job in $workers) {
    $results += $job | Wait-Job | Receive-Job
}

$elapsed = $startTime.Elapsed.TotalSeconds
$totalRequests = ($results | Measure-Object -Sum).Sum
$rps = [Math]::Round($totalRequests / $elapsed)
$avgLatency = [Math]::Round(($elapsed * 1000) / $totalRequests, 2)

Write-Host ""
Write-Host "========================================"
Write-Host "RESULTS"
Write-Host "========================================"
Write-Host ""
Write-Host "Throughput:"
Write-Host "  Total Requests: $totalRequests"
Write-Host "  Elapsed Time: $([Math]::Round($elapsed, 2)) seconds"
Write-Host "  RPS: $rps requests/sec"
Write-Host "  Avg Latency: $avgLatency ms"
Write-Host ""
Write-Host "Per-Worker Stats:"
Write-Host "  Min: $(($results | Measure-Object -Minimum).Minimum) req/worker"
Write-Host "  Max: $(($results | Measure-Object -Maximum).Maximum) req/worker"
Write-Host "  Avg: $([Math]::Round(($results | Measure-Object -Average).Average)) req/worker"
Write-Host ""
Write-Host "Performance Rating:"

if ($rps -gt 100000) {
    Write-Host "  EXCEPTIONAL: $rps RPS - Production ready!"
} elseif ($rps -gt 50000) {
    Write-Host "  EXCELLENT: $rps RPS - Strong performance"
} elseif ($rps -gt 10000) {
    Write-Host "  GOOD: $rps RPS - Functional but room for optimization"
} elseif ($rps -gt 1000) {
    Write-Host "  FAIR: $rps RPS - Needs optimization"
} else {
    Write-Host "  CRITICAL: $rps RPS - Severe bottleneck detected"
}

Write-Host ""
Write-Host "========================================"

Stop-Process -Id $serverProc.Id -Force
Start-Sleep -Seconds 1

Write-Host "Test complete"
Write-Host ""
Write-Host "Server logs: D:\GITHUB\SYMMETRA\async_perf.log"
