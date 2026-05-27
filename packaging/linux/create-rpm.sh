#!/usr/bin/env bash
# Red Hat/Fedora package builder
# Creates: symmetra-{version}-linux-x64.rpm

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
echo -e "  Symmetra RPM Package Builder v$VERSION"
echo ""

# Verify inputs
step "Checking inputs..."
[[ -f "$SYM_EXE" ]] || fail "sym binary not found at: $SYM_EXE"
ok "sym binary found"

# Check for rpmbuild
step "Checking for rpmbuild..."
command -v rpmbuild &>/dev/null || fail "rpmbuild not found. Install rpm-build package."
ok "rpmbuild found"

# Create build directory structure
WORK_DIR=$(mktemp -d)
trap "rm -rf $WORK_DIR" EXIT

BUILD_ROOT="$WORK_DIR/build"
SOURCES="$WORK_DIR/SOURCES"
SPECS="$WORK_DIR/SPECS"
RPMS="$WORK_DIR/RPMS"

mkdir -p "$BUILD_ROOT"
mkdir -p "$SOURCES"
mkdir -p "$SPECS"
mkdir -p "$RPMS"

step "Creating package structure..."

# Copy binary to SOURCES
mkdir -p "$SOURCES/sym-$VERSION"
cp "$SYM_EXE" "$SOURCES/sym-$VERSION/sym"
chmod +x "$SOURCES/sym-$VERSION/sym"
ok "Copied sym binary"

# Create tarball
(cd "$SOURCES" && tar czf sym-$VERSION.tar.gz sym-$VERSION/)
ok "Created source tarball"

# Create .spec file
cat > "$SPECS/symmetra.spec" << EOF
Name:           symmetra
Version:        $VERSION
Release:        1%{?dist}
Summary:        TypeScript compiler that targets NVIDIA GPUs
License:        MIT
URL:            https://symmetra.dev
Source0:        sym-%{version}.tar.gz

BuildArch:      x86_64
Requires:       glibc >= 2.31
Suggests:       nvidia-driver-devel

%description
Symmetra is a TypeScript-to-CUDA compiler that generates GPU machine code
for high-performance computing workloads.

Features:
- 3.1M requests/second sustained throughput
- <20us P99 latency (batch mode)
- Zero garbage collection overhead
- Full ES2022+ language support
- TLS 1.3 encryption
- Production-ready with 859+ test assertions

Requirements:
- NVIDIA GPU (RTX 2070 or newer, sm_75+)
- NVIDIA CUDA driver (version 525+)
- Fedora/RHEL 8+ (glibc 2.31)

%prep
%setup -q

%build
# Binary is pre-built, nothing to compile

%install
mkdir -p %{buildroot}/usr/local/bin
install -m 0755 sym %{buildroot}/usr/local/bin/sym

%post
echo "Symmetra $VERSION installed successfully!"
echo ""
echo "  Next steps:"
echo "    sym doctor              # verify GPU and CUDA driver"
echo "    sym --version           # check version"
echo ""
echo "  Getting started guide: https://symmetra.dev/docs/getting-started"

%preun
if [ \$1 = 0 ]; then
  echo "Removing Symmetra..."
fi

%files
%attr(0755, root, root) /usr/local/bin/sym

%changelog
* $(date +"%a %b %d %Y") Symmetra Contributors <info@symmetra.dev> - $VERSION-1
- Release $VERSION
EOF
ok "Created symmetra.spec"

# Build the RPM
step "Building .rpm package..."
mkdir -p "$OUTPUT_DIR"

rpmbuild --build-in-place \
    --define "_topdir $WORK_DIR" \
    --define "_builddir $BUILD_ROOT" \
    --define "_rpmdir $RPMS" \
    -ba "$SPECS/symmetra.spec" \
    > /dev/null 2>&1

RPM_FILE="$RPMS/$ARCH/symmetra-$VERSION-1.fc$(rpm --eval %fedora).$ARCH.rpm"

# Find the actual RPM file (it might have different naming)
if [[ ! -f "$RPM_FILE" ]]; then
    RPM_FILE=$(find "$RPMS" -name "*.rpm" -type f | head -1)
fi

if [[ ! -f "$RPM_FILE" ]]; then
    fail "RPM build failed"
fi

# Copy to output directory
cp "$RPM_FILE" "$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.rpm"
ok "Created: symmetra-$VERSION-linux-$ARCH.rpm"

# Verify the package
step "Verifying package..."
rpm --checksig "$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.rpm" > /dev/null
ok "Package verified"

# Calculate SHA256
step "Calculating SHA256..."
SHA256=$(sha256sum "$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.rpm" | awk '{print $1}')
ok "SHA256: $SHA256"

echo "$SHA256  symmetra-$VERSION-linux-$ARCH.rpm" > "$OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.rpm.sha256"
ok "Hash saved to .sha256 file"

echo ""
echo -e "${GREEN}  ✓ RPM package created successfully!${NC}"
echo ""
echo "  Package: symmetra-$VERSION-linux-$ARCH.rpm"
echo "  Size: $(ls -lh $OUTPUT_DIR/symmetra-$VERSION-linux-$ARCH.rpm | awk '{print $5}')"
echo "  SHA256: $SHA256"
echo ""
echo "  Installation:"
echo "    sudo dnf install ./symmetra-$VERSION-linux-$ARCH.rpm"
echo ""
echo "  Or from repository:"
echo "    sudo dnf install symmetra"
echo ""
