#!/usr/bin/env bash
# Push a single engine's Docker image to registry
# Usage: ./push-engine.sh -e rayk-ultra -v 1.0.0 -r docker.io

set -euo pipefail

# Default values
ENGINE_NAME=""
VERSION="1.0.0"
REGISTRY="docker.io"
ORG="symmetra"
PUSH_LATEST=true
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
        -e|--engine) ENGINE_NAME="$2"; shift 2 ;;
        -v|--version) VERSION="$2"; shift 2 ;;
        -r|--registry) REGISTRY="$2"; shift 2 ;;
        -o|--org) ORG="$2"; shift 2 ;;
        --no-latest) PUSH_LATEST=false; shift ;;
        --dry-run) DRY_RUN=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ -z "$ENGINE_NAME" ]]; then
    fail "Engine name required: -e|--engine <name>"
fi

echo ""
echo -e "${MAGENTA}  📤 Pushing Engine: $ENGINE_NAME v$VERSION${NC}"
echo ""

# Validate Docker
step "Checking Docker..."
if ! command -v docker &> /dev/null; then
    fail "Docker not found. Please install Docker."
fi
ok "Docker available"

# Build tags
LOCAL_TAG="symmetra:tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]'):$VERSION"
REGISTRY_TAG="$REGISTRY/$ORG/tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]'):$VERSION"
LATEST_TAG="$REGISTRY/$ORG/tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]'):latest"

echo ""
step "Image tags:"
echo "     Local: $LOCAL_TAG"
echo "     Registry: $REGISTRY_TAG"
[[ "$PUSH_LATEST" == "true" ]] && echo "     Latest: $LATEST_TAG"
echo ""

# Check if image exists
step "Checking if image exists..."
if ! docker image inspect "$LOCAL_TAG" &> /dev/null; then
    fail "Image not found: $LOCAL_TAG. Build it first with build-engine.sh"
fi
ok "Image found: $LOCAL_TAG"

# Tag for registry
step "Tagging image for registry..."
if [[ "$DRY_RUN" == "true" ]]; then
    echo "  [DRY-RUN] docker tag $LOCAL_TAG $REGISTRY_TAG"
else
    docker tag "$LOCAL_TAG" "$REGISTRY_TAG"
    ok "Tagged: $REGISTRY_TAG"
fi

# Tag as latest
if [[ "$PUSH_LATEST" == "true" ]]; then
    step "Tagging as latest..."
    if [[ "$DRY_RUN" == "true" ]]; then
        echo "  [DRY-RUN] docker tag $LOCAL_TAG $LATEST_TAG"
    else
        docker tag "$LOCAL_TAG" "$LATEST_TAG"
        ok "Tagged: $LATEST_TAG"
    fi
fi

# Push to registry
echo ""
step "Pushing to registry ($REGISTRY)..."
if [[ "$DRY_RUN" == "true" ]]; then
    echo "  [DRY-RUN] docker push $REGISTRY_TAG"
    [[ "$PUSH_LATEST" == "true" ]] && echo "  [DRY-RUN] docker push $LATEST_TAG"
else
    echo "     (This may take a minute...)"
    docker push "$REGISTRY_TAG"
    ok "Pushed: $REGISTRY_TAG"

    if [[ "$PUSH_LATEST" == "true" ]]; then
        docker push "$LATEST_TAG"
        ok "Pushed: $LATEST_TAG"
    fi
fi

# Summary
echo ""
echo -e "  ${GREEN}✅ Push Complete!${NC}"
echo ""
echo -e "  ${GREEN}📦 Images pushed:${NC}"
echo -e "     - ${GREEN}$REGISTRY_TAG${NC}"
[[ "$PUSH_LATEST" == "true" ]] && echo -e "     - ${GREEN}$LATEST_TAG${NC}"
echo ""
echo -e "  ${CYAN}Registry URL:${NC}"
echo "     $REGISTRY/$ORG/tm-$(echo $ENGINE_NAME | tr '[:upper:]' '[:lower:]')"
echo ""

[[ "$DRY_RUN" == "true" ]] && warn "This was a DRY-RUN. No images were actually pushed."
