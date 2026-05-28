# Measure Lightning variant throughput and compare to other variants

param([int]$Duration = 15)

function Test-Variant {
    param([string]$VariantFile, [int]$Port, [int]$Duration)

    Write-Host "Starting $VariantFile..." -ForegroundColor Gray
    $proc = Start-Process -FilePath ".\engine\build\Release\sym.exe" `
        -ArgumentList $VariantFile, "--port", $Port `
        -PassThru -NoNewWindow

    # Wait for server to initialize
    Write-Host "  Waiting for initialization..." -ForegroundColor Gray
    Start-Sleep -Seconds 8

    if ($proc.HasExited) {
        Write-Host "  ERROR: Process exited" -ForegroundColor Red
        return $null
    }

    $requestCount = 0
    $errorCount = 0
    $startTime = Get-Date

    # Send requests for Duration seconds
    Write-Host "  Sending requests for $Duration seconds..." -ForegroundColor Gray
    while ((Get-Date) - $startTime -lt [TimeSpan]::FromSeconds($Duration)) {
        try {
            # -SkipHttpErrorCheck: don't throw on 4xx/5xx
            $r = Invoke-WebRequest -Uri "http://localhost:$Port/" `
                -TimeoutSec 2 `
                -SkipHttpErrorCheck `
                -ErrorAction SilentlyContinue

            if ($null -ne $r) {
                $requestCount++
            } else {
                $errorCount++
            }
        } catch {
            $errorCount++
        }
    }

    $elapsed = ((Get-Date) - $startTime).TotalSeconds
    $throughput = [math]::Round($requestCount / $elapsed, 0)

    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1

    return @{
        Throughput = $throughput
        Requests = $requestCount
        Errors = $errorCount
        Elapsed = $elapsed
    }
}

Write-Host ""
Write-Host "Symmetra GPU Engine Performance Testing" -ForegroundColor Cyan
Write-Host "Duration: $Duration seconds per variant" -ForegroundColor Cyan
Write-Host ""

# Test variants
$variants = @(
    @{ File = "turing_lightning.ts"; Port = 9000; Name = "LIGHTNING (pure compute)" },
    @{ File = "turing_ultra.ts"; Port = 9001; Name = "ULTRA (10-cell, no state)" },
    @{ File = "turing_superfast.ts"; Port = 9002; Name = "SUPERFAST (1-state, 20-cell)" },
    @{ File = "turing_fast.ts"; Port = 9003; Name = "FAST (2-state, 30-cell)" }
)

$results = @()

foreach ($v in $variants) {
    Write-Host "$($v.Name)" -ForegroundColor Cyan
    $result = Test-Variant -VariantFile $v.File -Port $v.Port -Duration $Duration

    if ($result) {
        $results += @{
            Name = $v.Name
            File = $v.File
            Throughput = $result.Throughput
            Requests = $result.Requests
        }
        Write-Host "  Result: $($result.Throughput) req/s ($($result.Requests) requests)" -ForegroundColor Green
    } else {
        Write-Host "  FAILED" -ForegroundColor Red
    }
}

# Display comparison
Write-Host ""
Write-Host "Performance Comparison:" -ForegroundColor Cyan
Write-Host ""

$baseline = 711
foreach ($r in $results | Sort-Object -Property Throughput -Descending) {
    $speedup = [math]::Round($r.Throughput / $baseline, 1)
    $rps = $r.Throughput.ToString().PadLeft(8)
    Write-Host "$rps req/s  ($([math]::Max($speedup, 0.1).ToString('0.0').PadLeft(4))x)  - $($r.Name)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Baseline: Conservative estimate (711 req/s)" -ForegroundColor Gray
