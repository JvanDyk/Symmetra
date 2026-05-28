#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Symmetra Prerequisites Validator

.DESCRIPTION
    Validates all prerequisites for building Symmetra:
    - CMake 3.20+
    - Visual Studio Build Tools 2022
    - CUDA Toolkit 13.1+
    - esbuild
    - GPU driver

.PARAMETER InstallMissing
    Automatically install missing prerequisites (requires admin). Default: $false

.PARAMETER Verbose
    Show detailed information. Default: $true

.EXAMPLE
    .\Validate-Prerequisites.ps1
    .\Validate-Prerequisites.ps1 -InstallMissing $true

#>

param(
    [bool]$InstallMissing = $false,
    [bool]$Verbose = $true
)

$ErrorActionPreference = 'Stop'

# ============================================================================
# CONFIGURATION
# ============================================================================

$prerequisites = @(
    @{
        Name        = 'CMake'
        Command     = 'cmake'
        MinVersion  = '3.20.0'
        Installation = 'winget install kitware.cmake'
        Description = 'Build system configuration tool'
        Critical    = $true
    }
    @{
        Name        = 'Visual Studio Build Tools 2022'
        Command     = 'msbuild'
        MinVersion  = '17.0.0'
        Installation = 'winget install Microsoft.VisualStudio.2022.BuildTools'
        Description = 'C++ compiler and build system'
        Critical    = $true
    }
    @{
        Name        = 'CUDA Toolkit'
        Command     = 'nvcc'
        MinVersion  = '13.1.0'
        Installation = 'https://developer.nvidia.com/cuda-downloads'
        Description = 'NVIDIA GPU compilation toolkit'
        Critical    = $true
    }
    @{
        Name        = 'esbuild'
        Command     = 'esbuild'
        MinVersion  = '0.15.0'
        Installation = 'npm install -g esbuild'
        Description = 'JavaScript bundler'
        Critical    = $false
    }
)

$gpuPrerequisites = @(
    @{
        Name        = 'NVIDIA GPU Driver'
        Command     = 'nvidia-smi'
        MinVersion  = '500.0'
        Description = 'GPU driver'
        Critical    = $true
    }
)

# ============================================================================
# UTILITIES
# ============================================================================

function Format-Version {
    param([string]$Version)
    $v = $Version -split '\.' | Select-Object -First 3
    [version]($v -join '.')
}

function Get-CommandVersion {
    param([string]$Command)

    try {
        $output = & $Command --version 2>&1 | Select-Object -First 1
        if ($output -match '(\d+\.?\d*\.?\d*)') {
            return $matches[1]
        }
        return $null
    } catch {
        return $null
    }
}

function Compare-Versions {
    param([string]$Current, [string]$Required)

    try {
        $current = Format-Version $Current
        $required = Format-Version $Required
        return $current -ge $required
    } catch {
        return $false
    }
}

function Write-Check {
    param([string]$Message, [bool]$Pass)

    $symbol = if ($Pass) { '✓' } else { '✗' }
    $color = if ($Pass) { 'Green' } else { 'Red' }
    Write-Host "$symbol $Message" -ForegroundColor $color
}

# ============================================================================
# VALIDATION FUNCTIONS
# ============================================================================

function Test-CommandExists {
    param([string]$Command)

    try {
        $result = Get-Command $Command -ErrorAction SilentlyContinue
        return $null -ne $result
    } catch {
        return $false
    }
}

function Validate-Prerequisite {
    param($Prereq)

    Write-Host "`nValidating $($Prereq.Name)..." -ForegroundColor Cyan

    if (-not (Test-CommandExists $Prereq.Command)) {
        Write-Check "$($Prereq.Command) command: NOT FOUND" $false

        if ($Prereq.Critical) {
            if ($InstallMissing) {
                Write-Host "Installing $($Prereq.Name)..." -ForegroundColor Yellow
                Invoke-Installation $Prereq
            } else {
                Write-Host "Installation: $($Prereq.Installation)" -ForegroundColor Yellow
            }
        }
        return $false
    }

    Write-Check "$($Prereq.Command) command: FOUND" $true

    $version = Get-CommandVersion $Prereq.Command
    if ($null -ne $version) {
        $isValid = Compare-Versions $version $Prereq.MinVersion
        Write-Check "$($Prereq.Name) version $version (required: $($Prereq.MinVersion))" $isValid
        return $isValid
    } else {
        Write-Check "$($Prereq.Name) version: UNKNOWN" $false
        return $true
    }
}

function Validate-GpuDriver {
    param($Prereq)

    Write-Host "`nValidating $($Prereq.Name)..." -ForegroundColor Cyan

    if (-not (Test-CommandExists $Prereq.Command)) {
        Write-Check "nvidia-smi: NOT FOUND" $false
        Write-Host "Install NVIDIA drivers from: https://www.nvidia.com/Download/driverDetails.aspx" -ForegroundColor Yellow
        return $false
    }

    Write-Check "nvidia-smi: FOUND" $true

    try {
        $output = & nvidia-smi --query-gpu=index,name,driver_version,compute_cap --format=csv,noheader 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "`nGPU Information:" -ForegroundColor Green
            $output | ForEach-Object {
                Write-Host "  $_" -ForegroundColor Green
            }
            return $true
        } else {
            Write-Check "GPU detection: FAILED" $false
            return $false
        }
    } catch {
        Write-Check "GPU detection: ERROR ($_)" $false
        return $false
    }
}

function Validate-CudaInstallation {
    Write-Host "`nValidating CUDA Installation..." -ForegroundColor Cyan

    $cudaPath = $env:CUDA_PATH
    if (-not $cudaPath) {
        Write-Check "CUDA_PATH environment variable" $false
        Write-Host "CUDA not found in environment. Checking registry..." -ForegroundColor Yellow

        try {
            $cudaReg = Get-Item 'HKLM:\SOFTWARE\NVIDIA Corporation\Installed Products\CUDA Toolkit*' -ErrorAction SilentlyContinue | Select-Object -Last 1
            if ($cudaReg) {
                $cudaPath = $cudaReg.GetValue('InstallPath')
                Write-Check "CUDA path from registry: $cudaPath" $true
            }
        } catch {
            Write-Check "CUDA registry lookup" $false
            return $false
        }
    } else {
        Write-Check "CUDA_PATH: $cudaPath" $true
    }

    $nvrtcPath = Join-Path $cudaPath 'include\nvrtc.h'
    if (Test-Path $nvrtcPath) {
        Write-Check "NVRTC header found" $true
    } else {
        Write-Check "NVRTC header: NOT FOUND" $false
        return $false
    }

    return $true
}

function Invoke-Installation {
    param($Prereq)

    if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        Write-Host "Installation requires administrator privileges. Please run as administrator." -ForegroundColor Red
        return $false
    }

    try {
        Write-Host "Executing: $($Prereq.Installation)" -ForegroundColor Yellow
        Invoke-Expression $Prereq.Installation
        return $LASTEXITCODE -eq 0
    } catch {
        Write-Host "Installation failed: $_" -ForegroundColor Red
        return $false
    }
}

# ============================================================================
# ENVIRONMENT VALIDATION
# ============================================================================

function Validate-Environment {
    Write-Host "`nValidating System Environment..." -ForegroundColor Cyan

    $osVersion = [System.Environment]::OSVersion
    Write-Check "Windows OS: $($osVersion.VersionString)" $true

    $processorCount = [System.Environment]::ProcessorCount
    Write-Check "CPU cores: $processorCount" ($processorCount -ge 4)

    $memory = (Get-CimInstance -ClassName CIM_PhysicalMemory | Measure-Object -Property capacity -Sum).sum / 1GB
    Write-Check "RAM: $([Math]::Round($memory))GB" ($memory -ge 16)

    $diskSpace = (Get-Volume C).SizeRemaining / 1GB
    Write-Check "Free disk space: $([Math]::Round($diskSpace))GB" ($diskSpace -ge 50)
}

# ============================================================================
# SUMMARY REPORT
# ============================================================================

function Write-SummaryReport {
    param([hashtable[]]$Results)

    Write-Host "`n" + ("=" * 70) -ForegroundColor Cyan
    Write-Host "VALIDATION SUMMARY" -ForegroundColor Cyan
    Write-Host ("=" * 70) -ForegroundColor Cyan

    $passCount = ($Results | Where-Object { $_ -eq $true }).Count
    $totalCount = $Results.Count

    Write-Host "`nResults: $passCount/$totalCount prerequisites validated" -ForegroundColor $(if ($passCount -eq $totalCount) { 'Green' } else { 'Yellow' })

    if ($passCount -eq $totalCount) {
        Write-Host "`n✓ ALL PREREQUISITES MET - Ready to build!" -ForegroundColor Green
        return $true
    } else {
        Write-Host "`n✗ SOME PREREQUISITES MISSING" -ForegroundColor Red
        Write-Host "  Please install missing components and run validation again." -ForegroundColor Yellow
        return $false
    }
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

function Main {
    Write-Host "`n" + ("=" * 70) -ForegroundColor Cyan
    Write-Host "SYMMETRA GPU COMPUTE ENGINE - PREREQUISITES VALIDATOR" -ForegroundColor Cyan
    Write-Host ("=" * 70) -ForegroundColor Cyan

    $results = @()

    # Validate standard prerequisites
    Write-Host "`n[1/3] Validating Build Tools..." -ForegroundColor Magenta
    foreach ($prereq in $prerequisites) {
        $result = Validate-Prerequisite $prereq
        $results += $result
    }

    # Validate GPU and driver
    Write-Host "`n[2/3] Validating GPU Setup..." -ForegroundColor Magenta
    foreach ($prereq in $gpuPrerequisites) {
        $result = Validate-GpuDriver $prereq
        $results += $result
    }

    # Validate CUDA installation
    $cudaValid = Validate-CudaInstallation
    $results += $cudaValid

    # Validate environment
    Write-Host "`n[3/3] Validating System Environment..." -ForegroundColor Magenta
    Validate-Environment

    # Summary
    $allValid = Write-SummaryReport $results

    Write-Host "`n" + ("=" * 70) + "`n" -ForegroundColor Cyan

    exit $(if ($allValid) { 0 } else { 1 })
}

Main
