#!/bin/bash

TARGET=${1:-"http://localhost:8010"}
DURATION=${2:-10}

echo "Loading test against $TARGET for $DURATION seconds"
echo "Starting server responses timing..."

START=$(date +%s%N)
COUNT=0
SUCCESS=0
ERRORS=0

# Fire requests in background
for i in {1..100}; do
    while true; do
        if curl -s "$TARGET" > /dev/null 2>&1; then
            ((SUCCESS++))
        else
            ((ERRORS++))
        fi
        ((COUNT++))

        NOW=$(date +%s%N)
        ELAPSED=$(( (NOW - START) / 1000000 ))  # ms

        if [ $ELAPSED -gt $((DURATION * 1000)) ]; then
            break 2
        fi
    done &
done

# Wait for all background processes
wait

ELAPSED=$(( ($(date +%s%N) - START) / 1000000000 ))  # seconds
RPS=$(( COUNT / ELAPSED ))

echo ""
echo "=== Load Test Results ==="
echo "Duration: ${ELAPSED}s"
echo "Total Requests: $COUNT"
echo "Successful: $SUCCESS"
echo "Failed: $ERRORS"
echo "RPS: $RPS"
