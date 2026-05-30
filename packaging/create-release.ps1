#!/usr/bin/env pwsh
# Create a GitHub release with packaged engines
# Usage: ./create-release.ps1 -Version "1.0.0" -Owner "JvanDyk" -Repo "symmetra"

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $false)]
    [string]$Owner = "JvanDyk",

    [Parameter(Mandatory = $false)]
    [string]$Repo = "symmetra",

    [Parameter(Mandatory = $false)]
    [string]$ReleaseNotes = "",

    [Parameter(Mandatory = $false)]
    [switch]$PreRelease = $false,

    [Parameter(Mandatory = $false)]
    [switch]$DryRun = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$colors = @{
    Success = "Green"
    Error = "Red"
    Info = "Cyan"
    Warning = "Yellow"
    Section = "Magenta"
}

function Write-Step { param([string]$msg) Write-Host "  →  $msg" -ForegroundColor $colors.Info }
function Write-Ok { param([string]$msg) Write-Host "  ✓  $msg" -ForegroundColor $colors.Success }
function Write-Fail { param([string]$msg) Write-Host "  ✗  $msg" -ForegroundColor $colors.Error; exit 1 }
function Write-Warn { param([string]$msg) Write-Host "  ⚠  $msg" -ForegroundColor $colors.Warning }

Write-Host ""
Write-Host "  🚀 Creating Release: v$Version" -ForegroundColor Magenta
Write-Host ""

# Verify gh CLI is installed
Write-Step "Checking GitHub CLI..."
try {
    $ghVersion = gh --version
    Write-Ok "GitHub CLI available"
}
catch {
    Write-Fail "GitHub CLI not found. Install with: winget install GitHub.cli"
}

# Verify we have release artifacts
Write-Step "Finding release artifacts..."
$packages = Get-ChildItem -Path "release-packages" -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "symmetra-tm-" }
if (-not $packages) {
    Write-Fail "No packages found in release-packages/. Build engines first with build-all-engines.ps1"
}

Write-Ok "Found $($packages.Count) artifacts:"
$packages | ForEach-Object { Write-Host "     - $($_.Name)" -ForegroundColor DarkGray }
Write-Host ""

# Generate release notes if not provided
if (-not $ReleaseNotes) {
    Write-Step "Generating release notes..."
    $ReleaseNotes = @"
# Symmetra v$Version Release

## Changes
- Engine release v$Version
- All Turing Machine variants updated

## Installation

### Windows
\`\`\`powershell
Invoke-WebRequest -Uri 'https://github.com/$Owner/$Repo/releases/download/v$Version/symmetra-$Version-windows-x64.zip' -OutFile 'symmetra.zip'
Expand-Archive -Path 'symmetra.zip' -DestinationPath 'C:\Program Files\Symmetra\'
\`\`\`

### Linux
\`\`\`bash
wget https://github.com/$Owner/$Repo/releases/download/v$Version/symmetra-$Version-linux-x64.tar.gz
tar xzf symmetra-$Version-linux-x64.tar.gz -C /usr/local/
\`\`\`

## Docker
\`\`\`bash
docker pull symmetra:latest
docker run -it symmetra:latest
\`\`\`

## Artifacts

This release includes binaries for Windows, Linux, and Docker images for all Turing Machine variants.

Verify artifact integrity with the provided SHA256SUMS.
"@
    Write-Ok "Release notes generated"
}

# Create GitHub release
Write-Host ""
Write-Step "Creating GitHub release..."

if ($DryRun) {
    Write-Host "  [DRY-RUN] Would create release v$Version on $Owner/$Repo" -ForegroundColor DarkGray
    Write-Host "  [DRY-RUN] Pre-release: $PreRelease" -ForegroundColor DarkGray
    Write-Host "  [DRY-RUN] Artifacts to upload: $($packages.Count) files" -ForegroundColor DarkGray
} else {
    try {
        $releaseArgs = @(
            "release", "create",
            "v$Version",
            "--repo", "$Owner/$Repo",
            "--title", "v$Version",
            "--notes", $ReleaseNotes
        )

        if ($PreRelease) {
            $releaseArgs += "--prerelease"
        }

        Write-Host "    (Creating release...)" -ForegroundColor DarkGray
        & gh @releaseArgs

        Write-Ok "Release created: v$Version"
    }
    catch {
        Write-Fail "Failed to create release: $_"
    }

    # Upload artifacts
    Write-Host ""
    Write-Step "Uploading artifacts..."

    foreach ($package in $packages) {
        try {
            Write-Host "    Uploading: $($package.Name)" -ForegroundColor DarkGray
            & gh release upload "v$Version" $package.FullName --repo "$Owner/$Repo"
            Write-Ok "Uploaded: $($package.Name)"
        }
        catch {
            Write-Warn "Failed to upload $($package.Name): $_"
        }
    }
}

# Summary
Write-Host ""
Write-Host "  ✅ Release Complete!" -ForegroundColor Green
Write-Host ""
Write-Host "  📦 Release Information:" -ForegroundColor Green
Write-Host "     Version: v$Version" -ForegroundColor Cyan
Write-Host "     Repository: $Owner/$Repo" -ForegroundColor Cyan
Write-Host "     URL: https://github.com/$Owner/$Repo/releases/tag/v$Version" -ForegroundColor Cyan
Write-Host "     Pre-Release: $PreRelease" -ForegroundColor Cyan
Write-Host ""
Write-Host "  📊 Artifacts uploaded: $($packages.Count)" -ForegroundColor Green
$packages | ForEach-Object { Write-Host "     - $($_.Name)" -ForegroundColor DarkGray }
Write-Host ""

if ($DryRun) {
    Write-Warn "This was a DRY-RUN. No release was created."
}

Write-Host "  Next steps:" -ForegroundColor Green
Write-Host "     1. Visit: https://github.com/$Owner/$Repo/releases/tag/v$Version"
Write-Host "     2. Review release notes and artifacts"
Write-Host "     3. Share release announcement"
Write-Host ""
