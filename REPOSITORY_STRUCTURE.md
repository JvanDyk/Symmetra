# Repository Structure

This document describes the structure of the public Symmetra repository at `github.com/JvanDyk/Symmetra`.

## Overview

```
github.com/JvanDyk/Symmetra/
├── README.md                    # Quick start and overview
├── LICENSE                      # MIT License
├── CHANGELOG.md                 # Version history
├── SECURITY.md                  # Security model and disclosure
├── CONTRIBUTING.md              # Contribution guidelines
├── REPOSITORY_STRUCTURE.md      # This file
├── .gitignore                   # Git ignore rules
│
├── .github/
│   └── workflows/
│       └── release.yml          # CI/CD automation (builds, packages, publishes)
│
├── scripts/
│   ├── install.ps1              # Windows PowerShell installer
│   └── install.sh               # Linux Bash installer
│
├── docs/
│   └── INSTALLATION.md          # Detailed installation guide
│
└── .github/                     # GitHub-specific configuration
    └── ISSUE_TEMPLATE/          # Issue templates
        ├── bug_report.md
        └── feature_request.md
```

## File Descriptions

### Root Files

| File | Purpose |
|------|---------|
| `README.md` | Quick start, features, system requirements, links to documentation |
| `LICENSE` | MIT License (standard open-source license) |
| `CHANGELOG.md` | Version history with features and changes per release |
| `SECURITY.md` | Security model, vulnerability disclosure, supported versions |
| `CONTRIBUTING.md` | How to contribute, code of conduct, reporting issues |
| `.gitignore` | Git rules to exclude binaries, build artifacts, OS files |

### GitHub Workflows

**`.github/workflows/release.yml`** — Automated CI/CD pipeline

Triggers on version tags (`v*`) and:
1. **Builds** binaries on Windows and Linux (parallel)
2. **Packages** into ZIP, MSI, deb, rpm, snap formats (parallel)
3. **Publishes** GitHub Release with all 7 assets + SHA256SUMS.txt

### Installation Scripts

**`scripts/install.ps1`** — Windows PowerShell installer
- Downloads latest binary from GitHub releases
- Adds `sym.exe` to user PATH
- Verifies SHA256 checksum
- Minimal dependencies (PowerShell 5.1+)

**`scripts/install.sh`** — Linux Bash installer
- Downloads latest binary from GitHub releases
- Installs to `~/.symmetra/bin`
- Adds to `$PATH` in shell rc file
- Checks NVIDIA driver and CUDA version

### Documentation

**`docs/INSTALLATION.md`** — Detailed installation guide
- Prerequisites (GPU, driver, OS requirements)
- Installation methods for each platform
- Verification steps
- Troubleshooting common issues
- Binary verification with SHA256

## What's NOT in This Repository

⚠️ **Source Code** — The Symmetra engine source code is proprietary  
⚠️ **Build System** — CMake, CUDA compiler config, build scripts  
⚠️ **Test Suite** — Internal tests and test data  
⚠️ **Engine Internals** — Turing machine implementation details  

## Release Process

When a new version is released:

1. **Preparation** (private repo)
   - Update version in `engine/CMakeLists.txt`
   - Run full test suite
   - Create release notes

2. **Tagging** (private repo)
   ```bash
   git tag v1.0.1
   git push origin v1.0.1
   ```

3. **Automation** (GitHub Actions on public repo)
   - `.github/workflows/release.yml` triggers
   - Builds Windows and Linux binaries in parallel
   - Creates 7 distribution packages
   - Generates SHA256SUMS.txt
   - Creates GitHub Release with all assets

4. **Distribution**
   - Users download from GitHub Releases or via package managers
   - Installation scripts curl binary from latest release
   - Package managers pull `.deb`, `.rpm`, `.snap` from releases

## Binary Artifacts

GitHub Releases contain these artifacts:

| Artifact | Platform | Format | Use Case |
|----------|----------|--------|----------|
| `symmetra-{version}-windows-x64.zip` | Windows | ZIP archive | Portable installation |
| `symmetra-{version}-windows-x64.msi` | Windows | MSI installer | GUI installation, enterprise |
| `symmetra-{version}-windows-x64.exe` | Windows | Single binary | Manual PATH setup |
| `symmetra-{version}-linux-x64.deb` | Linux | Debian package | Ubuntu/Debian |
| `symmetra-{version}-linux-x64.rpm` | Linux | Red Hat package | Fedora/RHEL |
| `symmetra-{version}-linux-x64.snap` | Linux | Snap package | Universal Linux |
| `sym-linux-x64` | Linux | Raw binary | Manual installation |
| `SHA256SUMS.txt` | All | Text file | Integrity verification |

## Version Numbering

Symmetra uses [Semantic Versioning](https://semver.org/):

- `MAJOR.MINOR.PATCH` (e.g., `1.0.0`, `1.0.1`, `1.1.0`)
- **MAJOR** — Breaking changes
- **MINOR** — New features (backward compatible)
- **PATCH** — Bug fixes and security patches

Tags are created as `v{version}` (e.g., `v1.0.0`).

## Security Considerations

- **No source code exposure** — binaries only
- **Binary signing** — SHA256 checksums for all artifacts
- **Release verification** — users can verify checksums against `SHA256SUMS.txt`
- **Responsible disclosure** — security issues reported to [security@symmetra.dev](mailto:security@symmetra.dev)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on:
- Reporting bugs
- Requesting features
- Improving documentation
- Security vulnerability disclosure

---

**Last Updated:** 2026-05-25
