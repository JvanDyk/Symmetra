#!/usr/bin/env bash
# Symmetra installer for Linux
# Usage: curl -fsSL https://symmscript.com/install.sh | bash

set -euo pipefail

SYMMETRA_VERSION="1.0.0"
INSTALL_DIR="$HOME/.symmetra/bin"
GITHUB_REPO="JvanDyk/symmetra"
RELEASE_BASE="https://github.com/${GITHUB_REPO}/releases/download/v${SYMMETRA_VERSION}"
DEB_URL="${RELEASE_BASE}/symmetra-v${SYMMETRA_VERSION}-linux-x64.deb"
RPM_URL="${RELEASE_BASE}/symmetra-v${SYMMETRA_VERSION}-linux-x64.rpm"
BIN_URL="${RELEASE_BASE}/sym"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

step() { echo -e "${CYAN}  →  $1${NC}"; }
ok()   { echo -e "${GREEN}  ✓  $1${NC}"; }
warn() { echo -e "${YELLOW}  ⚠  $1${NC}"; }
fail() { echo -e "${RED}  ✗  $1${NC}"; exit 1; }

echo ""
echo -e "  ${BOLD}Symmetra ${SYMMETRA_VERSION}${NC}  ·  TypeScript for the GPU"
echo ""

# ── 1. OS check ────────────────────────────────────────────────────────────────
step "Checking OS..."
[[ "$(uname -s)" == "Linux" ]] || fail "Linux only. Windows: iwr https://symmetra.dev/install.ps1 | iex"
ok "Linux $(uname -r)"

# ── 2. Architecture ────────────────────────────────────────────────────────────
ARCH="$(uname -m)"
[[ "$ARCH" == "x86_64" ]] || fail "Only x86_64 is supported. Detected: $ARCH"

# ── 3. NVIDIA GPU ──────────────────────────────────────────────────────────────
step "Checking for NVIDIA GPU..."
if command -v nvidia-smi &>/dev/null; then
    GPU_INFO=$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -1 || true)
    [[ -n "$GPU_INFO" ]] && ok "$GPU_INFO" || warn "nvidia-smi found but returned no GPU info"
else
    warn "nvidia-smi not found — CUDA driver may not be installed"
    warn "Install from: https://developer.nvidia.com/cuda-downloads"
fi

# ── 4. CUDA driver ─────────────────────────────────────────────────────────────
step "Checking CUDA driver..."
if command -v nvidia-smi &>/dev/null; then
    DRIVER=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null | head -1 || echo "")
    [[ -n "$DRIVER" ]] && ok "Driver $DRIVER" || warn "Could not read driver version"
else
    warn "Symmetra requires CUDA driver 525+ (nvidia-smi not found)"
fi

# ── 5. glibc ───────────────────────────────────────────────────────────────────
step "Checking glibc..."
GLIBC_VER=$(ldd --version 2>&1 | head -1 | grep -oP '\d+\.\d+' | head -1 || echo "0.0")
IFS='.' read -r GMAJ GMIN <<< "$GLIBC_VER"
if (( GMAJ < 2 || (GMAJ == 2 && GMIN < 31) )); then
    warn "glibc 2.31+ recommended (Ubuntu 20.04+). Found: $GLIBC_VER"
else
    ok "glibc $GLIBC_VER"
fi

# ── 6. Download ────────────────────────────────────────────────────────────────
step "Installing Symmetra ${SYMMETRA_VERSION}..."
mkdir -p "$INSTALL_DIR"
DEST="$INSTALL_DIR/sym"

# Try package manager first
INSTALLED=false
if command -v dpkg &>/dev/null; then
    step "Attempting apt installation..."
    if sudo apt-get update -qq && sudo apt-get install -y "$DEB_URL" 2>/dev/null; then
        ok "Installed via apt"
        INSTALLED=true
    fi
elif command -v rpm &>/dev/null; then
    step "Attempting rpm installation..."
    if sudo rpm -i "$RPM_URL" 2>/dev/null; then
        ok "Installed via rpm"
        INSTALLED=true
    fi
fi

# Fall back to direct binary download
if [[ "$INSTALLED" != "true" ]]; then
    step "Downloading binary..."
    HTTP_STATUS=0
    if command -v curl &>/dev/null; then
        HTTP_STATUS=$(curl -fsSL --write-out "%{http_code}" "$BIN_URL" -o "$DEST" 2>/dev/null || echo "000")
    elif command -v wget &>/dev/null; then
        wget -q "$BIN_URL" -O "$DEST" 2>/dev/null && HTTP_STATUS=200 || HTTP_STATUS=000
    else
        fail "curl or wget required"
    fi

    if [[ "$HTTP_STATUS" != "200" ]] || [[ ! -s "$DEST" ]]; then
        rm -f "$DEST"
        echo ""
        warn "Download failed."
        echo ""
        echo -e "  ${BOLD}Manual install:${NC}"
        echo "    1. Download from:"
        echo "       https://github.com/${GITHUB_REPO}/releases"
        echo "    2. Place binary at: $INSTALL_DIR/sym"
        echo "    3. Run: chmod +x $INSTALL_DIR/sym"
        echo ""
        echo "  Docs: https://symmscript.com/docs/getting-started"
        echo ""
        exit 1
    fi

    chmod +x "$DEST"
    ok "Downloaded to $DEST"
fi

# ── 7. PATH ────────────────────────────────────────────────────────────────────
step "Configuring PATH..."
if   [[ "$SHELL" == */zsh  ]]; then RC="$HOME/.zshrc"
elif [[ "$SHELL" == */bash ]]; then RC="$HOME/.bashrc"
else RC="$HOME/.profile"; fi

EXPORT_LINE="export PATH=\"\$HOME/.symmetra/bin:\$PATH\""
if ! grep -qF ".symmetra/bin" "$RC" 2>/dev/null; then
    { echo ""; echo "# Symmetra"; echo "$EXPORT_LINE"; } >> "$RC"
    ok "Added PATH to $RC"
    warn "Run: source $RC  (or open a new terminal)"
else
    ok "PATH already set in $RC"
fi
export PATH="$INSTALL_DIR:$PATH"

# ── 8. Verify ──────────────────────────────────────────────────────────────────
step "Verifying..."
if "$DEST" version 2>/dev/null; then
    :
else
    warn "Could not run sym — open a new terminal and run: sym doctor"
fi

echo ""
echo -e "${GREEN}  Symmetra installed!${NC}"
echo ""
echo "  sym doctor          # verify GPU + CUDA driver"
echo "  sym ./server.ts     # compile and launch"
echo "  sym check           # audit package.json compatibility"
echo ""
echo "  Docs: https://symmscript.com/docs/getting-started"
echo ""
