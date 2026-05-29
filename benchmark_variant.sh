#!/bin/bash
# Benchmark a single Turing machine variant
# Usage: ./benchmark_variant.sh <variant_name> <port> [duration_seconds]

VARIANT=${1:-"LIGHTNING"}
PORT=${2:-8001}
DURATION=${3:-30}

echo "=========================================="
echo "Benchmarking: $VARIANT"
echo "Port: $PORT"
echo "Duration: $DURATION seconds"
echo "=========================================="

# Wait for server to be ready
echo "Waiting for server to start..."
for i in {1..60}; do
    if curl -s "http://localhost:$PORT" > /dev/null 2>&1; then
        echo "✓ Server ready after $((i))s"
        break
    fi
    if [ $i -eq 60 ]; then
        echo "✗ Server failed to start"
        exit 1
    fi
    sleep 1
done

# Warm-up period (10 requests to ensure compiled)
echo "Warm-up: 10 requests..."
for i in {1..10}; do
    curl -s "http://localhost:$PORT" > /dev/null 2>&1
    echo -n "."
done
echo " ✓"

# Measurement period
echo "Measuring throughput for $DURATION seconds..."
START=$(date +%s)
COUNT=0
ERRORS=0

while [ $(( $(date +%s) - START )) -lt $DURATION ]; do
    if curl -s "http://localhost:$PORT" > /dev/null 2>&1; then
        ((COUNT++))
    else
        ((ERRORS++))
    fi
    # Show progress every 10 requests
    if [ $((COUNT % 10)) -eq 0 ]; then
        ELAPSED=$(( $(date +%s) - START ))
        CURRENT_RPS=$(( COUNT / (ELAPSED + 1) ))
        echo "  [$ELAPSED s] $COUNT requests, RPS: $CURRENT_RPS"
    fi
done

ELAPSED=$(( $(date +%s) - START ))
RPS=$(( COUNT / (ELAPSED + 1) ))

echo ""
echo "========== RESULTS =========="
echo "Variant: $VARIANT"
echo "Port: $PORT"
echo "Total Requests: $COUNT"
echo "Successful: $((COUNT - ERRORS))"
echo "Failed: $ERRORS"
echo "Duration: ${ELAPSED}s"
echo "RPS: $RPS"
echo "============================"
