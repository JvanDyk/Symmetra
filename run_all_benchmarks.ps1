# PowerShell script to benchmark all Turing machine variants

$variants = @(
    @{ Name = "LIGHTNING"; Path = "releases/turing-machines/lightning/turing_lightning_server.ts"; Port = 8001 },
    @{ Name = "HYPER"; Path = "releases/turing-machines/hyper/turing_hyper_server.ts"; Port = 8002 },
    @{ Name = "EXTREME"; Path = "releases/turing-machines/extreme/turing_extreme_server.ts"; Port = 8003 },
    @{ Name = "ULTRA"; Path = "releases/turing-machines/ultra/turing_ultra_server.ts"; Port = 8004 },
    @{ Name = "BALANCED"; Path = "releases/turing-machines/balanced/turing_balanced_server.ts"; Port = 8005 },
    @{ Name = "SUPERFAST"; Path = "releases/turing-machines/superfast/turing_superfast_server.ts"; Port = 8006 },
    @{ Name = "SIMPLE"; Path = "releases/turing-machines/simple/turing_simple_server.ts"; Port = 8007 },
    @{ Name = "MODERATE"; Path = "releases/turing-machines/moderate/turing_moderate_server.ts"; Port = 8008 },
    @{ Name = "REFERENCE"; Path = "releases/turing-machines/reference/turing_reference_server.ts"; Port = 8009 }
)

$results = @()

foreach ($variant in $variants) {
    Write-Host ""
    Write-Host "Starting benchmark for $($variant.Name)..."

    # Check if file exists
    if (-not (Test-Path $variant.Path)) {
        Write-Host "✗ File not found: $($variant.Path)"
        continue
    }

    # Start server
    $process = Start-Process -FilePath ".\engine\build\Release\sym.exe" `
        -ArgumentList $variant.Path `
        -NoNewWindow `
        -PassThru `
        -ErrorAction SilentlyContinue

    if ($null -eq $process) {
        Write-Host "✗ Failed to start server"
        continue
    }

    # Run benchmark
    & bash -c "./benchmark_variant.sh '$($variant.Name)' $($variant.Port) 20" 2>&1 | Out-Host

    # Kill server
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

Write-Host ""
Write-Host "All benchmarks complete!"
