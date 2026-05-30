#!/usr/bin/env bash
# Test all built Turing Machine engines
# Usage: ./test-all-engines.sh -v 1.0.0

set -euo pipefail

# Default values
VERSION="1.0.0"
PARALLEL=false
MAX_PARALLEL=4
TEST_PERFORMANCE=true
TEST_CORRECTNESS=true
TEST_DOCKER=false

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
fail() { echo -e "${RED}     ✗  $1${NC}"; exit 1; }
warn() { echo -e "${YELLOW}     ⚠  $1${NC}"; }
header() { echo -e "${MAGENTA}  🧪 $1${NC}"; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version) VERSION="$2"; shift 2 ;;
        --parallel) PARALLEL=true; shift ;;
        -j|--jobs) MAX_PARALLEL="$2"; shift 2 ;;
        --no-performance) TEST_PERFORMANCE=false; shift ;;
        --no-correctness) TEST_CORRECTNESS=false; shift ;;
        --docker) TEST_DOCKER=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo ""
echo -e "${MAGENTA}  🧪 Testing ALL Engines v$VERSION${NC}"
echo ""

# Discover all built engines
header "Discovering engines..."
ENGINES_DIR="build/engines"
if [[ ! -d "$ENGINES_DIR" ]]; then
    fail "No built engines found: $ENGINES_DIR"
fi

ENGINES=()
while IFS= read -r -d '' dir; do
    engine_name=$(basename "$dir")
    ENGINES+=("$engine_name")
done < <(find "$ENGINES_DIR" -maxdepth 1 -type d ! -name "engines" -print0 | sort -z)

if [[ ${#ENGINES[@]} -eq 0 ]]; then
    fail "No engines found in $ENGINES_DIR/"
fi

ok "Found ${#ENGINES[@]} engines:"
for engine in "${ENGINES[@]}"; do
    echo -e "     - ${CYAN}$engine${NC}"
done
echo ""

# Run tests
header "Running tests..."
MODE=$([ "$PARALLEL" = true ] && echo "Parallel ($MAX_PARALLEL workers)" || echo "Sequential")
step "Mode: $MODE"
echo ""

SUCCESS_COUNT=0
FAILED_COUNT=0
FAILED_ENGINES=()
START_TIME=$(date +%s)

if [[ "$PARALLEL" == "true" ]]; then
    if command -v parallel &> /dev/null; then
        export -f ok fail warn step header
        export VERSION TEST_PERFORMANCE TEST_CORRECTNESS TEST_DOCKER

        echo "${ENGINES[@]}" | tr ' ' '\n' | parallel -j "$MAX_PARALLEL" \
            "echo '  ⏱️  Testing: {}'
            if ./packaging/test-engine.sh -e {} -v $VERSION \
                $([ '$TEST_PERFORMANCE' = false ] && echo '--no-performance') \
                $([ '$TEST_CORRECTNESS' = false ] && echo '--no-correctness') \
                $([ '$TEST_DOCKER' = true ] && echo '--docker'); then
                echo '  ✓  {} passed'
            else
                echo '  ✗  {} failed'
            fi"
    else
        echo "Note: GNU parallel not found, falling back to sequential"
        PARALLEL=false
    fi
fi

if [[ "$PARALLEL" == "false" ]]; then
    for i in "${!ENGINES[@]}"; do
        engine="${ENGINES[$i]}"
        current=$((i + 1))
        total=${#ENGINES[@]}

        echo -e "  ⏱️  Testing: ${CYAN}$engine${NC} ($current of $total)"

        if ./packaging/test-engine.sh \
            -e "$engine" \
            -v "$VERSION" \
            $([ "$TEST_PERFORMANCE" = false ] && echo "--no-performance") \
            $([ "$TEST_CORRECTNESS" = false ] && echo "--no-correctness") \
            $([ "$TEST_DOCKER" = true ] && echo "--docker"); then
            ((SUCCESS_COUNT++))
            ok "$engine passed"
        else
            ((FAILED_COUNT++))
            FAILED_ENGINES+=("$engine")
            fail "$engine failed"
        fi

        echo ""
    done
fi

# Summary
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
DURATION_MINS=$((DURATION / 60))

echo ""
echo -e "  ${MAGENTA}════════════════════════════════════════${NC}"
echo -e "  ${MAGENTA}📊 TEST SUMMARY${NC}"
echo -e "  ${MAGENTA}════════════════════════════════════════${NC}"
echo ""
echo "     Total Engines: ${#ENGINES[@]}"
echo -e "     ${GREEN}Passed: $SUCCESS_COUNT${NC}"
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

echo -e "  ${GREEN}Next steps:${NC}"
echo "     1. Review failed engines (if any)"
echo "     2. Push to registry: ./push-all-engines.sh -v $VERSION"
echo "     3. Create release: ./create-release.sh -v $VERSION"
echo ""

exit $([ $FAILED_COUNT -eq 0 ] && echo 0 || echo 1)
