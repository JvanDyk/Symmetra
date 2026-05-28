#!/bin/bash

# Symmetra GPU Engine Performance Benchmarking Tool
# Usage: ./benchmark.sh [options]
#   --server <url>      Target server URL (default: http://localhost:8080)
#   --duration <secs>   Test duration per workload (default: 30)
#   --threads <n>       Concurrent threads (default: 32)
#   --size <bytes>      Request payload size (default: 1024)
#   --tls               Enable TLS testing (requires https://)

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Defaults
SERVER="http://localhost:8080"
DURATION=30
THREADS=32
PAYLOAD_SIZE=1024
USE_TLS=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --server) SERVER="$2"; shift 2 ;;
        --duration) DURATION="$2"; shift 2 ;;
        --threads) THREADS="$2"; shift 2 ;;
        --size) PAYLOAD_SIZE="$2"; shift 2 ;;
        --tls) USE_TLS=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo -e "${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Symmetra GPU Engine Performance Benchmark                      ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "Target:     ${YELLOW}$SERVER${NC}"
echo -e "Duration:   ${YELLOW}${DURATION}s${NC} per workload"
echo -e "Threads:    ${YELLOW}${THREADS}${NC} concurrent"
echo -e "Payload:    ${YELLOW}${PAYLOAD_SIZE} bytes${NC}"
echo ""

# Check if server is reachable
echo -n "Checking server connectivity... "
if ! curl -s -o /dev/null -w "%{http_code}" "$SERVER/ping" >/dev/null 2>&1; then
    echo -e "${RED}FAILED${NC}"
    echo "Server not reachable at $SERVER"
    exit 1
fi
echo -e "${GREEN}OK${NC}"
echo ""

# Function to run benchmark
run_benchmark() {
    local name=$1
    local endpoint=$2
    local method=${3:-GET}
    local payload=${4:-}

    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}Test: $name${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

    # Build ab command
    local ab_cmd="ab -t $DURATION -c $THREADS -q"

    if [ "$method" = "POST" ] && [ -n "$payload" ]; then
        ab_cmd="$ab_cmd -p <(echo '$payload')"
    fi

    if [ "$method" = "POST" ]; then
        ab_cmd="$ab_cmd -T application/json"
    fi

    ab_cmd="$ab_cmd $SERVER$endpoint"

    # Run benchmark
    local result=$(eval "$ab_cmd" 2>&1 || true)

    # Extract metrics
    local rps=$(echo "$result" | grep "Requests per second:" | awk '{print $4}' | cut -d. -f1)
    local latency=$(echo "$result" | grep "Time per request:" | head -1 | awk '{print $4}')
    local min_time=$(echo "$result" | grep "min" | awk '{print $2}')
    local max_time=$(echo "$result" | grep "max" | awk '{print $3}')
    local mean_time=$(echo "$result" | grep "mean" | awk '{print $4}')

    # Display results
    echo -e "  ${GREEN}Throughput:${NC}       $rps req/s"
    echo -e "  ${GREEN}Mean latency:${NC}     ${latency}ms"
    if [ -n "$min_time" ]; then
        echo -e "  ${GREEN}Min latency:${NC}      $min_time ms"
        echo -e "  ${GREEN}Max latency:${NC}      $max_time ms"
    fi

    echo ""
}

# Benchmark 1: Simple JSON ping
run_benchmark "Simple JSON route" "/ping" "GET"

# Benchmark 2: POST with body
run_benchmark "POST JSON echo" "/echo" "POST" "{\"message\":\"test\",\"size\":$PAYLOAD_SIZE}"

# Benchmark 3: Array operation
run_benchmark "Array transform" "/transform" "POST" "{\"data\":[$(seq -s ',' 1 100)]}"

# Benchmark 4: Cache-like operation
run_benchmark "Cache lookup" "/cache" "GET"

# Summary
echo ""
echo -e "${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Benchmark Complete                                            ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${YELLOW}📊 Interpretation:${NC}"
echo "  • req/s should be in range 600k-960k for optimal conditions"
echo "  • Actual throughput depends on handler complexity"
echo "  • Latency should remain stable under sustained load"
echo ""
echo -e "${YELLOW}📈 Next steps:${NC}"
echo "  1. Run benchmark after optimization changes"
echo "  2. Compare against baseline numbers above"
echo "  3. Profile GPU utilization: nvidia-smi dmon"
echo "  4. Check for bottlenecks: CPU I/O, memory bandwidth, kernel time"
echo ""
