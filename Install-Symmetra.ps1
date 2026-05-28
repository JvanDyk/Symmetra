#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Symmetra GPU Compute Engine - Installer

.DESCRIPTION
    Production installer for Symmetra, handling:
    - Prerequisite validation
    - Binary verification (checksums, signatures)
    - Installation to Program Files
    - Environment setup
    - Service registration
    - Post-install validation

.PARAMETER InstallPath
    Installation directory. Default: C:\Program Files\Symmetra

.PARAMETER SkipVerification
    Skip signature and checksum verification. Default: $false

.PARAMETER RegisterService
    Register Windows service. Default: $true

.PARAMETER SkipTests
    Skip running validation tests. Default: $false

.EXAMPLE
    .\Install-Symmetra.ps1
    .\Install-Symmetra.ps1 -InstallPath 'D:\Symmetra' -RegisterService $true

#>

param(
    [string]$InstallPath = 'C:\Program Files\Symmetra',
    [bool]$SkipVerification = $false,
    [bool]$RegisterService = $true,
    [bool]$SkipTests = $false
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ============================================================================
# CONFIGURATION
# ============================================================================

$script:ScriptRoot = $PSScriptRoot
$script:Version = '1.0.0'
$script:LogFile = Join-Path $env:TEMP "symmetra_install_$(Get-Date -Format 'yyyy-MM-dd_HH-mm-ss').log"

# ============================================================================
# LOGGING
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
    param([string]$Message)

    Log-Message $Message 'ERROR'
    Write-Host "`nInstallation failed. Log: $LogFile" -ForegroundColor Red
}

# ============================================================================
# PREREQUISITE CHECKS
# ============================================================================

function Test-AdminPrivileges {
    Log-Section "Checking Administrator Privileges"

    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    if (-not $isAdmin) {
        Log-Error "Administrator privileges required. Please run as administrator."
        exit 1
    }

    Log-Message "Administrator privileges: ✓" SUCCESS
}

function Test-Prerequisites {
    Log-Section "Validating Prerequisites"

    $required = @('cmake', 'nvcc', 'nvidia-smi')
    $missing = @()

    foreach ($tool in $required) {
        if (Get-Command $tool -ErrorAction SilentlyContinue) {
            Log-Message "$tool: ✓ Found" SUCCESS
        } else {
            Log-Message "$tool: ✗ Not found" WARNING
            $missing += $tool
        }
    }

    if ($missing.Count -gt 0) {
        Log-Message "Missing prerequisites: $($missing -join ', ')" WARNING
        Log-Message "Installation can proceed, but some features may not work." WARNING
    }

    Log-Message "Prerequisite check completed" SUCCESS
}

# ============================================================================
# VERIFICATION FUNCTIONS
# ============================================================================

function Verify-Checksums {
    Log-Section "Verifying File Integrity"

    $checksumFile = Join-Path $script:ScriptRoot 'SHA256SUMS.txt'

    if (-not (Test-Path $checksumFile)) {
        if ($SkipVerification) {
            Log-Message "SHA256SUMS.txt not found (skipping verification)" WARNING
            return $true
        } else {
            Log-Error "SHA256SUMS.txt not found"
            return $false
        }
    }

    $checksums = Get-Content $checksumFile | ConvertFrom-StringData

    $binaries = @('sym.exe', 'codegen_test.exe', 'benchmark_test.exe', 'scalability_test.exe', 'robustness_test.exe', 'performance_test.exe')
    $allValid = $true

    foreach ($binary in $binaries) {
        $binaryPath = Join-Path $script:ScriptRoot $binary

        if (-not (Test-Path $binaryPath)) {
            Log-Message "$binary: not found (skipping)" WARNING
            continue
        }

        $hash = (Get-FileHash -Path $binaryPath -Algorithm SHA256).Hash
        $expectedHash = ($checksums.GetEnumerator() | Where-Object { $_.Value -like "*$binary*" } | Select-Object -First 1).Name

        if ($hash -eq $expectedHash) {
            Log-Message "$binary: ✓ Verified" SUCCESS
        } else {
            Log-Message "$binary: ✗ Checksum mismatch" ERROR
            $allValid = $false
        }
    }

    if (-not $allValid) {
        Log-Error "Checksum verification failed"
        return $false
    }

    Log-Message "All checksums verified" SUCCESS
    return $true
}

function Verify-Signatures {
    Log-Section "Verifying Digital Signatures"

    $binaries = @('sym.exe')

    foreach ($binary in $binaries) {
        $binaryPath = Join-Path $script:ScriptRoot $binary

        if (-not (Test-Path $binaryPath)) {
            Log-Message "$binary: not found" WARNING
            continue
        }

        try {
            $signature = Get-AuthenticodeSignature -FilePath $binaryPath

            if ($signature.Status -eq 'Valid') {
                Log-Message "$binary: ✓ Signed by $($signature.SignerCertificate.Subject)" SUCCESS
            } elseif ($signature.Status -eq 'NotSigned') {
                Log-Message "$binary: ⚠ Not signed" WARNING
            } else {
                Log-Message "$binary: ✗ Invalid signature" ERROR
                return $false
            }
        } catch {
            Log-Message "$binary: Unable to verify signature ($_)" WARNING
        }
    }

    return $true
}

# ============================================================================
# INSTALLATION
# ============================================================================

function Install-Binaries {
    Log-Section "Installing Binaries"

    # Create installation directory
    if (-not (Test-Path $InstallPath)) {
        New-Item -ItemType Directory -Path $InstallPath -Force | Out-Null
        Log-Message "Created: $InstallPath" SUCCESS
    }

    # Copy binaries
    $binaries = @('sym.exe', 'codegen_test.exe', 'benchmark_test.exe', 'scalability_test.exe', 'robustness_test.exe', 'performance_test.exe')

    foreach ($binary in $binaries) {
        $src = Join-Path $script:ScriptRoot $binary
        $dst = Join-Path $InstallPath $binary

        if (-not (Test-Path $src)) {
            Log-Message "$binary: not found in source" WARNING
            continue
        }

        try {
            Copy-Item -Path $src -Destination $dst -Force
            Log-Message "Installed: $binary" SUCCESS
        } catch {
            Log-Error "Failed to copy $binary : $_"
            return $false
        }
    }

    return $true
}

function Setup-Environment {
    Log-Section "Setting Up Environment"

    # Add installation directory to PATH
    $currentPath = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine')

    if ($currentPath -notlike "*$InstallPath*") {
        $newPath = "$InstallPath;$currentPath"
        [System.Environment]::SetEnvironmentVariable('PATH', $newPath, 'Machine')
        Log-Message "Added $InstallPath to PATH" SUCCESS
    } else {
        Log-Message "$InstallPath already in PATH" INFO
    }

    # Set SYMMETRA_HOME environment variable
    [System.Environment]::SetEnvironmentVariable('SYMMETRA_HOME', $InstallPath, 'Machine')
    Log-Message "Set SYMMETRA_HOME=$InstallPath" SUCCESS

    # Refresh environment
    $env:PATH = [System.Environment]::GetEnvironmentVariable('PATH', 'Machine')
}

function Register-Service {
    Log-Section "Registering Windows Service"

    if (-not $RegisterService) {
        Log-Message "Service registration skipped" INFO
        return $true
    }

    $serviceName = 'Symmetra'
    $exePath = Join-Path $InstallPath 'sym.exe'

    # Check if service already exists
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
        Log-Message "Service $serviceName already exists" WARNING

        try {
            Stop-Service -Name $serviceName -Force -ErrorAction SilentlyContinue
            Remove-Service -Name $serviceName -Force -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 1
            Log-Message "Removed existing service" INFO
        } catch {
            Log-Message "Failed to remove existing service: $_" WARNING
        }
    }

    try {
        $params = @{
            Name            = $serviceName
            BinaryPathName  = "`"$exePath`" --port 8080 --daemon"
            DisplayName     = 'Symmetra GPU Compute Engine'
            Description     = 'GPU-accelerated HTTP server for TypeScript/CUDA'
            StartupType     = 'Automatic'
            ErrorControl    = 'Normal'
        }

        New-Service @params | Out-Null
        Log-Message "Service registered: $serviceName" SUCCESS

        # Set service to run as Network Service
        $sc = Get-CimInstance -ClassName Win32_Service -Filter "Name='$serviceName'"
        if ($null -ne $sc) {
            Invoke-CimMethod -InputObject $sc -MethodName Change -Arguments @{ StartName = 'NT AUTHORITY\NetworkService'; StartPassword = $null } | Out-Null
            Log-Message "Service user: NT AUTHORITY\NetworkService" SUCCESS
        }

        return $true
    } catch {
        Log-Message "Failed to register service: $_" ERROR
        return $false
    }
}

# ============================================================================
# VALIDATION
# ============================================================================

function Validate-Installation {
    Log-Section "Validating Installation"

    $binaries = @('sym.exe')

    foreach ($binary in $binaries) {
        $binaryPath = Join-Path $InstallPath $binary

        if (Test-Path $binaryPath) {
            $size = (Get-Item $binaryPath).Length / 1MB
            Log-Message "$binary: ✓ Found ($([Math]::Round($size, 2)) MB)" SUCCESS
        } else {
            Log-Message "$binary: ✗ Not found" ERROR
            return $false
        }
    }

    # Test sym.exe
    if (-not $SkipTests) {
        try {
            $symPath = Join-Path $InstallPath 'sym.exe'
            & $symPath --version 2>&1 | Out-Null

            if ($LASTEXITCODE -eq 0) {
                Log-Message "sym.exe: ✓ Executable" SUCCESS
            } else {
                Log-Message "sym.exe: ⚠ Executable but returned exit code $LASTEXITCODE" WARNING
            }
        } catch {
            Log-Message "sym.exe: ✗ Execution failed ($_)" ERROR
            return $false
        }
    }

    return $true
}

# ============================================================================
# UNINSTALL
# ============================================================================

function Create-Uninstaller {
    Log-Section "Creating Uninstaller"

    $uninstallerPath = Join-Path $InstallPath 'Uninstall-Symmetra.ps1'

    $uninstallerScript = @'
#!/usr/bin/env pwsh
param([bool]$RemoveService = $true)

$InstallPath = "$PSScriptRoot"
Write-Host "Uninstalling Symmetra from: $InstallPath"

if ($RemoveService) {
    if (Get-Service -Name 'Symmetra' -ErrorAction SilentlyContinue) {
        Stop-Service -Name 'Symmetra' -Force -ErrorAction SilentlyContinue
        Remove-Service -Name 'Symmetra' -Force -ErrorAction SilentlyContinue
        Write-Host "Service removed"
    }
}

Remove-Item -Recurse -Force $InstallPath -ErrorAction SilentlyContinue
Write-Host "Uninstalled successfully"
'@

    $uninstallerScript | Out-File -FilePath $uninstallerPath -Encoding UTF8 -Force
    Log-Message "Uninstaller created: $uninstallerPath" SUCCESS
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

function Main {
    Write-Host "`n" + ("=" * 70) -ForegroundColor Cyan
    Write-Host "SYMMETRA GPU COMPUTE ENGINE - INSTALLER" -ForegroundColor Cyan
    Write-Host ("=" * 70) -ForegroundColor Cyan
    Write-Host "`nVersion: $script:Version" -ForegroundColor Cyan
    Write-Host "Installation Path: $InstallPath" -ForegroundColor Cyan
    Write-Host "Log File: $LogFile" -ForegroundColor Cyan

    Log-Section "Installation Started"

    # Pre-installation checks
    Test-AdminPrivileges
    Test-Prerequisites

    if (-not $SkipVerification) {
        if (-not (Verify-Checksums)) { exit 1 }
        if (-not (Verify-Signatures)) { exit 1 }
    }

    # Installation
    if (-not (Install-Binaries)) { exit 1 }
    Setup-Environment
    if (-not (Register-Service)) { exit 1 }
    Create-Uninstaller

    # Validation
    if (-not (Validate-Installation)) { exit 1 }

    # Success
    Log-Section "Installation Complete"
    Log-Message "Symmetra installed successfully" SUCCESS
    Log-Message "`nTo start the service: Start-Service -Name 'Symmetra'" INFO
    Log-Message "To verify: sym --version" INFO
    Log-Message "Documentation: $InstallPath\README.md" INFO

    Write-Host "`n" + ("=" * 70) -ForegroundColor Green
    Write-Host "✓ INSTALLATION SUCCESSFUL" -ForegroundColor Green
    Write-Host ("=" * 70) -ForegroundColor Green
    Write-Host "`n"

    exit 0
}

try {
    Main
} catch {
    Log-Error "Unexpected error: $_"
    exit 1
}
