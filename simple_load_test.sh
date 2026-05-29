#!/bin/bash

TARGET=${1:-"http://localhost:8010"}
DURATION=${2:-10}

echo "Load test: $TARGET for $DURATION seconds"

START=$(date +%s)
COUNT=0

while [ $(( $(date +%s) - START )) -lt $DURATION ]; do
    curl -s "$TARGET" > /dev/null 2>&1
    ((COUNT++))
done

ELAPSED=$(( $(date +%s) - START ))
RPS=$(( COUNT / ELAPSED ))

echo "Requests: $COUNT"
echo "Duration: ${ELAPSED}s"
echo "RPS: $RPS"
