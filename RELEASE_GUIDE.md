# Release Guide for Symmetra Public Repository

This guide explains how to manage releases for the `github.com/JvanDyk/Symmetra` public repository.

## Overview

The public repository is a **binary-only distribution** repository that distributes compiled Symmetra binaries without exposing engine source code.

```
Private Repository (d--GITHUB-SYMMETRA)
    ├── Source code (C++, TypeScript)
    ├── Build system (CMake)
    ├── Tests
    └── Internal documentation
          │
          └──→ Build & Tag: git tag v1.0.1
                  │
                  └──→ GitHub Actions (.github/workflows/release.yml)
                          ├─ Build Windows binary
                        ├─ Build Linux binary
                        ├─ Create packages (ZIP, MSI, deb, rpm, snap)
                        └─ Publish to GitHub Releases on PUBLIC repo
                              │
                              └──→ Public Repository (JvanDyk/Symmetra)
                                    ├── Compiled binaries
                                    ├── Installation scripts
                                    ├── Documentation
                                    └── GitHub Release artifacts
```

## Release Checklist

### Step 1: Prepare in Private Repository

In the private `d--GITHUB-SYMMETRA` repository:

```bash
# 1. Update version in CMakeLists.txt
# Edit: engine/CMakeLists.txt
# Change: project(sym VERSION 1.0.1)

# 2. Update versions in packaging scripts
# - packaging/windows/create-zip.ps1
# - packaging/windows/create-msi.ps1
# - packaging/linux/create-deb.sh
# - packaging/linux/create-rpm.sh
# - packaging/linux/snap/snapcraft.yaml
# - packaging/windows/winget/*.yaml

# 3. Build and test locally
cd engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 8
ctest -C Release --output-on-failure

# Expected: All tests pass (100%)
```

### Step 2: Tag the Release

```bash
# Commit version updates
git add .
git commit -m "chore: bump version to 1.0.1"

# Create semantic version tag
git tag v1.0.1

# Push tag (triggers CI/CD)
git push origin v1.0.1
```

### Step 3: Monitor CI/CD

GitHub Actions automatically runs `.github/workflows/release.yml`:

1. **Build Stage (Parallel)**
   - Windows: Compiles `sym.exe` on Windows Server 2022
   - Linux: Compiles `sym` on Ubuntu 22.04

2. **Package Stage (Parallel)**
   - Windows: Creates ZIP + MSI + winget manifests
   - Linux: Creates deb + rpm + snap + binary

3. **Publish Stage**
   - Downloads all artifacts
   - Generates SHA256SUMS.txt
   - Creates GitHub Release on **public** repository
   - Publishes all 7 distribution files
   - Updates `public/version.json`

### Step 4: Verify Release

Check the GitHub Release on `github.com/JvanDyk/Symmetra/releases`:

✅ Release tag: `v1.0.1`  
✅ Release name: `Symmetra 1.0.1`  
✅ Assets present:
- `symmetra-1.0.1-windows-x64.zip`
- `symmetra-1.0.1-windows-x64.msi`
- `symmetra-1.0.1-linux-x64.deb`
- `symmetra-1.0.1-linux-x64.rpm`
- `symmetra-1.0.1-linux-x64.snap`
- `sym-linux-x64`
- `SHA256SUMS.txt`

✅ Release notes are clear and professional  
✅ `public/version.json` updated (if automatic publishing is configured)

### Step 5: Announce Release

1. **Update website** (`symmetra.dev`) — link to new release
2. **Social media** — announce on Twitter, LinkedIn, etc.
3. **Email users** — notify customers of new version
4. **Documentation** — update links to latest version

## Binary Distribution Methods

Users can install from multiple channels:

### Installation Scripts

**Windows PowerShell:**
```powershell
iwr https://symmetra.dev/install.ps1 | iex
```
- Downloads latest binary from GitHub Releases
- Adds to PATH automatically

**Linux Bash:**
```bash
curl -fsSL https://symmetra.dev/install.sh | bash
```
- Downloads latest binary from GitHub Releases
- Configures PATH

### Package Managers

**Windows:**
```powershell
winget install symmetra.sym
```

**Ubuntu/Debian:**
```bash
sudo apt install symmetra
```

**Fedora/RHEL:**
```bash
sudo dnf install symmetra
```

**Universal (Snap):**
```bash
snap install --classic symmetra
```

### Manual Download

Users can download directly from GitHub Releases:
1. Visit `github.com/JvanDyk/Symmetra/releases`
2. Download desired format (ZIP, MSI, deb, rpm, snap)
3. Verify SHA256 checksum
4. Install or extract

## Public Repository Files

The public repository contains:

```
github.com/JvanDyk/Symmetra/
├── README.md                    # Overview, quick start
├── LICENSE                      # MIT License
├── CHANGELOG.md                 # Version history
├── SECURITY.md                  # Security model
├── CONTRIBUTING.md              # Contribution guidelines
├── REPOSITORY_STRUCTURE.md      # This repo structure
├── RELEASE_GUIDE.md             # This file
├── .gitignore                   # What to exclude
│
├── .github/
│   ├── CODEOWNERS               # Change notifications
│   ├── workflows/
│   │   └── release.yml          # CI/CD automation
│   └── ISSUE_TEMPLATE/
│       ├── bug_report.md
│       └── feature_request.md
│
├── scripts/
│   ├── install.ps1              # Windows installer
│   └── install.sh               # Linux installer
│
└── docs/
    └── INSTALLATION.md          # Detailed install guide
```

## Version Management

### Versioning Scheme

Symmetra uses [Semantic Versioning](https://semver.org/):

```
MAJOR.MINOR.PATCH

1.0.0 → 1.0.1 (patch — bug fixes)
1.0.1 → 1.1.0 (minor — new features)
1.1.0 → 2.0.0 (major — breaking changes)
```

### Tag Format

All releases are tagged with `v` prefix:

```
v1.0.0  ← Initial release
v1.0.1  ← First patch
v1.1.0  ← First minor update
v2.0.0  ← Major version
```

## Security Verification

Users can verify binary integrity:

```bash
# Download SHA256SUMS.txt from release
# Verify all downloaded files
sha256sum -c SHA256SUMS.txt

# Expected output:
# symmetra-1.0.1-windows-x64.zip: OK
# symmetra-1.0.1-windows-x64.msi: OK
# ...
```

## Troubleshooting

### Build Fails

1. Check GitHub Actions logs: https://github.com/JvanDyk/Symmetra/actions
2. Verify tag was pushed: `git tag | grep v1.0.1`
3. Check for YAML syntax errors in workflow
4. Verify all dependencies are available

### Release Not Published

1. Verify public repo token is configured in GitHub Secrets
2. Check `.github/workflows/release.yml` for publish job
3. Review GitHub Actions logs for errors

### Manual Release (If Automation Fails)

```bash
# Create release manually on GitHub
# Upload artifacts from private repo build

# In private repo, after successful build:
# - sym.exe → windows binary
# - sym → linux binary
# - packaging/windows/symmetra-{version}-windows-x64.zip
# - packaging/windows/symmetra-{version}-windows-x64.msi
# - packaging/linux/symmetra-{version}-linux-x64.deb
# - packaging/linux/symmetra-{version}-linux-x64.rpm
# - packaging/linux/symmetra-{version}-linux-x64.snap
# - SHA256SUMS.txt

# Upload to github.com/JvanDyk/Symmetra/releases manually
```

## Supported Versions

| Version | Released | Status | Support Ends |
|---------|----------|--------|--------------|
| 1.0.0 | 2026-05-25 | Latest | 2027-05-25 |

**Security patches** are backported to all supported versions.  
**New features** appear only in the latest version.

## Future Enhancements

- [ ] Automated package manager submissions (winget, apt, dnf)
- [ ] Code signing for Windows executables (Authenticode)
- [ ] GPG signing for Linux packages
- [ ] Hardware-bound licensing
- [ ] Beta/RC release channels

---

**Last Updated:** 2026-05-25  
**Version:** 1.0.0
