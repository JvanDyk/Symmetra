#!/usr/bin/env pwsh
param([int]$Port = 9000, [int]$Dur = 60, [int]$W = 32)

Write-Host "`nGPU Load Test Starting...`n" -ForegroundColor Cyan

$sym = "D:\GITHUB\SYMMETRA\engine\build\Release\sym.exe"
$test = "D:\GITHUB\SYMMETRA\gpu_bench_test.ts"

if (-not (Test-Path $test)) {
    Set-Content -Path $test -Value @'
export function compute(n: number): number {
    let a = n;
    for (let i = 0; i < 256; i++) {
        a = a ^ ((a << 1) | (a >> 1));
    }
    return a;
}
export default async function handler(req: Request): Promise<Response> {
    let r = 0;
    for (let i = 0; i < 32; i++) r += compute(i);
    return new Response(JSON.stringify({ok:true,r}), {status:200});
}
'@
}

Write-Host "Starting server on port $Port..." -ForegroundColor Green
$proc = Start-Process -FilePath $sym -ArgumentList @("`"$test`"", "--port", $Port) -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 4

Write-Host "Running $Dur second load test with $W workers..." -ForegroundColor Green
$url = "http://localhost:$Port/"
$start = Get-Date
$success = 0
$fail = 0
$lats = @()

$jobs = @()
for ($i = 0; $i -lt $W; $i++) {
    $job = Start-Job {
        param($u, $d)
        $s = 0; $f = 0; $l = @()
        $end = (Get-Date).AddSeconds($d)
        while ((Get-Date) -lt $end) {
            $t1 = Get-Date
            $r = Invoke-WebRequest -Uri $u -Method GET -TimeoutSec 5 -UseBasicParsing -ErrorAction SilentlyContinue
            $t2 = Get-Date
            if ($r) { $s++ } else { $f++ }
            $l += ($t2-$t1).TotalMilliseconds
        }
        @{s=$s; f=$f; l=$l}
    } -ArgumentList $url, $Dur
    $jobs += $job
}

while ($jobs | Where-Object State -eq Running) {
    $c = ($jobs | Where-Object State -eq Completed).Count
    Write-Host "`r  Completed: $c/$W" -NoNewline
    Start-Sleep -Seconds 1
}

foreach ($job in $jobs) {
    $res = Receive-Job $job
    $success += $res.s
    $fail += $res.f
    $lats += $res.l
    Remove-Job $job
}

$end = Get-Date
$sec = ($end - $start).TotalSeconds
$rps = $success / $sec
$lats = $lats | Sort-Object
$avg = ($lats | Measure-Object -Average).Average
$p99 = $lats[[int]($lats.Count * 0.99)]

Write-Host "`n`n╔════════════════════════════════════════════════════════════════╗"
Write-Host "║  GPU ASYNC CALLBACK LOAD TEST RESULTS                            ║"
Write-Host "╚════════════════════════════════════════════════════════════════╝`n"

Write-Host "Throughput: $([int]$rps) RPS" -ForegroundColor Green
Write-Host "  Requests: $success success, $fail failed"
Write-Host "  Duration: $([math]::Round($sec,1)) seconds`n"

Write-Host "Latency (ms):" -ForegroundColor Green
Write-Host "  Min:      $([math]::Round($lats[0], 2))"
Write-Host "  Avg:      $([math]::Round($avg, 2))"
Write-Host "  P99:      $([math]::Round($p99, 2))"
Write-Host "  Max:      $([math]::Round($lats[-1], 2))`n"

if ($rps -ge 100000) {
    Write-Host "✅ EXCELLENT GPU Performance: $([int]$rps) RPS" -ForegroundColor Green
} elseif ($rps -ge 10000) {
    Write-Host "✅ Good: $([int]$rps) RPS" -ForegroundColor Green
} else {
    Write-Host "⚠️ Result: $([int]$rps) RPS" -ForegroundColor Yellow
}

Write-Host "`nExpected: 100K-300K RPS on RTX 5070 with async callbacks"
Write-Host "Baseline: 711K RPS (pre-async), 3.2M RPS (peak)`n"

Write-Host "╔════════════════════════════════════════════════════════════════╗`n"

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
