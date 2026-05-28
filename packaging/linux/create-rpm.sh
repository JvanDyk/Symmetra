#!/usr/bin/env bash
# Creates .rpm package for Symmetra
# Usage: ./create-rpm.sh -v 1.0.0 -b ./sym

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

echo "📦 Creating .rpm package v$VERSION"

# Create spec file
SPEC_FILE=$(mktemp)
trap "rm -f $SPEC_FILE" EXIT

cat > "$SPEC_FILE" << 'SPEC_EOF'
Name:           symmetra
Version:        %{version}
Release:        1%{?dist}
Summary:        GPU-accelerated TypeScript server runtime
License:        MIT
URL:            https://github.com/JvanDyk/symmetra

%description
Compile TypeScript directly to CUDA. Execute on GPU.
Get 3.2M RPS peak throughput with async callbacks and lock-free queues.

%prep
# Nothing to prepare

%install
mkdir -p %{buildroot}/usr/local/bin
install -m 0755 %{binary} %{buildroot}/usr/local/bin/sym

%files
/usr/local/bin/sym

%post
echo "Symmetra installed successfully!"
echo "Run 'sym' to get started"

%changelog
* Mon May 29 2026 Symmetra <info@symmetra.io> - %{version}-1
- Initial release
SPEC_EOF

# Replace placeholders
sed -i "s|%{version}|$VERSION|g" "$SPEC_FILE"
sed -i "s|%{binary}|$BINARY_PATH|g" "$SPEC_FILE"

# Build RPM
if command -v rpmbuild &>/dev/null; then
    rpmbuild -bb "$SPEC_FILE" --define "_rpmdir ."
    RPM_FILE=$(find . -maxdepth 2 -name "symmetra-$VERSION-1*.x86_64.rpm" 2>/dev/null | head -1)
    
    if [[ -f "$RPM_FILE" ]]; then
        mv "$RPM_FILE" "symmetra-v$VERSION-linux-x64.rpm"
        SIZE=$(du -h "symmetra-v$VERSION-linux-x64.rpm" | cut -f1)
        echo "✅ Created: symmetra-v$VERSION-linux-x64.rpm ($SIZE)"
        
        HASH=$(sha256sum "symmetra-v$VERSION-linux-x64.rpm" | cut -d' ' -f1)
        echo "SHA256: $HASH"
        echo ""
        echo "✓ Package ready for release"
    fi
else
    echo "❌ rpmbuild not found. Install with: sudo dnf install rpm-build"
    exit 1
fi
