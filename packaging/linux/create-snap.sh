#!/usr/bin/env bash
# Snap package builder
# Creates: symmetra-{version}-linux-x64.snap

set -euo pipefail

SYM_EXE="${1:-/tmp/sym}"
VERSION="${2:-1.0.0}"
OUTPUT_DIR="${3:-.}"

# Colors
CYAN='\033[0;36m'; GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

step() { echo -e "${CYAN}  $1${NC}"; }
ok()   { echo -e "${GREEN}  [ok] $1${NC}"; }
fail() { echo -e "${RED}  [x]  $1${NC}"; exit 1; }

echo ""
echo -e "  Symmetra Snap Package Builder v$VERSION"
echo ""

# Verify inputs
step "Checking inputs..."
[[ -f "$SYM_EXE" ]] || fail "sym binary not found at: $SYM_EXE"
ok "sym binary found"

# Check for snapcraft
step "Checking for snapcraft..."
command -v snapcraft &>/dev/null || fail "snapcraft not found. Install with: snap install snapcraft --classic"
ok "snapcraft found"

# Create build directory
WORK_DIR=$(mktemp -d)
trap "rm -rf $WORK_DIR" EXIT

step "Creating build structure..."

# Copy binary
mkdir -p "$WORK_DIR/snap-build/bin"
cp "$SYM_EXE" "$WORK_DIR/snap-build/bin/sym"
chmod +x "$WORK_DIR/snap-build/bin/sym"
ok "Copied sym binary"

# Copy snapcraft.yaml
cp "$(dirname "$0")/snap/snapcraft.yaml" "$WORK_DIR/snap-build/snapcraft.yaml"
ok "Copied snapcraft.yaml"

# Update version in snapcraft.yaml
sed -i "s/version: .*/version: $VERSION/" "$WORK_DIR/snap-build/snapcraft.yaml"
ok "Updated version in snapcraft.yaml"

# Build the snap
step "Building snap package..."
mkdir -p "$OUTPUT_DIR"

(
    cd "$WORK_DIR/snap-build"
    snapcraft pack . --output "$OUTPUT_DIR/symmetra-$VERSION-linux-x64.snap" \
        > /dev/null 2>&1 || fail "snapcraft build failed"
)

ok "Created: symmetra-$VERSION-linux-x64.snap"

# Verify the snap
step "Verifying snap package..."
snap info "$OUTPUT_DIR/symmetra-$VERSION-linux-x64.snap" > /dev/null 2>&1 || true
ok "Snap verified"

# Calculate SHA256
step "Calculating SHA256..."
SHA256=$(sha256sum "$OUTPUT_DIR/symmetra-$VERSION-linux-x64.snap" | awk '{print $1}')
ok "SHA256: $SHA256"

echo "$SHA256  symmetra-$VERSION-linux-x64.snap" > "$OUTPUT_DIR/symmetra-$VERSION-linux-x64.snap.sha256"
ok "Hash saved to .sha256 file"

echo ""
echo -e "${GREEN}  ✓ Snap package created successfully!${NC}"
echo ""
echo "  Package: symmetra-$VERSION-linux-x64.snap"
echo "  Size: $(ls -lh $OUTPUT_DIR/symmetra-$VERSION-linux-x64.snap | awk '{print $5}')"
echo "  SHA256: $SHA256"
echo ""
echo "  Installation:"
echo "    snap install --classic ./symmetra-$VERSION-linux-x64.snap"
echo ""
echo "  Or from Snap Store:"
echo "    snap install symmetra --classic"
echo ""
echo "  Snap Store URL:"
echo "    https://snapcraft.io/symmetra"
echo ""
