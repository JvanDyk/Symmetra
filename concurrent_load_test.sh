#!/bin/bash
# Concurrent load test - measures real RPS with simultaneous connections
# Usage: ./concurrent_load_test.sh <port> <concurrent_connections> <duration_seconds>

PORT=${1:-8010}
CONCURRENT=${2:-20}
DURATION=${3:-15}

echo "=========================================="
echo "Concurrent Load Test"
echo "Port: $PORT"
echo "Concurrent connections: $CONCURRENT"
echo "Duration: $DURATION seconds"
echo "=========================================="

# Wait for server ready
echo "Waiting for server..."
for i in {1..30}; do
    if curl -s "http://localhost:$PORT/" > /dev/null 2>&1; then
        echo "Server ready"
        break
    fi
    sleep 1
done

# Warm up - send a few requests
echo "Warm-up: 5 requests..."
for i in {1..5}; do
    curl -s "http://localhost:$PORT/" > /dev/null 2>&1 &
done
wait

# Measure
echo "Starting load test..."
START=$(date +%s%N)
COUNT=0
PID_LIST=""

deadline=$(( $(date +%s) + DURATION ))

# Launch concurrent connections
while [ $(date +%s) -lt $deadline ]; do
    for i in $(seq 1 $CONCURRENT); do
        curl -s "http://localhost:$PORT/" > /dev/null 2>&1 &
        ((COUNT++))
    done
    sleep 0.1  # Small delay to avoid spawning too fast
done

# Wait for all to complete
wait

END=$(date +%s%N)
ELAPSED_NS=$(( END - START ))
ELAPSED_MS=$(( ELAPSED_NS / 1000000 ))
ELAPSED_S=$(( ELAPSED_MS / 1000 ))

# Calculate RPS
if [ $ELAPSED_MS -gt 0 ]; then
    RPS=$(( (COUNT * 1000) / ELAPSED_MS ))
else
    RPS=0
fi

echo ""
echo "=========================================="
echo "Results:"
echo "Total requests: $COUNT"
echo "Elapsed time: ${ELAPSED_S}s (${ELAPSED_MS}ms)"
echo "RPS: $RPS"
echo "=========================================="
