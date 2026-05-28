# Symmetra Release Procedure

This document describes the complete release process for Symmetra, from tagging a version to publishing binaries across all platforms.

## Overview

Symmetra uses a **binary-only distribution model**:
- **Private repository** (`d--GITHUB-SYMMETRA`): Contains full source code, engine, and build infrastructure
- **Public repository** (`github.com/JvanDyk/symmetra`): Contains **only compiled binaries**, no source code
- **Version management**: Version is the single source of truth in `engine/CMakeLists.txt`

## Release Checklist

### 1. Update Version

Update the version in `engine/CMakeLists.txt`:

```cmake
project(sym VERSION 1.0.1)
```

Then update the version in all packaging scripts:
- `packaging/windows/create-zip.ps1` → `$Version = "1.0.1"`
- `packaging/windows/create-msi.ps1` → `$Version = "1.0.1"`
- `packaging/linux/create-deb.sh` → `VERSION="1.0.1"`
- `packaging/windows/winget/*.yaml` → `PackageVersion: 1.0.1`
- `packaging/linux/snap/snapcraft.yaml` → `version: 1.0.1`

### 2. Verify Build and Tests

```bash
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 8
ctest -C Release --output-on-failure
```

All tests must pass (target: 100% pass rate).

### 3. Create Version Tag

Tag the commit with the version:

```bash
git tag v1.0.1
git push origin v1.0.1
```

This triggers the automated CI/CD pipeline.

## Automated Release Pipeline

The `.github/workflows/release.yml` workflow executes automatically on version tag push:

### Stage 1: Build (Parallel)
- **Windows**: Builds `sym.exe` (x64) on Windows Server 2022
- **Linux**: Builds `sym` (x64) on Ubuntu 22.04

Both use CUDA Toolkit 12.6 with NVRTC compiler.

### Stage 2: Package (Parallel)

#### Windows Packaging (`package-windows` job)
Creates three distribution formats:

1. **ZIP Package** (`symmetra-{version}-windows-x64.zip`)
   - Contains: `sym.exe`, `install.ps1`, `README.md`
   - Use case: Portable installation, manual PATH configuration
   - Built by: `packaging/windows/create-zip.ps1`

2. **MSI Installer** (`symmetra-{version}-windows-x64.msi`)
   - Features: 
     - Automatic PATH configuration
     - Start Menu shortcuts
     - User-scoped installation (no admin required)
     - Can be distributed via Windows Update
   - Use case: Standard Windows installation
   - Built by: `packaging/windows/create-msi.ps1` + WiX v4 toolset
   - Configuration: `packaging/windows/symmetra.wxs`

3. **Windows Package Manager (winget) Manifests**
   - Allows: `winget install symmetra.sym`
   - Files:
     - `packaging/windows/winget/symmetra.sym.yaml` — Package metadata
     - `packaging/windows/winget/symmetra.sym.installer.yaml` — Installer details
     - `packaging/windows/winget/symmetra.sym.locale.en-US.yaml` — Localization & description
   - Publishing: Submit manifests to [Windows Package Manager Community Repository](https://github.com/microsoft/winget-pkgs)

#### Linux Packaging (`package-linux` job)
Creates three distribution formats:

1. **Debian Package** (`symmetra-{version}-linux-x64.deb`)
   - Targets: Debian, Ubuntu 20.04+
   - Features:
     - Installs to `/usr/local/bin/sym`
     - Post-install hook shows installation messages
     - Depends: `libc6 >= 2.31`
   - Install: `sudo apt install ./symmetra-1.0.1-linux-x64.deb`
   - Built by: `packaging/linux/create-deb.sh`

2. **Red Hat Package** (`symmetra-{version}-linux-x64.rpm`)
   - Targets: Fedora, RHEL 8+
   - Features:
     - Installs to `/usr/local/bin/sym`
     - Pre/post-install hooks
     - Requires: `glibc >= 2.31`
   - Install: `sudo dnf install ./symmetra-1.0.1-linux-x64.rpm`
   - Built by: `packaging/linux/create-rpm.sh`

3. **Snap Package** (`symmetra-{version}-linux-x64.snap`)
   - Targets: Universal Linux (all distributions)
   - Features:
     - Classic confinement (access to GPU via CUDA driver)
     - Automatic updates via Snap Store
     - Works on Ubuntu, Fedora, Arch, etc.
   - Install: `snap install --classic symmetra`
   - Built by: `packaging/linux/create-snap.sh`
   - Configuration: `packaging/linux/snap/snapcraft.yaml`

4. **Raw Binary** (`sym-linux-x64`)
   - For users who prefer to manage installation manually
   - Can be placed anywhere in PATH

### Stage 3: Publish (Centralized)

The `publish` job:

1. **Downloads all artifacts** from Windows and Linux build/package stages
2. **Generates SHA256SUMS.txt** for all distribution files
3. **Creates GitHub Release** on `github.com/JvanDyk/symmetra`:
   - Tag: `v1.0.1`
   - Release name: `Symmetra 1.0.1`
   - Assets: All distribution files + SHA256SUMS.txt
   - Body: Installation instructions, download links, requirements
4. **Updates public repository version.json** (if `PUBLIC_REPO_TOKEN` is configured):
   - Location: `public/version.json`
   - Contains: Version, release date, SHA256 hashes, download URLs
   - Used by website for dynamic version display

## Distribution Matrix

| Platform | Format | Installation Method | Target Audience |
|----------|--------|---------------------|-----------------|
| Windows | ZIP | Manual + `install.ps1` | Power users, portable setups |
| Windows | MSI | GUI installer | End users, enterprise |
| Windows | winget | `winget install symmetra.sym` | Dev environment managers |
| Linux | .deb | `apt install` | Debian/Ubuntu users |
| Linux | .rpm | `dnf install` | Fedora/RHEL users |
| Linux | .snap | `snap install` | Universal Linux users |
| Linux | Binary | Manual PATH | Custom deployments |

## Installation Methods Summary

### Windows

1. **PowerShell Script** (recommended for users):
   ```powershell
   iwr https://symmetra.dev/install.ps1 | iex
   ```
   - Downloads latest binary from GitHub releases
   - Adds to user PATH
   - Minimal overhead

2. **MSI Installer**:
   - Download from GitHub releases
   - Double-click and follow wizard
   - Automatically configures PATH + Start Menu

3. **Windows Package Manager**:
   ```powershell
   winget install symmetra.sym
   ```
   - Requires winget (Windows 10 1709+)
   - Automatic updates

### Linux

1. **Bash Script** (recommended for users):
   ```bash
   curl -fsSL https://symmetra.dev/install.sh | bash
   ```
   - Downloads latest binary from GitHub releases
   - Adds to `~/.symmetra/bin`
   - Configures shell PATH

2. **Package Manager**:
   ```bash
   # Ubuntu/Debian
   sudo apt install symmetra
   
   # Fedora/RHEL
   sudo dnf install symmetra
   
   # Universal
   snap install --classic symmetra
   ```
   - Repository-based installation
   - Automatic updates

## File Structure

```
packaging/
├── windows/
│   ├── create-zip.ps1           # ZIP builder
│   ├── create-msi.ps1           # MSI builder (requires WiX)
│   ├── symmetra.wxs             # WiX MSI configuration
│   └── winget/
│       ├── symmetra.sym.yaml
│       ├── symmetra.sym.installer.yaml
│       └── symmetra.sym.locale.en-US.yaml
└── linux/
    ├── create-deb.sh            # Debian package builder
    ├── create-rpm.sh            # Red Hat package builder
    ├── create-snap.sh           # Snap package builder
    └── snap/
        └── snapcraft.yaml       # Snap configuration

.github/workflows/
└── release.yml                  # Automated CI/CD pipeline

public/
├── install.ps1                  # Windows installation script
├── install.sh                   # Linux installation script
├── version.json                 # Dynamic version metadata
└── docs/
    └── getting-started.html     # Installation guide
```

## Security & Verification

### SHA256 Verification

After downloading, verify integrity:

```bash
# Download SHA256SUMS.txt from release
sha256sum -c SHA256SUMS.txt
```

### Code Signing (Future)

The CI/CD pipeline is ready to support code signing:
- Windows: Authenticode signing for .msi and .exe
- Linux: GPG signing for packages
- Configuration: Add signing credentials to GitHub Secrets

## Troubleshooting

### Windows Build Issues

If MSI build fails:
1. Verify WiX Toolset is installed: `wix --version`
2. Check sym.exe exists in build directory
3. Ensure PowerShell execution policy allows scripts

### Linux Build Issues

If package build fails:
1. Verify build tools: `dpkg-dev`, `rpm`, `snapcraft`
2. Check `sym` binary is executable: `chmod +x sym`
3. Ensure glibc check passes (Ubuntu 20.04+)

### Release Workflow Stuck

1. Check GitHub Actions logs: https://github.com/JvanDyk/symmetra/actions
2. Verify tag was pushed: `git tag | grep v1.0.1`
3. Check workflow syntax: `yamllint .github/workflows/release.yml`

## Recovery Procedures

### Delete Failed Release

```bash
# Delete local tag
git tag -d v1.0.1

# Delete remote tag
git push origin --delete v1.0.1
```

Then fix the issue and re-tag:

```bash
git tag v1.0.1
git push origin v1.0.1
```

### Re-trigger Workflow

If CI/CD workflow needs to re-run after tag is already created:

```bash
# Push an empty commit
git commit --allow-empty -m "Trigger CI/CD"
git push
```

Then re-tag with the same version (requires deleting first).

## Notes

- Source code is **never** published to the public repository
- Public repo (`JvanDyk/symmetra`) contains only compiled binaries and installation scripts
- Version is single source of truth: `engine/CMakeLists.txt` → tag → release
- All packages include SHA256 checksums for integrity verification
- Installation scripts automatically download binaries from public GitHub releases
- Website is served from `public/` directory of public repository

---

**Last Updated**: 2026-05-25  
**Release Version**: 1.0.0
