#!/bin/bash
# Quick RPS test - no frills, just measure throughput
# Usage: ./quick_rps_test.sh <port> <duration>

PORT=${1:-8001}
DURATION=${2:-10}

echo "Quick RPS test on port $PORT for $DURATION seconds"

# Wait for server to be ready (compilation can take 10-15 seconds)
echo "Waiting for server startup..."
for i in {1..30}; do
    if curl -s "http://localhost:$PORT/" > /dev/null 2>&1; then
        echo "Server ready after ${i}s"
        break
    fi
    sleep 1
done

START=$(date +%s%N)
COUNT=0

deadline=$(( $(date +%s) + DURATION ))
while [ $(date +%s) -lt $deadline ]; do
    curl -s "http://localhost:$PORT/" > /dev/null 2>&1 &
    ((COUNT++))
    # Limit concurrent processes to avoid overwhelming system
    if [ $((COUNT % 20)) -eq 0 ]; then
        wait
    fi
done

wait
END=$(date +%s%N)

ELAPSED_MS=$(( (END - START) / 1000000 ))
ELAPSED_S=$(( ELAPSED_MS / 1000 ))
RPS=$(( (COUNT * 1000) / (ELAPSED_MS + 1) ))

echo ""
echo "Results:"
echo "--------"
echo "Requests sent: $COUNT"
echo "Time: ${ELAPSED_S}s (${ELAPSED_MS}ms)"
echo "RPS: $RPS (requests/second)"
