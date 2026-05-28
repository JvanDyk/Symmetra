# Test Lightning variant performance
param([int]$Port = 8080, [int]$Duration = 10)

function Test-ServerReady {
    param([int]$Port = 8080, [int]$Timeout = 30)
    $start = Get-Date
    while ((Get-Date) - $start -lt [TimeSpan]::FromSeconds($Timeout)) {
        try {
            $null = Invoke-WebRequest -Uri "http://localhost:$Port/" -TimeoutSec 1 -ErrorAction SilentlyContinue
            return $true
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    return $false
}

Write-Host "Starting Lightning server on port $Port..." -ForegroundColor Yellow

$serverProcess = Start-Process -FilePath ".\engine\build\Release\sym.exe" `
    -ArgumentList "turing_lightning.ts", "--port", $Port `
    -PassThru -NoNewWindow -RedirectStandardError "stderr.txt" -RedirectStandardOutput "stdout.txt"

Write-Host "Waiting for server (max 30s)..." -ForegroundColor Yellow
$ready = Test-ServerReady -Port $Port -Timeout 30

if ($ready) {
    Write-Host "Server ready" -ForegroundColor Green
    Start-Sleep -Seconds 2

    Write-Host "Testing initial request..." -ForegroundColor Yellow
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:$Port/" -TimeoutSec 5 -ErrorAction Stop
        Write-Host "Server responding (HTTP $($response.StatusCode))" -ForegroundColor Green

        Write-Host ""
        Write-Host "Running $Duration second throughput test..." -ForegroundColor Yellow

        $startTime = Get-Date
        $requestCount = 0
        $errorCount = 0

        while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($Duration)) {
            try {
                $r = Invoke-WebRequest -Uri "http://localhost:$Port/" -TimeoutSec 2 -ErrorAction Stop
                $requestCount++
            } catch {
                $errorCount++
            }
        }

        $elapsed = ((Get-Date) - $startTime).TotalSeconds
        $throughput = [math]::Round($requestCount / $elapsed, 0)

        Write-Host ""
        Write-Host "=== Results ===" -ForegroundColor Green
        Write-Host "Throughput:      $throughput req/s"
        Write-Host "Total Requests:  $requestCount"
        Write-Host "Errors:          $errorCount"
        Write-Host "Elapsed:         $([math]::Round($elapsed, 2)) s"
        Write-Host ""

        $baseline = 711
        $speedup = [math]::Round($throughput / $baseline, 1)
        Write-Host "vs Slow Baseline ($baseline req/s): ${speedup}x" -ForegroundColor Yellow

    } catch {
        Write-Host "Error: $_" -ForegroundColor Red
    }
} else {
    Write-Host "Server startup timeout" -ForegroundColor Red
}

Write-Host ""
Write-Host "Shutting down..." -ForegroundColor Yellow
Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Write-Host "Done" -ForegroundColor Green
