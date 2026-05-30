#!/usr/bin/env bash
# Build all Turing Machine engines
# Usage: ./build-all-engines.sh -v 1.0.0 --parallel

set -euo pipefail

# Default values
VERSION="1.0.0"
PARALLEL=false
MAX_PARALLEL=4
BUILD_DOCKER=true
BUILD_TARBALL=true
SKIP_FAILURES=false

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# Functions
step() { echo -e "${CYAN}     →  $1${NC}"; }
ok() { echo -e "${GREEN}     ✓  $1${NC}"; }
fail() { echo -e "${RED}     ✗  $1${NC}"; }
warn() { echo -e "${YELLOW}     ⚠  $1${NC}"; }
header() { echo -e "${MAGENTA}  🔨 $1${NC}"; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version) VERSION="$2"; shift 2 ;;
        --parallel) PARALLEL=true; shift ;;
        -j|--jobs) MAX_PARALLEL="$2"; shift 2 ;;
        --no-docker) BUILD_DOCKER=false; shift ;;
        --skip-failures) SKIP_FAILURES=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo ""
echo -e "${MAGENTA}  🚀 Building ALL Symmetra Engines v$VERSION${NC}"
echo ""

# 1. Discover all engines
header "Discovering engines..."
ENGINES_DIR="releases/turing-machines"
if [[ ! -d "$ENGINES_DIR" ]]; then
    fail "Engines directory not found: $ENGINES_DIR"
fi

ENGINES=()
while IFS= read -r -d '' dir; do
    engine_name=$(basename "$dir")
    if [[ -f "$dir"/turing_*.ts ]]; then
        ENGINES+=("$engine_name")
    fi
done < <(find "$ENGINES_DIR" -maxdepth 1 -type d -print0 | tail -z -n +2)

if [[ ${#ENGINES[@]} -eq 0 ]]; then
    fail "No engines found in $ENGINES_DIR/"
fi

ok "Found ${#ENGINES[@]} engines:"
for engine in "${ENGINES[@]}"; do
    echo -e "     - ${CYAN}$engine${NC}"
done
echo ""

# 2. Build engines
header "Building engines..."
MODE=$([ "$PARALLEL" = true ] && echo "Parallel ($MAX_PARALLEL workers)" || echo "Sequential")
step "Mode: $MODE"
echo ""

SUCCESS_COUNT=0
FAILED_COUNT=0
FAILED_ENGINES=()
START_TIME=$(date +%s)

if [[ "$PARALLEL" == "true" ]]; then
    # Parallel build using GNU parallel or xargs
    if command -v parallel &> /dev/null; then
        export -f ok fail warn step header
        export VERSION BUILD_DOCKER BUILD_TARBALL SKIP_FAILURES

        echo "${ENGINES[@]}" | tr ' ' '\n' | parallel -j "$MAX_PARALLEL" \
            "echo '  ⏱️  Building: {}'
            if ./packaging/build-engine.sh -e {} -v $VERSION $([ '$BUILD_DOCKER' = false ] && echo '--no-docker') $([ '$BUILD_TARBALL' = false ] && echo '--no-tarball'); then
                echo '  ✓  {} completed'
                ((SUCCESS_COUNT++))
            else
                echo '  ✗  {} failed'
                ((FAILED_COUNT++))
            fi"
    else
        # Fallback: sequential if parallel not available
        echo "Note: GNU parallel not found, falling back to sequential build"
        PARALLEL=false
    fi
fi

if [[ "$PARALLEL" == "false" ]]; then
    # Sequential build
    for i in "${!ENGINES[@]}"; do
        engine="${ENGINES[$i]}"
        current=$((i + 1))
        total=${#ENGINES[@]}

        echo -e "  ⏱️  Building: ${CYAN}$engine${NC} ($current of $total)"

        if ./packaging/build-engine.sh \
            -e "$engine" \
            -v "$VERSION" \
            $([ "$BUILD_DOCKER" = false ] && echo "--no-docker") \
            $([ "$BUILD_TARBALL" = false ] && echo "--no-tarball"); then
            ((SUCCESS_COUNT++))
            ok "$engine completed"
        else
            ((FAILED_COUNT++))
            FAILED_ENGINES+=("$engine")
            fail "$engine failed"

            if [[ "$SKIP_FAILURES" == "false" ]]; then
                exit 1
            fi
        fi

        echo ""
    done
fi

# 3. Summary
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
DURATION_MINS=$((DURATION / 60))

echo ""
echo -e "  ${MAGENTA}════════════════════════════════════════${NC}"
echo -e "  ${MAGENTA}📊 BUILD SUMMARY${NC}"
echo -e "  ${MAGENTA}════════════════════════════════════════${NC}"
echo ""
echo "     Total Engines: ${#ENGINES[@]}"
echo -e "     ${GREEN}Successful: $SUCCESS_COUNT${NC}"
echo -e "     ${RED}Failed: $FAILED_COUNT${NC}"
echo "     Duration: ${DURATION_MINS}m${DURATION}s"
echo ""

if [[ ${#FAILED_ENGINES[@]} -gt 0 ]]; then
    echo -e "     ${RED}Failed Engines:${NC}"
    for engine in "${FAILED_ENGINES[@]}"; do
        echo -e "       - ${RED}$engine${NC}"
    done
    echo ""
fi

# 4. Artifacts summary
header "Artifacts generated:"
echo ""

if [[ -d "build/engines" ]]; then
    BUILD_COUNT=$(find build/engines -maxdepth 1 -type d | wc -l)
    ((BUILD_COUNT--))  # Subtract the parent directory
    if [[ $BUILD_COUNT -gt 0 ]]; then
        echo "     Build Directories: $BUILD_COUNT"
        find build/engines -maxdepth 1 -type d ! -name "engines" -exec basename {} \; | sort | sed 's/^/       - /'
    fi
fi

if [[ -d "release-packages" ]]; then
    PKG_COUNT=$(find release-packages -maxdepth 1 -name "symmetra-tm-*" | wc -l)
    if [[ $PKG_COUNT -gt 0 ]]; then
        echo "     Packages Created: $PKG_COUNT"
        find release-packages -maxdepth 1 -name "symmetra-tm-*" -exec basename {} \; | sort | sed 's/^/       - /'
    fi
fi

echo ""
echo -e "  ${GREEN}Next steps:${NC}"
echo "     1. Review build artifacts in: ./build/engines/"
echo "     2. Run tests: ./packaging/test-all-engines.sh -v $VERSION"
echo "     3. Push to registry: ./packaging/push-all-engines.sh -v $VERSION"
echo "     4. Generate releases: ./packaging/create-release.sh -v $VERSION"
echo ""

# Exit with appropriate code
if [[ $FAILED_COUNT -gt 0 && "$SKIP_FAILURES" == "false" ]]; then
    exit 1
fi

exit 0
