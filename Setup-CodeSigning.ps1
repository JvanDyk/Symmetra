#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Symmetra Code Signing Setup

.DESCRIPTION
    Sets up code signing infrastructure for production builds:
    - Generates self-signed certificate (development)
    - Imports external certificate (production)
    - Validates certificate chain
    - Tests code signing

.PARAMETER Mode
    Setup mode: 'SelfSigned' (development) or 'ImportCert' (production)

.PARAMETER CertificatePath
    Path to PFX certificate file (for ImportCert mode)

.PARAMETER Password
    Certificate password (for ImportCert mode)

.EXAMPLE
    .\Setup-CodeSigning.ps1 -Mode SelfSigned
    .\Setup-CodeSigning.ps1 -Mode ImportCert -CertificatePath 'path\to\cert.pfx' -Password 'password'

#>

param(
    [ValidateSet('SelfSigned', 'ImportCert', 'ListCerts', 'TestSign', 'Verify')]
    [string]$Mode = 'ListCerts',

    [string]$CertificatePath,
    [string]$Password
)

$ErrorActionPreference = 'Stop'

# ============================================================================
# CONFIGURATION
# ============================================================================

$CertName = 'Symmetra Code Signing'
$CertSubject = 'CN=Symmetra Code Signing, O=Symmetra Development, C=US'

# ============================================================================
# UTILITIES
# ============================================================================

function Write-Header {
    param([string]$Title)
    Write-Host "`n" + ("=" * 70) -ForegroundColor Cyan
    Write-Host $Title -ForegroundColor Cyan
    Write-Host ("=" * 70) -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓ $Message" -ForegroundColor Green
}

function Write-Error {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
}

function Write-Warning {
    param([string]$Message)
    Write-Host "⚠ $Message" -ForegroundColor Yellow
}

# ============================================================================
# ADMIN CHECK
# ============================================================================

function Test-AdminPrivileges {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    return $isAdmin
}

# ============================================================================
# CERTIFICATE OPERATIONS
# ============================================================================

function Create-SelfSignedCertificate {
    Write-Header "Creating Self-Signed Code Signing Certificate"

    if (-not (Test-AdminPrivileges)) {
        Write-Error "Administrator privileges required"
        return $false
    }

    # Check if certificate already exists
    $existing = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $CertSubject }
    if ($existing) {
        Write-Warning "Certificate already exists: $($existing.Thumbprint)"
        return $true
    }

    try {
        $cert = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject $CertSubject `
            -FriendlyName $CertName `
            -CertStoreLocation Cert:\CurrentUser\My `
            -NotAfter (Get-Date).AddYears(5) `
            -TextExtension '2.5.29.37={text}1.3.6.1.5.5.7.3.3'

        Write-Success "Certificate created"
        Write-Host "Thumbprint: $($cert.Thumbprint)" -ForegroundColor Cyan
        Write-Host "Subject: $($cert.Subject)" -ForegroundColor Cyan
        Write-Host "Valid until: $($cert.NotAfter)" -ForegroundColor Cyan

        # Add to trusted root for testing
        $store = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser')
        $store.Open('ReadWrite')
        $store.Add($cert)
        $store.Close()

        Write-Success "Certificate added to Trusted Root"
        return $true
    } catch {
        Write-Error "Failed to create certificate: $_"
        return $false
    }
}

function Import-Certificate {
    Write-Header "Importing Code Signing Certificate"

    if (-not (Test-AdminPrivileges)) {
        Write-Error "Administrator privileges required"
        return $false
    }

    if (-not (Test-Path $CertificatePath)) {
        Write-Error "Certificate file not found: $CertificatePath"
        return $false
    }

    try {
        if ($Password) {
            $secPassword = ConvertTo-SecureString $Password -AsPlainText -Force
        } else {
            Write-Warning "No password provided. Certificate will be imported without password protection."
            $secPassword = $null
        }

        Import-PfxCertificate -FilePath $CertificatePath `
            -CertStoreLocation Cert:\CurrentUser\My `
            -Password $secPassword | Out-Null

        Write-Success "Certificate imported successfully"

        # List imported certificate
        Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | ForEach-Object {
            Write-Host "Thumbprint: $($_.Thumbprint)" -ForegroundColor Cyan
            Write-Host "Subject: $($_.Subject)" -ForegroundColor Cyan
            Write-Host "Issuer: $($_.Issuer)" -ForegroundColor Cyan
            Write-Host "Valid until: $($_.NotAfter)" -ForegroundColor Cyan
        }

        return $true
    } catch {
        Write-Error "Failed to import certificate: $_"
        return $false
    }
}

function List-Certificates {
    Write-Header "Available Code Signing Certificates"

    $certs = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert

    if ($certs.Count -eq 0) {
        Write-Warning "No code signing certificates found"
        Write-Host "`nTo create a certificate, run:" -ForegroundColor Yellow
        Write-Host "  .\Setup-CodeSigning.ps1 -Mode SelfSigned" -ForegroundColor Yellow
        return
    }

    $certs | ForEach-Object {
        Write-Host "`nCertificate:" -ForegroundColor Green
        Write-Host "  Thumbprint: $($_.Thumbprint)"
        Write-Host "  Subject: $($_.Subject)"
        Write-Host "  Issuer: $($_.Issuer)"
        Write-Host "  Issued: $($_.NotBefore)"
        Write-Host "  Expires: $($_.NotAfter)"

        $daysLeft = ($_.NotAfter - (Get-Date)).Days
        if ($daysLeft -lt 30) {
            Write-Warning "  Certificate expires in $daysLeft days"
        } else {
            Write-Host "  Status: Valid ($daysLeft days remaining)" -ForegroundColor Green
        }
    }
}

function Test-CodeSigning {
    Write-Header "Testing Code Signing"

    $testFile = Join-Path $env:TEMP 'test_sign.exe'
    $originalFile = Join-Path (Get-Location) 'engine\build\Release\sym.exe'

    if (-not (Test-Path $originalFile)) {
        Write-Error "sym.exe not found. Please build first."
        return $false
    }

    # Get certificate
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Select-Object -First 1

    if ($null -eq $cert) {
        Write-Error "No code signing certificate found"
        return $false
    }

    # Create test file
    Copy-Item $originalFile $testFile -Force
    Write-Host "Test file created: $testFile" -ForegroundColor Cyan

    # Sign file
    try {
        Set-AuthenticodeSignature -FilePath $testFile `
            -Certificate $cert `
            -TimestampServer 'http://timestamp.comodoca.com' | Out-Null

        Write-Success "File signed successfully"
    } catch {
        Write-Error "Signing failed: $_"
        return $false
    }

    # Verify signature
    $sig = Get-AuthenticodeSignature -FilePath $testFile

    Write-Host "`nSignature Details:" -ForegroundColor Cyan
    Write-Host "  Status: $($sig.Status)" -ForegroundColor $(if ($sig.Status -eq 'Valid') { 'Green' } else { 'Red' })
    Write-Host "  Signer: $($sig.SignerCertificate.Subject)"
    Write-Host "  Thumbprint: $($sig.SignerCertificate.Thumbprint)"
    Write-Host "  Timestamp: $($sig.TimeStamperCertificate.Subject)"

    # Cleanup
    Remove-Item $testFile -Force

    return $sig.Status -eq 'Valid'
}

function Verify-SignedBinary {
    param([string]$FilePath)

    Write-Header "Verifying Signed Binary"

    if (-not (Test-Path $FilePath)) {
        Write-Error "File not found: $FilePath"
        return $false
    }

    $sig = Get-AuthenticodeSignature -FilePath $FilePath

    Write-Host "File: $FilePath" -ForegroundColor Cyan
    Write-Host "Status: $($sig.Status)" -ForegroundColor $(if ($sig.Status -eq 'Valid') { 'Green' } else { 'Red' })

    if ($sig.SignerCertificate) {
        Write-Host "`nSigner Certificate:" -ForegroundColor Cyan
        Write-Host "  Subject: $($sig.SignerCertificate.Subject)"
        Write-Host "  Issuer: $($sig.SignerCertificate.Issuer)"
        Write-Host "  Thumbprint: $($sig.SignerCertificate.Thumbprint)"
        Write-Host "  Valid: $($sig.SignerCertificate.NotBefore) to $($sig.SignerCertificate.NotAfter)"
    }

    if ($sig.TimeStamperCertificate) {
        Write-Host "`nTimestamp Server:" -ForegroundColor Cyan
        Write-Host "  Subject: $($sig.TimeStamperCertificate.Subject)"
    }

    return $sig.Status -eq 'Valid'
}

# ============================================================================
# CERTIFICATE BACKUP
# ============================================================================

function Export-Certificate {
    Write-Header "Exporting Certificate for Backup"

    if (-not (Test-AdminPrivileges)) {
        Write-Error "Administrator privileges required"
        return $false
    }

    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Select-Object -First 1

    if ($null -eq $cert) {
        Write-Error "No code signing certificate found"
        return $false
    }

    $exportPath = Join-Path (Get-Location) "symmetra_cert_backup_$(Get-Date -Format 'yyyy-MM-dd').pfx"

    try {
        $password = Read-Host -AsSecureString "Enter password to protect exported certificate"
        Export-PfxCertificate -Cert $cert `
            -FilePath $exportPath `
            -Password $password | Out-Null

        Write-Success "Certificate exported to: $exportPath"
        Write-Warning "Protect this file! It contains your private key."
        return $true
    } catch {
        Write-Error "Export failed: $_"
        return $false
    }
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

function Main {
    Write-Host "`n╔════════════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║         SYMMETRA CODE SIGNING SETUP                              ║" -ForegroundColor Cyan
    Write-Host "╚════════════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan

    switch ($Mode) {
        'SelfSigned' {
            if (-not (Test-AdminPrivileges)) {
                Write-Error "This operation requires administrator privileges"
                exit 1
            }
            Create-SelfSignedCertificate
        }

        'ImportCert' {
            if (-not (Test-AdminPrivileges)) {
                Write-Error "This operation requires administrator privileges"
                exit 1
            }
            if (-not $CertificatePath) {
                Write-Error "CertificatePath parameter required for ImportCert mode"
                exit 1
            }
            Import-Certificate
        }

        'ListCerts' {
            List-Certificates
        }

        'TestSign' {
            if (-not (Test-AdminPrivileges)) {
                Write-Error "This operation requires administrator privileges"
                exit 1
            }
            Test-CodeSigning
        }

        'Verify' {
            if (-not $CertificatePath) {
                Write-Error "FilePath parameter required for Verify mode"
                exit 1
            }
            Verify-SignedBinary -FilePath $CertificatePath
        }
    }

    Write-Host "`n"
}

try {
    Main
    exit 0
} catch {
    Write-Error "Unexpected error: $_"
    exit 1
}
