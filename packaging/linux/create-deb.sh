#!/usr/bin/env bash
# Creates .deb package for Symmetra
# Usage: ./create-deb.sh -v 1.0.0 -b ./sym

set -euo pipefail

VERSION=""
BINARY_PATH=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version) VERSION="$2"; shift 2 ;;
        -b|--binary) BINARY_PATH="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ -z "$VERSION" ]] || [[ -z "$BINARY_PATH" ]]; then
    echo "Usage: $0 -v VERSION -b BINARY_PATH"
    exit 1
fi

if [[ ! -f "$BINARY_PATH" ]]; then
    echo "❌ Binary not found: $BINARY_PATH"
    exit 1
fi

echo "📦 Creating .deb package v$VERSION"

# Create package structure
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

DEB_DIR="$TEMP_DIR/symmetra-$VERSION"
mkdir -p "$DEB_DIR/DEBIAN" "$DEB_DIR/usr/local/bin"

# Copy binary
cp "$BINARY_PATH" "$DEB_DIR/usr/local/bin/sym"
chmod +x "$DEB_DIR/usr/local/bin/sym"

# Create control file
cat > "$DEB_DIR/DEBIAN/control" << EOF
Package: symmetra
Version: $VERSION
Architecture: amd64
Maintainer: Symmetra <info@symmetra.io>
Depends: libc6 (>= 2.31)
Description: GPU-accelerated TypeScript server runtime
 Compile TypeScript directly to CUDA. Execute on GPU. 
 Get 3.2M RPS peak throughput with async callbacks.
Homepage: https://github.com/JvanDyk/symmetra
EOF

# Create postinst script
cat > "$DEB_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Add sym to PATH if not already there
if ! grep -q "/usr/local/bin" /etc/profile 2>/dev/null; then
    echo "export PATH=\"/usr/local/bin:\$PATH\"" >> /etc/profile
fi

echo "Symmetra installed successfully!"
echo "Run 'sym' to get started"
EOF

chmod +x "$DEB_DIR/DEBIAN/postinst"

# Build .deb
dpkg-deb --build "$DEB_DIR" "symmetra-v$VERSION-linux-x64.deb"

SIZE=$(du -h "symmetra-v$VERSION-linux-x64.deb" | cut -f1)
echo "✅ Created: symmetra-v$VERSION-linux-x64.deb ($SIZE)"

HASH=$(sha256sum "symmetra-v$VERSION-linux-x64.deb" | cut -d' ' -f1)
echo "SHA256: $HASH"
echo ""
echo "✓ Package ready for release"
