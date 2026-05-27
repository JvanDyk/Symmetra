#!/usr/bin/env bash
# Debian/Ubuntu package builder
# Creates: symmetra-{version}-linux-x64.deb

set -euo pipefail

SYM_EXE="${1:-/tmp/sym}"
VERSION="${2:-1.0.0}"
ARCH="${3:-x86_64}"
OUTPUT_DIR="${4:-.}"

# Colors
CYAN='\033[0;36m'; GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

step() { echo -e "${CYAN}  $1${NC}"; }
ok()   { echo -e "${GREEN}  [ok] $1${NC}"; }
fail() { echo -e "${RED}  [x]  $1${NC}"; exit 1; }

echo ""
echo -e "  Symmetra Debian Package Builder v$VERSION"
echo ""

# Verify inputs
step "Checking inputs..."
[[ -f "$SYM_EXE" ]] || fail "sym binary not found at: $SYM_EXE"
ok "sym binary found"

# Create package directory structure
WORK_DIR=$(mktemp -d)
trap "rm -rf $WORK_DIR" EXIT

PKG_DIR="$WORK_DIR/symmetra-$VERSION"
mkdir -p "$PKG_DIR/usr/local/bin"
mkdir -p "$PKG_DIR/DEBIAN"

step "Creating package structure..."

# Copy binary
cp "$SYM_EXE" "$PKG_DIR/usr/local/bin/sym"
chmod +x "$PKG_DIR/usr/local/bin/sym"
ok "Copied sym to usr/local/bin"

# Create DEBIAN/control file
cat > "$PKG_DIR/DEBIAN/control" << EOF
Package: symmetra
Version: $VERSION
Architecture: amd64
Maintainer: Symmetra Contributors <info@symmetra.dev>
Homepage: https://symmetra.dev
Vcs-Git: https://github.com/JvanDyk/symmetra.git
Vcs-Browser: https://github.com/JvanDyk/symmetra
Priority: optional
Section: devel
Depends: libc6 (>= 2.31)
Suggests: nvidia-driver-535
Description: TypeScript compiler that targets NVIDIA GPUs
 Symmetra is a TypeScript-to-CUDA compiler that generates GPU machine code
 for high-performance computing workloads.
 .
 Features:
  - 3.1M requests/second sustained throughput
  - <20us P99 latency (batch mode)
  - Zero garbage collection overhead
  - Full ES2022+ language support
  - TLS 1.3 encryption
  - Production-ready with 859+ test assertions
 .
 Requirements:
  - NVIDIA GPU (RTX 2070 or newer, sm_75+)
  - NVIDIA CUDA driver (version 525+)
  - Ubuntu 20.04 (glibc 2.31) or later
EOF
ok "Created DEBIAN/control"

# Create postinst script (runs after installation)
cat > "$PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

if [ "$1" = "configure" ]; then
    echo "Symmetra installed successfully!"
    echo ""
    echo "  Next steps:"
    echo "    sym doctor              # verify GPU and CUDA driver"
    echo "    sym --version           # check version"
    echo ""
    echo "  Getting started guide: https://symmetra.dev/docs/getting-started"
fi
EOF
chmod +x "$PKG_DIR/DEBIAN/postinst"
ok "Created postinst hook"

# Create prerm script (runs before uninstallation)
cat > "$PKG_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e

if [ "$1" = "remove" ]; then
    echo "Removing Symmetra..."
fi
EOF
chmod +x "$PKG_DIR/DEBIAN/prerm"
ok "Created prerm hook"

# Build the .deb package
step "Building .deb package..."
mkdir -p "$OUTPUT_DIR"

DEB_FILE="$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.deb"
dpkg-deb --build "$PKG_DIR" "$DEB_FILE"
ok "Created: $(basename $DEB_FILE)"

# Verify the package
step "Verifying package..."
dpkg-deb --info "$DEB_FILE" > /dev/null
ok "Package verified"

# Calculate SHA256
step "Calculating SHA256..."
SHA256=$(sha256sum "$DEB_FILE" | awk '{print $1}')
ok "SHA256: $SHA256"

echo "$SHA256  $(basename $DEB_FILE)" > "$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.deb.sha256"
ok "Hash saved to .sha256 file"

echo ""
echo -e "${GREEN}  ✓ Debian package created successfully!${NC}"
echo ""
echo "  Package: $(basename $DEB_FILE)"
echo "  Size: $(ls -lh $DEB_FILE | awk '{print $5}')"
echo "  SHA256: $SHA256"
echo ""
echo "  Installation:"
echo "    sudo apt install ./$(basename $DEB_FILE)"
echo ""
echo "  Or from repository:"
echo "    sudo apt update && sudo apt install symmetra"
echo ""
