#!/usr/bin/env bash
# Symmetra installer for Linux
# Usage: curl -fsSL https://symmetra.dev/install.sh | bash

set -euo pipefail

SYMMETRA_VERSION="1.0.0"
INSTALL_DIR="$HOME/.symmetra/bin"
BINARY_URL="https://github.com/YOUR_USERNAME/symmetra/releases/download/v${SYMMETRA_VERSION}/sym-linux-x64"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

step() { echo -e "${CYAN}  $1${NC}"; }
ok()   { echo -e "${GREEN}  [ok] $1${NC}"; }
warn() { echo -e "${YELLOW}  [!]  $1${NC}"; }
fail() { echo -e "${RED}  [x]  $1${NC}"; exit 1; }

echo ""
echo -e "  Symmetra ${SYMMETRA_VERSION} installer"
echo -e "  TypeScript for the GPU"
echo ""

# ── 1. Check OS ──────────────────────────────────────────────────────────────
step "Checking OS..."
if [[ "$(uname -s)" != "Linux" ]]; then
    fail "This script is for Linux. For Windows use: iwr https://symmetra.dev/install.ps1 | iex"
fi
ok "Linux $(uname -r)"

# ── 2. Check architecture ────────────────────────────────────────────────────
ARCH="$(uname -m)"
if [[ "$ARCH" != "x86_64" ]]; then
    fail "Only x86_64 is supported. Detected: $ARCH"
fi

# ── 3. Check for NVIDIA GPU ──────────────────────────────────────────────────
step "Checking for NVIDIA GPU..."
if command -v nvidia-smi &>/dev/null; then
    GPU_INFO=$(nvidia-smi --query-gpu=name,driver_version,compute_cap --format=csv,noheader 2>/dev/null | head -1 || true)
    if [[ -n "$GPU_INFO" ]]; then
        ok "Found: $GPU_INFO"
    else
        warn "nvidia-smi found but no GPU info returned"
    fi
else
    warn "nvidia-smi not found — CUDA driver may not be installed."
    warn "Install CUDA Toolkit 12.x: https://developer.nvidia.com/cuda-downloads"
    warn "(Symmetra bundles NVRTC — only the driver is required at runtime)"
fi

# ── 4. Check CUDA driver ─────────────────────────────────────────────────────
step "Checking CUDA driver version..."
if [[ -f /proc/driver/nvidia/version ]]; then
    DRIVER_VER=$(grep -oP 'NVRM.*?Driver.*?(\d+\.\d+)' /proc/driver/nvidia/version | head -1 || true)
    if [[ -n "$DRIVER_VER" ]]; then
        ok "$DRIVER_VER"
    fi
elif command -v nvidia-smi &>/dev/null; then
    DRIVER_VER=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1 || true)
    ok "Driver: $DRIVER_VER"
else
    warn "Cannot detect CUDA driver — Symmetra requires CUDA 12.x driver (version 525+)"
fi

# ── 5. Check glibc ──────────────────────────────────────────────────────────
step "Checking glibc..."
GLIBC_VER=$(ldd --version 2>&1 | head -1 | grep -oP '\d+\.\d+' | head -1 || echo "0.0")
GLIBC_MAJOR=$(echo "$GLIBC_VER" | cut -d. -f1)
GLIBC_MINOR=$(echo "$GLIBC_VER" | cut -d. -f2)
if (( GLIBC_MAJOR < 2 || (GLIBC_MAJOR == 2 && GLIBC_MINOR < 31) )); then
    warn "glibc 2.31+ recommended (Ubuntu 20.04+). Found: $GLIBC_VER"
else
    ok "glibc $GLIBC_VER"
fi

# ── 6. Download sym ──────────────────────────────────────────────────────────
step "Downloading sym..."
mkdir -p "$INSTALL_DIR"

DEST="$INSTALL_DIR/sym"
if command -v curl &>/dev/null; then
    curl -fsSL --progress-bar "$BINARY_URL" -o "$DEST"
elif command -v wget &>/dev/null; then
    wget -q --show-progress "$BINARY_URL" -O "$DEST"
else
    fail "curl or wget is required to download Symmetra."
fi

chmod +x "$DEST"
ok "Downloaded to $DEST"

# ── 7. Add to PATH ───────────────────────────────────────────────────────────
step "Configuring PATH..."
SHELL_RC=""
if [[ -n "${BASH_VERSION:-}" ]] || [[ "$SHELL" == */bash ]]; then
    SHELL_RC="$HOME/.bashrc"
elif [[ -n "${ZSH_VERSION:-}" ]] || [[ "$SHELL" == */zsh ]]; then
    SHELL_RC="$HOME/.zshrc"
else
    SHELL_RC="$HOME/.profile"
fi

EXPORT_LINE="export PATH=\"\$HOME/.symmetra/bin:\$PATH\""
if ! grep -qF ".symmetra/bin" "$SHELL_RC" 2>/dev/null; then
    echo "" >> "$SHELL_RC"
    echo "# Symmetra" >> "$SHELL_RC"
    echo "$EXPORT_LINE" >> "$SHELL_RC"
    ok "Added PATH entry to $SHELL_RC"
    warn "Run: source $SHELL_RC  (or open a new terminal)"
else
    ok "PATH entry already present in $SHELL_RC"
fi

export PATH="$INSTALL_DIR:$PATH"

# ── 8. Verify ────────────────────────────────────────────────────────────────
step "Verifying installation..."
if "$DEST" version 2>/dev/null; then
    :
else
    warn "Could not run sym — try opening a new terminal and running 'sym doctor'"
fi

echo ""
echo -e "${GREEN}  Symmetra installed successfully!${NC}"
echo ""
echo "  Next steps:"
echo "    sym doctor          # verify GPU, CUDA driver"
echo "    sym ./server.ts     # compile and run your first server"
echo ""
echo "  Docs: https://symmetra.dev/docs/getting-started"
echo ""
