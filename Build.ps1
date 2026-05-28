#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Symmetra GPU Compute Engine - Production Build Orchestrator

.DESCRIPTION
    Complete build system for Symmetra, handling:
    - Prerequisite validation
    - CMake configuration
    - Compilation with security hardening
    - Test execution
    - Binary signing
    - Distribution packaging
    - Checksum generation

.PARAMETER Configuration
    Build configuration: Release (default) or Debug

.PARAMETER Action
    Build action: Full (default), Configure, Compile, Test, Sign, Package, Verify, Clean

.PARAMETER SignBinaries
    Sign compiled binaries (requires cert). Default: $true for Release

.PARAMETER CreatePackage
    Create distribution package (zip + checksums). Default: $true for Release

.PARAMETER Parallel
    Number of parallel build jobs. Default: 8

.EXAMPLE
    .\Build.ps1 -Action Full -Configuration Release -SignBinaries $true

.EXAMPLE
    .\Build.ps1 -Action Test -Configuration Release

#>

param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',

    [ValidateSet('Full', 'Configure', 'Compile', 'Test', 'Sign', 'Package', 'Verify', 'Clean')]
    [string]$Action = 'Full',

    [bool]$SignBinaries = $true,

    [bool]$CreatePackage = $true,

    [int]$Parallel = 8
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ============================================================================
# CONFIGURATION
# ============================================================================

$script:BuildRoot = $PSScriptRoot
$script:EngineRoot = Join-Path $BuildRoot 'engine'
$script:BuildDir = Join-Path $EngineRoot 'build'
$script:OutputDir = Join-Path $BuildRoot 'dist'
$script:LogDir = Join-Path $OutputDir 'logs'

$script:Version = '1.0.0'
$script:BuildTimestamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$script:LogFile = Join-Path $LogDir "build_$script:BuildTimestamp.log"

# Load configuration
$configPath = Join-Path $BuildRoot 'build.config.json'
if (Test-Path $configPath) {
    $script:Config = Get-Content $configPath | ConvertFrom-Json
} else {
    Write-Warning "build.config.json not found, using defaults"
    $script:Config = @{}
}

# ============================================================================
# LOGGING UTILITIES
# ============================================================================

function Log-Message {
    param([string]$Message, [string]$Level = 'INFO')
    $timestamp = Get-Date -Format 'HH:mm:ss'
    $color = @{
        'INFO'    = 'Cyan'
        'SUCCESS' = 'Green'
        'WARNING' = 'Yellow'
        'ERROR'   = 'Red'
    }[$Level]

    $output = "[$timestamp] [$Level] $Message"
    Write-Host $output -ForegroundColor $color
    Add-Content $LogFile $output -ErrorAction SilentlyContinue
}

function Log-Section {
    param([string]$Title)
    Log-Message "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" INFO
    Log-Message $Title INFO
    Log-Message "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" INFO
}

function Log-Error {
    param([string]$Message, [int]$ExitCode = 1)
    Log-Message $Message 'ERROR'
    Write-Host "`nBuild failed. Log: $LogFile" -ForegroundColor Red
    exit $ExitCode
}

# ============================================================================
# DIRECTORY SETUP
# ============================================================================

function Initialize-Directories {
    Log-Section "Initializing Directories"

    @($OutputDir, $LogDir, $BuildDir) | ForEach-Object {
        if (-not (Test-Path $_)) {
            New-Item -ItemType Directory -Path $_ -Force | Out-Null
            Log-Message "Created: $_" INFO
        }
    }
}

# ============================================================================
# PREREQUISITE VALIDATION
# ============================================================================

function Validate-Prerequisites {
    Log-Section "Validating Prerequisites"

    $prerequisites = @(
        @{ Name = 'CMake'; Command = 'cmake'; MinVersion = '3.20' }
        @{ Name = 'Visual Studio Build Tools'; Command = 'msbuild'; MinVersion = '17.0' }
        @{ Name = 'CUDA Toolkit'; Command = 'nvcc'; MinVersion = '13.1' }
        @{ Name = 'esbuild'; Command = 'esbuild'; MinVersion = '0.15' }
    )

    $allValid = $true

    foreach ($prereq in $prerequisites) {
        try {
            $result = & $prereq.Command --version 2>&1
            if ($LASTEXITCODE -eq 0) {
                Log-Message "$($prereq.Name): ✓ Found" SUCCESS
            } else {
                Log-Message "$($prereq.Name): ✗ Command failed" WARNING
                $allValid = $false
            }
        } catch {
            Log-Message "$($prereq.Name): ✗ Not found in PATH" WARNING
            $allValid = $false
        }
    }

    if (-not $allValid) {
        Log-Error "Some prerequisites missing. Run: .\Validate-Prerequisites.ps1"
    }

    Log-Message "All prerequisites validated" SUCCESS
}

# ============================================================================
# CMAKE CONFIGURATION
# ============================================================================

function Configure-CMake {
    Log-Section "Configuring CMake"

    $cmakeArgs = @(
        "-B", $BuildDir
        "-S", $EngineRoot
        "-G", "Visual Studio 17 2022"
        "-A", "x64"
        "-DCMAKE_BUILD_TYPE=$Configuration"
        "-DCMAKE_CONFIGURATION_TYPES=$Configuration"
        "-DSYMM_BUILD_TESTS=ON"
        "-DSYMM_BUILD_EXAMPLES=ON"
        "-DSYMM_ENABLE_LTO=ON"
        "-DSYMM_ENABLE_SECURITY_HARDENING=ON"
        "-DSYMM_VERSION=$script:Version"
    )

    Log-Message "CMake command: cmake $(($cmakeArgs -join ' '))" INFO

    try {
        & cmake @cmakeArgs 2>&1 | Tee-Object -FilePath $LogFile -Append
        if ($LASTEXITCODE -ne 0) {
            Log-Error "CMake configuration failed"
        }
        Log-Message "CMake configuration completed" SUCCESS
    } catch {
        Log-Error "CMake configuration error: $_"
    }
}

# ============================================================================
# COMPILATION
# ============================================================================

function Invoke-Compilation {
    Log-Section "Compiling Engine"

    $buildArgs = @(
        "--build", $BuildDir
        "--config", $Configuration
        "--parallel", $Parallel
        "--verbose"
    )

    Log-Message "Build command: cmake $(($buildArgs -join ' '))" INFO

    try {
        $startTime = Get-Date
        & cmake @buildArgs 2>&1 | Tee-Object -FilePath $LogFile -Append

        if ($LASTEXITCODE -ne 0) {
            Log-Error "Compilation failed"
        }

        $duration = (Get-Date) - $startTime
        Log-Message "Compilation completed in $($duration.TotalSeconds) seconds" SUCCESS
    } catch {
        Log-Error "Compilation error: $_"
    }
}

# ============================================================================
# TEST EXECUTION
# ============================================================================

function Invoke-Tests {
    Log-Section "Running Test Suite"

    $testArgs = @(
        "-C", $Configuration
        "--output-on-failure"
        "--verbose"
    )

    Log-Message "Test command: ctest $(($testArgs -join ' '))" INFO

    try {
        $startTime = Get-Date
        Push-Location $BuildDir
        & ctest @testArgs 2>&1 | Tee-Object -FilePath $LogFile -Append
        Pop-Location

        if ($LASTEXITCODE -ne 0) {
            Log-Error "Tests failed"
        }

        $duration = (Get-Date) - $startTime
        Log-Message "Tests completed in $($duration.TotalSeconds) seconds" SUCCESS
    } catch {
        Log-Error "Test execution error: $_"
    }
}

# ============================================================================
# BINARY SIGNING (SECURITY)
# ============================================================================

function Sign-Binaries {
    Log-Section "Signing Binaries"

    if (-not $SignBinaries -or $Configuration -eq 'Debug') {
        Log-Message "Binary signing skipped (Debug mode or disabled)" WARNING
        return
    }

    $binaries = @(
        'sym.exe'
        'codegen_test.exe'
        'benchmark_test.exe'
        'scalability_test.exe'
        'robustness_test.exe'
        'performance_test.exe'
    )

    $releaseDir = Join-Path $BuildDir $Configuration

    foreach ($binary in $binaries) {
        $binaryPath = Join-Path $releaseDir $binary

        if (-not (Test-Path $binaryPath)) {
            Log-Message "Binary not found: $binary" WARNING
            continue
        }

        # Check for certificate
        $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Select-Object -First 1

        if ($null -eq $cert) {
            Log-Message "No code signing certificate found. Skipping signing for: $binary" WARNING
            continue
        }

        try {
            Set-AuthenticodeSignature -FilePath $binaryPath -Certificate $cert -TimestampServer 'http://timestamp.comodoca.com' | Out-Null
            Log-Message "Signed: $binary" SUCCESS
        } catch {
            Log-Message "Failed to sign $binary : $_" WARNING
        }
    }

    Log-Message "Binary signing completed" SUCCESS
}

# ============================================================================
# CHECKSUM GENERATION
# ============================================================================

function Generate-Checksums {
    param([string]$OutputPath)

    Log-Section "Generating Checksums"

    $releaseDir = Join-Path $BuildDir $Configuration
    $checksumFile = Join-Path $OutputPath 'SHA256SUMS.txt'

    $binaries = @(
        'sym.exe'
        'codegen_test.exe'
        'benchmark_test.exe'
        'scalability_test.exe'
        'robustness_test.exe'
        'performance_test.exe'
    )

    $checksums = @()

    foreach ($binary in $binaries) {
        $binaryPath = Join-Path $releaseDir $binary

        if (-not (Test-Path $binaryPath)) {
            Log-Message "Binary not found: $binary (skipping checksum)" WARNING
            continue
        }

        try {
            $hash = (Get-FileHash -Path $binaryPath -Algorithm SHA256).Hash
            $checksums += "$hash  $binary"
            Log-Message "SHA256 ($binary): $hash" INFO
        } catch {
            Log-Message "Failed to hash $binary : $_" WARNING
        }
    }

    $checksums | Out-File -FilePath $checksumFile -Encoding UTF8
    Log-Message "Checksums saved to: $checksumFile" SUCCESS
}

# ============================================================================
# PACKAGE CREATION
# ============================================================================

function Create-Distribution-Package {
    Log-Section "Creating Distribution Package"

    if (-not $CreatePackage) {
        Log-Message "Package creation disabled" WARNING
        return
    }

    $releaseDir = Join-Path $BuildDir $Configuration
    $packageDir = Join-Path $OutputDir "symmetra-$script:Version"
    $packageZip = Join-Path $OutputDir "symmetra-$script:Version.zip"

    # Create package directory structure
    if (Test-Path $packageDir) {
        Remove-Item -Recurse -Force $packageDir
    }
    New-Item -ItemType Directory -Path $packageDir | Out-Null

    # Copy binaries
    $binaries = @('sym.exe', 'codegen_test.exe', 'benchmark_test.exe', 'scalability_test.exe', 'robustness_test.exe', 'performance_test.exe')
    foreach ($binary in $binaries) {
        $src = Join-Path $releaseDir $binary
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $packageDir
            Log-Message "Packaged: $binary" INFO
        }
    }

    # Copy documentation
    $docs = @('README.md', 'QUICK_START.md', 'EXAMPLES.md', 'DEPLOYMENT_GUIDE.md', 'LICENSE.txt')
    foreach ($doc in $docs) {
        $src = Join-Path $BuildRoot $doc
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $packageDir
            Log-Message "Packaged: $doc" INFO
        }
    }

    # Copy installer script
    $installer = Join-Path $BuildRoot 'Install-Symmetra.ps1'
    if (Test-Path $installer) {
        Copy-Item -Path $installer -Destination $packageDir
        Log-Message "Packaged: installer script" INFO
    }

    # Generate checksums in package directory
    Generate-Checksums -OutputPath $packageDir

    # Create ZIP archive
    try {
        Compress-Archive -Path $packageDir -DestinationPath $packageZip -Force
        Log-Message "Package created: $packageZip" SUCCESS

        $zipHash = (Get-FileHash -Path $packageZip -Algorithm SHA256).Hash
        Log-Message "Package SHA256: $zipHash" INFO
    } catch {
        Log-Message "Failed to create package: $_" WARNING
    }
}

# ============================================================================
# VERIFICATION
# ============================================================================

function Verify-Build {
    Log-Section "Verifying Build"

    $releaseDir = Join-Path $BuildDir $Configuration
    $binaries = @('sym.exe', 'codegen_test.exe', 'benchmark_test.exe')
    $allExist = $true

    foreach ($binary in $binaries) {
        $path = Join-Path $releaseDir $binary
        if (Test-Path $path) {
            $size = (Get-Item $path).Length
            Log-Message "$binary exists ($('{0:N0}' -f $size) bytes)" SUCCESS
        } else {
            Log-Message "$binary not found" ERROR
            $allExist = $false
        }
    }

    if (-not $allExist) {
        Log-Error "Verification failed: missing binaries"
    }

    # Verify test binaries are executable
    try {
        $testExe = Join-Path $releaseDir 'codegen_test.exe'
        if (Test-Path $testExe) {
            & $testExe --version 2>&1 | Out-Null
            Log-Message "Binaries verified executable" SUCCESS
        }
    } catch {
        Log-Message "Warning: Could not verify executable: $_" WARNING
    }
}

# ============================================================================
# CLEAN BUILD ARTIFACTS
# ============================================================================

function Clean-BuildArtifacts {
    Log-Section "Cleaning Build Artifacts"

    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
        Log-Message "Removed: $BuildDir" SUCCESS
    }

    Log-Message "Clean completed" SUCCESS
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

function Main {
    Log-Section "Symmetra GPU Compute Engine - Build System"
    Log-Message "Configuration: $Configuration" INFO
    Log-Message "Action: $Action" INFO
    Log-Message "Version: $script:Version" INFO
    Log-Message "Timestamp: $script:BuildTimestamp" INFO

    Initialize-Directories

    switch ($Action) {
        'Full' {
            Validate-Prerequisites
            Configure-CMake
            Invoke-Compilation
            Invoke-Tests
            Sign-Binaries
            Create-Distribution-Package
            Verify-Build
            Log-Section "BUILD COMPLETE"
            Log-Message "Logs: $LogFile" SUCCESS
        }
        'Configure' {
            Validate-Prerequisites
            Configure-CMake
        }
        'Compile' {
            Invoke-Compilation
        }
        'Test' {
            Invoke-Tests
        }
        'Sign' {
            Sign-Binaries
        }
        'Package' {
            Create-Distribution-Package
            Generate-Checksums -OutputPath $OutputDir
        }
        'Verify' {
            Verify-Build
        }
        'Clean' {
            Clean-BuildArtifacts
        }
    }
}

try {
    Main
    exit 0
} catch {
    Log-Error "Unexpected error: $_"
}
