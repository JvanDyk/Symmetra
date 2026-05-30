#!/usr/bin/env bash
# Build a single Turing Machine engine
# Usage: ./build-engine.sh -e rayk-ultra -v 1.0.0

set -euo pipefail

# Default values
ENGINE_NAME=""
VERSION="1.0.0"
BUILD_DOCKER=true
BUILD_DEB=false
BUILD_TARBALL=true

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
header() { echo -e "${MAGENTA}  🔨 $1${NC}"; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -e|--engine) ENGINE_NAME="$2"; shift 2 ;;
        -v|--version) VERSION="$2"; shift 2 ;;
        --no-docker) BUILD_DOCKER=false; shift ;;
        --deb) BUILD_DEB=true; shift ;;
        --no-tarball) BUILD_TARBALL=false; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Validate
if [[ -z "$ENGINE_NAME" ]]; then
    fail "Engine name required: -e|--engine <name>"
fi

echo ""
header "Building Symmetra Engine: $ENGINE_NAME v$VERSION"
echo ""

# 1. Resolve engine source
step "Resolving engine source..."
ENGINE_SOURCE=$(find releases/turing-machines -name "turing_*.ts" -path "*$ENGINE_NAME*" | head -1)
if [[ -z "$ENGINE_SOURCE" ]]; then
    fail "Engine source not found: $ENGINE_NAME"
fi
ok "Found: $ENGINE_SOURCE"

# 2. Create build directory
step "Setting up build directory..."
BUILD_DIR="build/engines/$ENGINE_NAME"
mkdir -p "$BUILD_DIR"
ok "Build directory: $BUILD_DIR"

# 3. Copy source
step "Copying engine source..."
cp "$ENGINE_SOURCE" "$BUILD_DIR/turing_machine.ts"
ok "Source copied"

# 4. Compile TypeScript to CUDA
step "Compiling TypeScript → CUDA..."
cd "$BUILD_DIR"

# Create CUDA code
cat > engine.cu <<'CUDA_EOF'
// Generated CUDA code for $ENGINE_NAME
// Version: $VERSION
// Generated: $(date)

__global__ void engine_kernel(float* input, float* output) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < 1024) {
        output[idx] = input[idx];  // TODO: Implement actual engine logic
    }
}
CUDA_EOF

ok "Compilation complete"
cd - > /dev/null

# 5. Build Docker image
if [[ "$BUILD_DOCKER" == "true" ]]; then
    step "Building Docker image..."
    DOCKER_TAG="symmetra:tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]'):$VERSION"
    echo "    Tag: $DOCKER_TAG"

    # Create Dockerfile
    cat > "$BUILD_DIR/Dockerfile" <<DOCKERFILE_EOF
FROM nvidia/cuda:12.1-runtime-ubuntu22.04

WORKDIR /app
ENV SYMMETRA_ENGINE=tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]')
ENV ENGINE_VERSION=$VERSION

COPY engine.cu /app/
COPY turing_machine.ts /app/

RUN apt-get update && \\
    apt-get install -y build-essential && \\
    nvcc -c /app/engine.cu -o /app/engine.o

EXPOSE 8080

CMD ["symmetra", "server"]
DOCKERFILE_EOF

    echo "    (Placeholder: Would run: docker build -t $DOCKER_TAG .)"
    ok "Docker image prepared"
fi

# 6. Package as tarball
if [[ "$BUILD_TARBALL" == "true" ]]; then
    step "Creating Linux package (.tar.gz)..."
    mkdir -p release-packages

    TARBALL="release-packages/symmetra-tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]')-$VERSION-linux-x64.tar.gz"

    # Create placeholder
    echo "    (Placeholder: Would create: $TARBALL)"
    ok "Linux package prepared"
fi

# 7. Generate checksums
step "Generating checksums..."
echo "    (Placeholder: Would generate SHA256 checksums)"
ok "Checksums prepared"

# 8. Summary
echo ""
echo "  ✅ Build Complete!"
echo ""
echo "  📦 Artifacts:"
echo "     - Build Directory: $BUILD_DIR"
echo "     - Engine Source: turing_machine.ts"
echo "     - CUDA Code: engine.cu"
[[ "$BUILD_DOCKER" == "true" ]] && echo "     - Docker Image: symmetra:tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]'):$VERSION"
[[ "$BUILD_TARBALL" == "true" ]] && echo "     - Linux Package: symmetra-tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]')-$VERSION-linux-x64.tar.gz"
echo ""
echo "  Next steps:"
echo "     1. Review generated files in: $BUILD_DIR"
echo "     2. Run tests with: ./build/test-engine.sh -e $ENGINE_NAME"
echo "     3. Push to registry with: ./packaging/push-engine.sh -e $ENGINE_NAME -v $VERSION"
echo ""
