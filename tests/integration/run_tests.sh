#!/usr/bin/env bash
# Integration test runner for Symmetra
#
# Usage: bash tests/integration/run_tests.sh [HOST] [PORT]
# Default: HOST=localhost PORT=8090
#
# Start the server first:
#   sym ./tests/integration/server.ts
# Then in another terminal:
#   bash tests/integration/run_tests.sh

set -euo pipefail

HOST="${1:-localhost}"
PORT="${2:-8090}"
BASE="http://${HOST}:${PORT}"

PASS=0
FAIL=0
ERRORS=()

# ─── Helpers ─────────────────────────────────────────────────────────────────

ok()  { echo "  [PASS] $1"; ((PASS++)); }
fail(){ echo "  [FAIL] $1"; ((FAIL++)); ERRORS+=("$1"); }

# Run a curl test. Args: description, expected_contains, curl_args...
# Checks that the response body contains the expected substring.
check() {
  local desc="$1"; local expect="$2"; shift 2
  local body
  body=$(curl -sf --max-time 10 "$@" 2>/dev/null) || { fail "$desc (curl error)"; return; }
  if echo "$body" | grep -q "$expect"; then
    ok "$desc"
  else
    fail "$desc (got: ${body:0:120})"
  fi
}

# Check status code
check_status() {
  local desc="$1"; local expected_status="$2"; shift 2
  local status
  status=$(curl -s -o /dev/null -w "%{http_code}" --max-time 10 "$@" 2>/dev/null)
  if [ "$status" -eq "$expected_status" ]; then
    ok "$desc"
  else
    fail "$desc (expected $expected_status, got $status)"
  fi
}

# ─── Wait for server ─────────────────────────────────────────────────────────
echo "Waiting for server on ${BASE}..."
for i in {1..30}; do
  curl -sf --max-time 1 "${BASE}/ping" >/dev/null 2>&1 && break
  sleep 1
  if [ $i -eq 30 ]; then echo "ERROR: server did not start"; exit 1; fi
done
echo "Server is ready."
echo ""

# ─── Tests ───────────────────────────────────────────────────────────────────

echo "── Core ────────────────────────────────────────────────────────────────"
check "ping"                           '"ok":true'    "${BASE}/ping"
check "arithmetic: add"                '"add":3'      "${BASE}/arithmetic"
check "arithmetic: sqrt"               '"sqrt":12'    "${BASE}/arithmetic"
check "arithmetic: sin(π/2)=1"         '"sin":1'      "${BASE}/arithmetic"

echo ""
echo "── Classes ─────────────────────────────────────────────────────────────"
check "Vec3 sum"                        '"sum":\[5,7,9\]'           "${BASE}/vec3"
check "Vec3 dot product"               '"dot":32'                   "${BASE}/vec3"
check "inheritance: dog barks"         '"Rex barks"'                "${BASE}/animals"
check "inheritance: cat meows"         '"Whiskers meows"'           "${BASE}/animals"

echo ""
echo "── Generators ──────────────────────────────────────────────────────────"
check "range generator"                '"range":\[1,3,5,7,9\]'      "${BASE}/generators"
check "fibonacci generator"            '"fibonacci":\[0,1,1,2,3,5,8,13\]' "${BASE}/generators"

echo ""
echo "── Async / Promises ────────────────────────────────────────────────────"
check "Promise.all resolves all"       '"all resolved"'             "${BASE}/promises"
check "Promise.all order correct"      '"first"'                    "${BASE}/promises"

echo ""
echo "── Error handling ──────────────────────────────────────────────────────"
check "try/catch/finally all ran"      '"ok":true'                  "${BASE}/error-handling"
check "caught message in log"          '"caught: test error"'       "${BASE}/error-handling"
check "finally in log"                 '"finally"'                  "${BASE}/error-handling"

echo ""
echo "── Collections ─────────────────────────────────────────────────────────"
check "Map word frequency" \
  '"hello":2' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"words":["hello","world","hello","gpu"]}' \
  "${BASE}/collections"

check "Set unique count = 3" \
  '"uniqueCount":3' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"words":["hello","world","hello","gpu"]}' \
  "${BASE}/collections"

echo ""
echo "── RegExp ──────────────────────────────────────────────────────────────"
check "valid email" \
  '"emailValid":true' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"email":"user@example.com","ip":"192.168.1.1","slug":"hello-world"}' \
  "${BASE}/regex"

check "valid IP" \
  '"ipValid":true' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"email":"user@example.com","ip":"192.168.1.1","slug":"hello-world"}' \
  "${BASE}/regex"

check "invalid email" \
  '"emailValid":false' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"email":"not-an-email","ip":"1.2.3.4","slug":"ok"}' \
  "${BASE}/regex"

echo ""
echo "── Language features ───────────────────────────────────────────────────"
check "destructuring: first=1" \
  '"first":1' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"arr":[1,2,3,4],"obj":{"a":10,"b":20}}' \
  "${BASE}/destructuring"

check "optional chaining: deep access" \
  '"name":"Alice"' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"user":{"profile":{"name":"Alice","score":99}}}' \
  "${BASE}/safe-access"

check "optional chaining: missing key defaults" \
  '"name":"anonymous"' \
  -X POST -H 'Content-Type: application/json' \
  -d '{}' \
  "${BASE}/safe-access"

check "closure: add5(3)=8"    '"add5":8'   "${BASE}/closure"
check "closure: add10(3)=13"  '"add10":13' "${BASE}/closure"
check "template literal"      '"Hello from Symmetra v1"' "${BASE}/template"

echo ""
echo "── TextEncoder / TextDecoder ────────────────────────────────────────────"
check "ascii roundtrip" \
  '"ok":true' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"text":"Hello, GPU World!"}' \
  "${BASE}/text-roundtrip"

check "utf-8 roundtrip" \
  '"ok":true' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"text":"Héllo Wörld 🌍"}' \
  "${BASE}/text-roundtrip"

echo ""
echo "── URL parsing ─────────────────────────────────────────────────────────"
check "valid URL parse" \
  '"valid":true' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"url":"https://api.example.com/v1/users?page=2#top"}' \
  "${BASE}/url-parse"

check "URL hostname correct" \
  '"hostname":"api.example.com"' \
  -X POST -H 'Content-Type: application/json' \
  -d '{"url":"https://api.example.com/v1/users"}' \
  "${BASE}/url-parse"

check_status "invalid URL returns 400" 400 \
  -X POST -H 'Content-Type: application/json' \
  -d '{"url":"not-a-url"}' \
  "${BASE}/url-parse"

echo ""
echo "── Timers ──────────────────────────────────────────────────────────────"
check "setTimeout fires + clearTimeout works" \
  '"ok":true' \
  "${BASE}/timer-basic"

echo ""
echo "── AbortController ─────────────────────────────────────────────────────"
check "abort sets signal.aborted" \
  '"aborted":true' \
  "${BASE}/abort-basic"
check "abort fires event listener" \
  '"listenerFired":true' \
  "${BASE}/abort-basic"

echo ""
echo "── KV store ────────────────────────────────────────────────────────────"
curl -sf -X PUT -H 'Content-Type: application/json' \
  -d '{"value":"hello"}' "${BASE}/kv/testkey" >/dev/null
check "kv get after put" '"hello"' "${BASE}/kv/testkey"

curl -sf -X DELETE "${BASE}/kv/testkey" >/dev/null
check_status "kv get after delete returns 404" 404 "${BASE}/kv/testkey"

echo ""
echo "── typeof ──────────────────────────────────────────────────────────────"
check "typeof number literal"   '"number":true'   "${BASE}/typeof-check"
check "typeof string literal"   '"string":true'   "${BASE}/typeof-check"
check "typeof boolean literal"  '"boolean":true'  "${BASE}/typeof-check"
check "typeof null === object"  '"nullObj":true'  "${BASE}/typeof-check"

echo ""
echo "── String methods ──────────────────────────────────────────────────────"
check "padStart '005'"       '"padStart":"005"'    "${BASE}/string-methods"
check "padEnd '500'"         '"padEnd":"500"'      "${BASE}/string-methods"
check "trim removes spaces"  '"trim":"hello"'      "${BASE}/string-methods"
check "replaceAll b→x"       '"replaceAll":"aaxxcc"' "${BASE}/string-methods"
check "repeat ab×3"          '"repeat":"ababab"'   "${BASE}/string-methods"
check "toUpperCase"          '"upper":"HELLO"'     "${BASE}/string-methods"
check "toLowerCase"          '"lower":"hello"'     "${BASE}/string-methods"
check "includes World"       '"includes":true'     "${BASE}/string-methods"
check "startsWith Hello"     '"startsWith":true'   "${BASE}/string-methods"
check "endsWith World"       '"endsWith":true'     "${BASE}/string-methods"
check "indexOf World=6"      '"indexOf":6'         "${BASE}/string-methods"
check "slice 0-5=Hello"      '"slice":"Hello"'     "${BASE}/string-methods"

echo ""
echo "── Number / NaN / Infinity ─────────────────────────────────────────────"
check "Number.isInteger(42)=true"     '"isInt":true'        "${BASE}/number-methods"
check "Number.isInteger(3.14)=false"  '"isIntF":false'      "${BASE}/number-methods"
check "Number.isNaN(NaN)=true"        '"isNaN":true'        "${BASE}/number-methods"
check "Number.isNaN(42)=false"        '"isNaNNum":false'    "${BASE}/number-methods"
check "Number.isFinite(42)=true"      '"isFinite":true'     "${BASE}/number-methods"
check "Number.isFinite(Inf)=false"    '"isFiniteInf":false' "${BASE}/number-methods"
check "global isNaN(NaN)=true"        '"globalNaN":true'    "${BASE}/number-methods"
check "global isFinite(42)=true"      '"globalFin":true'    "${BASE}/number-methods"
check "Infinity > 1M"                 '"infPos":true'       "${BASE}/number-methods"
check "NEGATIVE_INFINITY < -1M"       '"infNeg":true'       "${BASE}/number-methods"

echo ""
echo "── Array statics ───────────────────────────────────────────────────────"
check "Array.isArray([...])=true"   '"isArray":true'    "${BASE}/array-statics"
check "Array.isArray('x')=false"    '"isArrayStr":false' "${BASE}/array-statics"
check "Array.isArray(42)=false"     '"isArrayNum":false' "${BASE}/array-statics"

echo ""
echo "── Object statics ──────────────────────────────────────────────────────"
check "Object.hasOwn returns true"  '"hasOwn":true'  "${BASE}/object-statics"

echo ""
echo "── Date.now ────────────────────────────────────────────────────────────"
check "Date.now returns a number"  '"isNumber":true' "${BASE}/date-now"
check "Date.now returns non-neg"   '"isNonNeg":true' "${BASE}/date-now"

echo ""
echo "── Math extras ─────────────────────────────────────────────────────────"
check "Math.sign(5)=1"    '"sign_pos":1'    "${BASE}/math-extras"
check "Math.sign(-3)=-1"  '"sign_neg":-1'   "${BASE}/math-extras"
check "Math.sign(0)=0"    '"sign_zero":0'   "${BASE}/math-extras"
check "Math.hypot(3,4)=5" '"hypot":5'       "${BASE}/math-extras"
check "Math.clamp 150→100" '"clamp":100'    "${BASE}/math-extras"

echo ""
echo "── RegExp matchAll ─────────────────────────────────────────────────────"
check "matchAll finds 3 matches"  '"count":3'   "${BASE}/matchall"
check "matchAll first match valid" '"first":true'  "${BASE}/matchall"
check "matchAll second match valid" '"second":true' "${BASE}/matchall"

echo ""
echo "── Promise.race ────────────────────────────────────────────────────────"
check "Promise.race picks fast"  '"ok":true'  "${BASE}/promise-race"

echo ""
echo "── Generator + break ───────────────────────────────────────────────────"
check "generator yields 10 values"   '"ok":true'       "${BASE}/generator-take"
check "generator result is 0..9"     '"result":\[0,1'  "${BASE}/generator-take"

echo ""
echo "── Async iterator ──────────────────────────────────────────────────────"
check "async iterator yields 5 items"  '"ok":true'    "${BASE}/async-iterator"
check "async iterator length=5"        '"length":5'   "${BASE}/async-iterator"

echo ""
echo "── BigInt basics ───────────────────────────────────────────────────────"
check "100n + 200n = 300"  '"sum":300'   "${BASE}/bigint-basic"
check "bigint ok=true"     '"ok":true'   "${BASE}/bigint-basic"

# ─── Summary ─────────────────────────────────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════════"
echo "Results: ${PASS} passed, ${FAIL} failed"
if [ ${FAIL} -gt 0 ]; then
  echo ""
  echo "Failed tests:"
  for e in "${ERRORS[@]}"; do echo "  - $e"; done
  exit 1
fi
echo "All tests passed."
