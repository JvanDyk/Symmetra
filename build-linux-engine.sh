#!/bin/bash
set -e

# Use clean Linux-only PATH
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export LD_LIBRARY_PATH=""

VERSION="1.0.0"
BUILD_DIR="/mnt/d/GITHUB/SYMMETRA/engine/build-linux"
SRC_DIR="/mnt/d/GITHUB/SYMMETRA/engine"
OUT_DIR="/mnt/d/GITHUB/SYMMETRA/release-packages"

echo "=== Installing build tools ==="
sudo apt-get update -qq
sudo apt-get install -y -q cmake build-essential wget

echo "cmake: $(cmake --version | head -1)"
echo "gcc: $(gcc --version | head -1)"

echo ""
echo "=== Installing CUDA 12.4 (Ubuntu 22.04 compatible) ==="
wget -q https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb
sudo apt-get update -qq
sudo apt-get install -y -q cuda-nvcc-12-4 cuda-cudart-dev-12-4 cuda-nvrtc-dev-12-4

export PATH="/usr/local/cuda-12.4/bin:$PATH"
export LD_LIBRARY_PATH="/usr/local/cuda-12.4/lib64"

echo "nvcc: $(nvcc --version | tail -1)"
echo "gcc:  $(gcc --version | head -1)"
echo ""

echo "=== CMake configure ==="
rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S "$SRC_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES=75 \
  -DCMAKE_CUDA_FLAGS="--allow-unsupported-compiler" \
  2>&1 | grep -E "^--|rror:"

echo ""
echo "=== Build ==="
cmake --build "$BUILD_DIR" --parallel $(nproc) 2>&1 | tail -8

[ -f "$BUILD_DIR/sym" ] || { echo "ERROR: sym not found"; exit 1; }
echo "Built: $(du -h $BUILD_DIR/sym | cut -f1)"

echo ""
echo "=== Create packages ==="
mkdir -p "$OUT_DIR"

# Raw binary
cp "$BUILD_DIR/sym" "$OUT_DIR/sym-$VERSION-linux-x64"
chmod +x "$OUT_DIR/sym-$VERSION-linux-x64"
echo "Created: sym-$VERSION-linux-x64"

# .deb — build in /tmp (not NTFS) to avoid permissions issue
DEB="/tmp/symmetra-deb-$$"
rm -rf "$DEB" && mkdir -p "$DEB/DEBIAN" "$DEB/usr/local/bin"
cp "$BUILD_DIR/sym" "$DEB/usr/local/bin/sym"
chmod 755 "$DEB/usr/local/bin/sym"
chmod 755 "$DEB/DEBIAN"
cat > "$DEB/DEBIAN/control" << EOF
Package: symmetra
Version: $VERSION
Section: devel
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.31)
Maintainer: Symmetra <support@symmetra.dev>
Description: GPU-accelerated TypeScript compiler
 Symmetra compiles TypeScript to native CUDA machine code.
 3.1M RPS with sub-20us P99 latency.
EOF
chmod 644 "$DEB/DEBIAN/control"
printf '#!/bin/bash\necho "Symmetra installed. Run: sym --version"\n' > "$DEB/DEBIAN/postinst"
chmod 755 "$DEB/DEBIAN/postinst"
DEB_OUT="/tmp/symmetra-$VERSION-linux-x64.deb"
dpkg-deb --build "$DEB" "$DEB_OUT"
cp "$DEB_OUT" "$OUT_DIR/symmetra-$VERSION-linux-x64.deb"
rm -rf "$DEB" "$DEB_OUT"
echo "Created: symmetra-$VERSION-linux-x64.deb"

echo ""
echo "=== Done ==="
ls -lh "$OUT_DIR"/
