#!/usr/bin/env bash
# Create a GitHub release with packaged engines
# Usage: ./create-release.sh -v 1.0.0 -o JvanDyk -r symmetra

set -euo pipefail

# Default values
VERSION=""
OWNER="JvanDyk"
REPO="symmetra"
RELEASE_NOTES=""
PRE_RELEASE=false
DRY_RUN=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# Functions
step() { echo -e "${CYAN}  →  $1${NC}"; }
ok() { echo -e "${GREEN}  ✓  $1${NC}"; }
fail() { echo -e "${RED}  ✗  $1${NC}"; exit 1; }
warn() { echo -e "${YELLOW}  ⚠  $1${NC}"; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version) VERSION="$2"; shift 2 ;;
        -o|--owner) OWNER="$2"; shift 2 ;;
        -r|--repo) REPO="$2"; shift 2 ;;
        -n|--notes) RELEASE_NOTES="$2"; shift 2 ;;
        --pre-release) PRE_RELEASE=true; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    fail "Version required: -v|--version <version>"
fi

echo ""
echo -e "${MAGENTA}  🚀 Creating Release: v$VERSION${NC}"
echo ""

# Verify gh CLI is installed
step "Checking GitHub CLI..."
if ! command -v gh &> /dev/null; then
    fail "GitHub CLI not found. Install with: sudo apt install gh"
fi
ok "GitHub CLI available"

# Verify we have release artifacts
step "Finding release artifacts..."
PACKAGES_DIR="release-packages"
if [[ ! -d "$PACKAGES_DIR" ]]; then
    fail "No packages directory found: $PACKAGES_DIR"
fi

PACKAGES=$(find "$PACKAGES_DIR" -maxdepth 1 -name "symmetra-tm-*" -type f 2>/dev/null || true)
if [[ -z "$PACKAGES" ]]; then
    fail "No packages found in $PACKAGES_DIR/. Build engines first with build-all-engines.sh"
fi

PACKAGE_COUNT=$(echo "$PACKAGES" | wc -l)
ok "Found $PACKAGE_COUNT artifacts:"
echo "$PACKAGES" | sed 's/^/     - /'
echo ""

# Generate release notes if not provided
if [[ -z "$RELEASE_NOTES" ]]; then
    step "Generating release notes..."
    read -r -d '' RELEASE_NOTES <<'EOF' || true
# Symmetra v{VERSION} Release

## Changes
- Engine release v{VERSION}
- All Turing Machine variants updated

## Installation

### Windows
```powershell
Invoke-WebRequest -Uri 'https://github.com/{OWNER}/{REPO}/releases/download/v{VERSION}/symmetra-{VERSION}-windows-x64.zip' -OutFile 'symmetra.zip'
Expand-Archive -Path 'symmetra.zip' -DestinationPath 'C:\Program Files\Symmetra\'
```

### Linux
```bash
wget https://github.com/{OWNER}/{REPO}/releases/download/v{VERSION}/symmetra-{VERSION}-linux-x64.tar.gz
tar xzf symmetra-{VERSION}-linux-x64.tar.gz -C /usr/local/
```

## Docker
```bash
docker pull {OWNER}/tm-latest:latest
docker run -it {OWNER}/tm-latest:latest
```

## Artifacts

This release includes binaries for Windows, Linux, and Docker images for all Turing Machine variants.

Verify artifact integrity with the provided SHA256SUMS.
EOF

    RELEASE_NOTES="${RELEASE_NOTES//\{VERSION\}/$VERSION}"
    RELEASE_NOTES="${RELEASE_NOTES//\{OWNER\}/$OWNER}"
    RELEASE_NOTES="${RELEASE_NOTES//\{REPO\}/$REPO}"
    ok "Release notes generated"
fi

# Create GitHub release
echo ""
step "Creating GitHub release..."

if [[ "$DRY_RUN" == "true" ]]; then
    echo "  [DRY-RUN] Would create release v$VERSION on $OWNER/$REPO"
    echo "  [DRY-RUN] Pre-release: $PRE_RELEASE"
    echo "  [DRY-RUN] Artifacts to upload: $PACKAGE_COUNT files"
else
    RELEASE_ARGS=(
        "release" "create"
        "v$VERSION"
        "--repo" "$OWNER/$REPO"
        "--title" "v$VERSION"
        "--notes" "$RELEASE_NOTES"
    )

    [[ "$PRE_RELEASE" == "true" ]] && RELEASE_ARGS+=("--prerelease")

    echo "    (Creating release...)"
    gh "${RELEASE_ARGS[@]}"
    ok "Release created: v$VERSION"

    # Upload artifacts
    echo ""
    step "Uploading artifacts..."

    while IFS= read -r package; do
        package_name=$(basename "$package")
        echo "    Uploading: $package_name"
        gh release upload "v$VERSION" "$package" --repo "$OWNER/$REPO"
        ok "Uploaded: $package_name"
    done <<< "$PACKAGES"
fi

# Summary
echo ""
echo -e "  ${GREEN}✅ Release Complete!${NC}"
echo ""
echo -e "  ${GREEN}📦 Release Information:${NC}"
echo -e "     Version: ${CYAN}v$VERSION${NC}"
echo -e "     Repository: ${CYAN}$OWNER/$REPO${NC}"
echo -e "     URL: ${CYAN}https://github.com/$OWNER/$REPO/releases/tag/v$VERSION${NC}"
echo -e "     Pre-Release: ${CYAN}$PRE_RELEASE${NC}"
echo ""
echo -e "  ${GREEN}📊 Artifacts uploaded: $PACKAGE_COUNT${NC}"
echo "$PACKAGES" | sed 's/^/     - /'
echo ""

[[ "$DRY_RUN" == "true" ]] && warn "This was a DRY-RUN. No release was created."

echo -e "  ${GREEN}Next steps:${NC}"
echo "     1. Visit: https://github.com/$OWNER/$REPO/releases/tag/v$VERSION"
echo "     2. Review release notes and artifacts"
echo "     3. Share release announcement"
echo ""
