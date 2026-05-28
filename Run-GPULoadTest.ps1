#!/usr/bin/env pwsh
# GPU Load Test - Simplified version
# Measures Symmetra engine throughput with async callbacks

param(
    [int]$Port = 9000,
    [int]$Duration = 60,
    [int]$Workers = 32
)

$ErrorActionPreference = "Continue"

Write-Host "`n╔════════════════════════════════════════════════════════════════════════╗"
Write-Host "║  GPU Load Test - Async Callback Performance                           ║"
Write-Host "║  Real GPU execution with async batch pipelining                        ║"
Write-Host "╚════════════════════════════════════════════════════════════════════════╝`n"

# Check for sym.exe
$symBinary = "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe"
if (-not (Test-Path $symBinary)) {
    Write-Host "❌ Error: sym.exe not found at $symBinary" -ForegroundColor Red
    Write-Host "Please build the engine first:" -ForegroundColor Yellow
    Write-Host "  cd D:\GITHUB\SYMMETRA\engine\build"
    Write-Host "  cmake --build . --config Release --parallel 8"
    exit 1
}

Write-Host "✓ Found Symmetra binary at $symBinary`n" -ForegroundColor Green

# Create test endpoint if needed
$testFile = "D:\GITHUB\SYMMETRA\gpu_bench_test.ts"
if (-not (Test-Path $testFile)) {
    Write-Host "📝 Creating test endpoint..." -ForegroundColor Cyan

    $tsCode = @'
// GPU Benchmark - Rule 60 XOR computation
export function compute(n: number): number {
    let acc = n;
    for (let i = 0; i < 256; i++) {
        const left = acc << 1;
        const right = acc >> 1;
        acc = acc ^ left ^ right;
    }
    return acc % 256;
}

export default async function handler(req: Request): Promise<Response> {
    try {
        let result = 0;
        for (let i = 0; i < 32; i++) {
            result += compute(i);
        }
        return new Response(
            JSON.stringify({ success: true, result, ts: Date.now() }),
            { status: 200, headers: { "Content-Type": "application/json" } }
        );
    } catch (e: unknown) {
        return new Response(
            JSON.stringify({ error: e instanceof Error ? e.message : String(e) }),
            { status: 500 }
        );
    }
}
'@

    Set-Content -Path $testFile -Value $tsCode -Encoding UTF8
    Write-Host "✓ Created: $testFile`n" -ForegroundColor Green
}

# Start server
Write-Host "🚀 Starting Symmetra GPU server on port $Port..." -ForegroundColor Cyan
$serverProcess = Start-Process -FilePath $symBinary -ArgumentList @("`"$testFile`"", "--port", $Port) `
    -WindowStyle Hidden -PassThru

# Wait for server startup
Start-Sleep -Seconds 3

# Check if server is running
$maxWait = 10
$waited = 0
$serverReady = $false

while ($waited -lt $maxWait) {
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:$Port/" -Method GET -TimeoutSec 2 -UseBasicParsing
        $serverReady = $true
        Write-Host "✓ Server ready on port $Port`n" -ForegroundColor Green
        break
    }
    catch {
        $waited++
        Start-Sleep -Milliseconds 500
    }
}

if (-not $serverReady) {
    Write-Host "❌ Server failed to start" -ForegroundColor Red
    Stop-Process -Id $serverProcess.Id -Force
    exit 1
}

# Run load test
Write-Host "⚡ Running $Duration second load test with $Workers workers...`n" -ForegroundColor Cyan

$successCount = 0
$failureCount = 0
$latencies = @()
$startTime = Get-Date

$jobs = @()
for ($i = 0; $i -lt $Workers; $i++) {
    $job = Start-Job -ScriptBlock {
        param($Url, $Duration)

        $localSuccess = 0
        $localFail = 0
        $localLatencies = @()
        $endTime = (Get-Date).AddSeconds($Duration)

        while ((Get-Date) -lt $endTime) {
            $reqStart = Get-Date
            try {
                $response = Invoke-WebRequest -Uri $Url -Method POST `
                    -Body (ConvertTo-Json @{ data = "test" }) `
                    -ContentType "application/json" `
                    -TimeoutSec 10 `
                    -UseBasicParsing

                if ($response.StatusCode -eq 200) {
                    $localSuccess++
                } else {
                    $localFail++
                }
            }
            catch {
                $localFail++
            }

            $reqEnd = Get-Date
            $latency = ($reqEnd - $reqStart).TotalMilliseconds
            $localLatencies += $latency
        }

        return @{
            Success = $localSuccess
            Fail = $localFail
            Latencies = $localLatencies
        }
    } -ArgumentList "http://localhost:$Port/", $Duration

    $jobs += $job
}

# Wait for jobs and collect results
$progressCheck = 0
while ($jobs | Where-Object { $_.State -eq "Running" }) {
    $progressCheck++
    $running = ($jobs | Where-Object { $_.State -eq "Running" }).Count
    $completed = ($jobs | Where-Object { $_.State -eq "Completed" }).Count

    Write-Host "`r  Progress: Running=$running, Completed=$completed, Elapsed=$progressCheck sec" -NoNewline
    Start-Sleep -Seconds 1
}

Write-Host "`n`n📊 Collecting results..." -ForegroundColor Cyan

foreach ($job in $jobs) {
    try {
        $result = Receive-Job -Job $job
        $successCount += $result.Success
        $failureCount += $result.Fail
        $latencies += $result.Latencies
    }
    catch {
        $failureCount++
    }
    Remove-Job -Job $job
}

$endTime = Get-Date
$elapsed = ($endTime - $startTime).TotalSeconds
$totalRequests = $successCount + $failureCount
$rps = $successCount / $elapsed
$successRate = ($successCount * 100) / $totalRequests

# Calculate latency stats
$latenciesSorted = $latencies | Sort-Object
$minLat = $latenciesSorted[0]
$maxLat = $latenciesSorted[-1]
$avgLat = ($latencies | Measure-Object -Average).Average
$p50 = $latenciesSorted[[int]($latenciesSorted.Count * 0.50)]
$p95 = $latenciesSorted[[int]($latenciesSorted.Count * 0.95)]
$p99 = $latenciesSorted[[int]($latenciesSorted.Count * 0.99)]

# Print results
Write-Host "`n╔════════════════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  GPU LOAD TEST RESULTS                                                  ║" -ForegroundColor Cyan
Write-Host "╚════════════════════════════════════════════════════════════════════════╝`n" -ForegroundColor Cyan

Write-Host "Throughput:" -ForegroundColor Green
Write-Host "  ✓ Requests/second:    $([int]$rps) RPS"
Write-Host "  ✓ Total requests:     $successCount successful, $failureCount failed"
Write-Host "  ✓ Success rate:       $([math]::Round($successRate, 1))%"
Write-Host "  ✓ Duration:           $([math]::Round($elapsed, 2)) seconds`n"

Write-Host "Latency (milliseconds):" -ForegroundColor Green
Write-Host "  • Minimum:            $([math]::Round($minLat, 4)) ms"
Write-Host "  • Average:            $([math]::Round($avgLat, 4)) ms"
Write-Host "  • P50 (median):       $([math]::Round($p50, 4)) ms"
Write-Host "  • P95:                $([math]::Round($p95, 4)) ms"
Write-Host "  • P99:                $([math]::Round($p99, 4)) ms"
Write-Host "  • Maximum:            $([math]::Round($maxLat, 4)) ms`n"

Write-Host "GPU Performance Analysis:" -ForegroundColor Green
if ($rps -ge 100000) {
    Write-Host "  ✅ EXCEPTIONAL: $([int]$rps) RPS" -ForegroundColor Green
} elseif ($rps -ge 50000) {
    Write-Host "  ✅ EXCELLENT: $([int]$rps) RPS" -ForegroundColor Green
} elseif ($rps -ge 10000) {
    Write-Host "  ✅ VERY GOOD: $([int]$rps) RPS" -ForegroundColor Green
} elseif ($rps -ge 5000) {
    Write-Host "  ⚠️  GOOD: $([int]$rps) RPS" -ForegroundColor Yellow
} elseif ($rps -ge 1000) {
    Write-Host "  ⚠️  FAIR: $([int]$rps) RPS (target: 100K+)" -ForegroundColor Yellow
} else {
    Write-Host "  ❌ NEEDS OPTIMIZATION: $([int]$rps) RPS" -ForegroundColor Red
}

Write-Host "`nHistorical Baseline (RTX 5070, pre-async):"
Write-Host "  • Sustained: 711,000 RPS"
Write-Host "  • Peak: 3,200,000 RPS"
Write-Host "  • Expected with async: 100K-300K RPS`n"

Write-Host "╔════════════════════════════════════════════════════════════════════════╗`n" -ForegroundColor Cyan

# Stop server
Write-Host "🛑 Stopping GPU server..." -ForegroundColor Yellow
Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue

Write-Host "`n✅ Load test complete!`n" -ForegroundColor Green
