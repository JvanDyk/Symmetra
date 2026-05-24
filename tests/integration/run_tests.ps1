# Integration test runner for Symmetra (Windows PowerShell)
#
# Usage: pwsh tests/integration/run_tests.ps1 [-Host localhost] [-Port 8090]
#
# Start the server first:
#   sym ./tests/integration/server.ts
# Then in another terminal:
#   pwsh tests/integration/run_tests.ps1

param(
    [string]$ServerHost = 'localhost',
    [int]   $Port       = 8090
)

$BASE   = "http://${ServerHost}:${Port}"
$PASS   = 0
$FAIL   = 0
$ERRORS = @()

# ─── Helpers ──────────────────────────────────────────────────────────────────

function Ok([string]$desc) {
    Write-Host "  [PASS] $desc" -ForegroundColor Green
    $script:PASS++
}

function Fail([string]$desc) {
    Write-Host "  [FAIL] $desc" -ForegroundColor Red
    $script:FAIL++
    $script:ERRORS += $desc
}

function Check([string]$desc, [string]$expect, [string]$url, [hashtable]$opts = @{}) {
    try {
        $splat = @{ Uri = $url; TimeoutSec = 10 } + $opts
        $resp  = Invoke-RestMethod @splat -ErrorAction Stop
        $body  = $resp | ConvertTo-Json -Compress -Depth 10
        if ($body -match [regex]::Escape($expect) -or $body -like "*$expect*") {
            Ok $desc
        } else {
            Fail "$desc (got: $($body.Substring(0, [Math]::Min($body.Length, 120))))"
        }
    } catch {
        Fail "$desc (error: $($_.Exception.Message))"
    }
}

function CheckStatus([string]$desc, [int]$expectedStatus, [string]$url, [hashtable]$opts = @{}) {
    try {
        $splat = @{ Uri = $url; TimeoutSec = 10 } + $opts
        Invoke-WebRequest @splat -ErrorAction Stop | Out-Null
        if ($expectedStatus -eq 200) { Ok $desc } else { Fail "$desc (expected $expectedStatus, got 200)" }
    } catch {
        $actual = $_.Exception.Response.StatusCode.value__
        if ($actual -eq $expectedStatus) { Ok $desc }
        else { Fail "$desc (expected $expectedStatus, got $actual)" }
    }
}

function Post([string]$url, [object]$body) {
    return @{ Method = 'POST'; ContentType = 'application/json'; Body = ($body | ConvertTo-Json -Compress) }
}

# ─── Wait for server ──────────────────────────────────────────────────────────
Write-Host "Waiting for server on $BASE..."
$ready = $false
for ($i = 0; $i -lt 30; $i++) {
    try { Invoke-RestMethod "$BASE/ping" -TimeoutSec 1 | Out-Null; $ready = $true; break }
    catch { Start-Sleep 1 }
}
if (-not $ready) { Write-Error "Server did not start in 30s"; exit 1 }
Write-Host "Server is ready.`n"

# ─── Tests ────────────────────────────────────────────────────────────────────

Write-Host "── Core ──────────────────────────────────────────────────────────────"
Check "ping"                  '"ok":true'  "$BASE/ping"
Check "arithmetic: add=3"     '"add":3'    "$BASE/arithmetic"
Check "arithmetic: sqrt=12"   '"sqrt":12'  "$BASE/arithmetic"

Write-Host ""
Write-Host "── Classes ────────────────────────────────────────────────────────────"
Check "Vec3 sum"               '"sum"'      "$BASE/vec3"
Check "Vec3 dot=32"            '"dot":32'   "$BASE/vec3"
Check "Dog barks"              'Rex barks'  "$BASE/animals"
Check "Cat meows"              'meows'      "$BASE/animals"

Write-Host ""
Write-Host "── Generators ─────────────────────────────────────────────────────────"
Check "range generator"     '"range"'       "$BASE/generators"
Check "fibonacci generator" '"fibonacci"'   "$BASE/generators"

Write-Host ""
Write-Host "── Async / Promises ──────────────────────────────────────────────────"
Check "Promise.all" '"all resolved"' "$BASE/promises"

Write-Host ""
Write-Host "── Error handling ─────────────────────────────────────────────────────"
Check "try/catch/finally" '"ok":true' "$BASE/error-handling"
Check "caught message"    'caught'    "$BASE/error-handling"

Write-Host ""
Write-Host "── Collections ────────────────────────────────────────────────────────"
Check "Map frequency" '"hello":2' "$BASE/collections" `
    (Post "$BASE/collections" @{ words = @("hello","world","hello","gpu") })
Check "Set unique=3" '"uniqueCount":3' "$BASE/collections" `
    (Post "$BASE/collections" @{ words = @("hello","world","hello","gpu") })

Write-Host ""
Write-Host "── RegExp ─────────────────────────────────────────────────────────────"
Check "valid email" '"emailValid":true' "$BASE/regex" `
    (Post "$BASE/regex" @{ email = "user@example.com"; ip = "1.2.3.4"; slug = "ok" })
Check "invalid email" '"emailValid":false' "$BASE/regex" `
    (Post "$BASE/regex" @{ email = "bad"; ip = "1.2.3.4"; slug = "ok" })

Write-Host ""
Write-Host "── TextEncoder / TextDecoder ──────────────────────────────────────────"
Check "ascii roundtrip ok" '"ok":true' "$BASE/text-roundtrip" `
    (Post "$BASE/text-roundtrip" @{ text = "Hello GPU!" })
Check "utf8 roundtrip ok"  '"ok":true' "$BASE/text-roundtrip" `
    (Post "$BASE/text-roundtrip" @{ text = "Héllo Wörld" })

Write-Host ""
Write-Host "── URL parsing ────────────────────────────────────────────────────────"
Check "valid URL" '"valid":true' "$BASE/url-parse" `
    (Post "$BASE/url-parse" @{ url = "https://example.com/path?q=1" })
Check "hostname correct" '"hostname":"example.com"' "$BASE/url-parse" `
    (Post "$BASE/url-parse" @{ url = "https://example.com/" })
CheckStatus "invalid URL=400" 400 "$BASE/url-parse" `
    (Post "$BASE/url-parse" @{ url = "not-a-url" })

Write-Host ""
Write-Host "── Timers ─────────────────────────────────────────────────────────────"
Check "setTimeout + clearTimeout" '"ok":true' "$BASE/timer-basic"

Write-Host ""
Write-Host "── AbortController ────────────────────────────────────────────────────"
Check "signal.aborted=true"    '"aborted":true'       "$BASE/abort-basic"
Check "listener fired"         '"listenerFired":true' "$BASE/abort-basic"

Write-Host ""
Write-Host "── KV store ───────────────────────────────────────────────────────────"
Invoke-RestMethod "$BASE/kv/testkey" -Method PUT -ContentType 'application/json' `
    -Body '{"value":"hello"}' | Out-Null
Check "kv get after put" '"hello"' "$BASE/kv/testkey"
Invoke-RestMethod "$BASE/kv/testkey" -Method DELETE | Out-Null
CheckStatus "kv 404 after delete" 404 "$BASE/kv/testkey"

Write-Host ""
Write-Host "── typeof ──────────────────────────────────────────────────────────────"
Check "typeof number"   '"number":true'   "$BASE/typeof-check"
Check "typeof string"   '"string":true'   "$BASE/typeof-check"
Check "typeof boolean"  '"boolean":true'  "$BASE/typeof-check"
Check "typeof null"     '"nullObj":true'  "$BASE/typeof-check"

Write-Host ""
Write-Host "── String methods ───────────────────────────────────────────────────────"
Check "padStart"    '"padStart":"005"'     "$BASE/string-methods"
Check "padEnd"      '"padEnd":"500"'       "$BASE/string-methods"
Check "trim"        '"trim":"hello"'       "$BASE/string-methods"
Check "replaceAll"  '"replaceAll":"aaxxcc"' "$BASE/string-methods"
Check "repeat"      '"repeat":"ababab"'    "$BASE/string-methods"
Check "toUpperCase" '"upper":"HELLO"'      "$BASE/string-methods"
Check "toLowerCase" '"lower":"hello"'      "$BASE/string-methods"
Check "includes"    '"includes":true'      "$BASE/string-methods"
Check "startsWith"  '"startsWith":true'    "$BASE/string-methods"
Check "endsWith"    '"endsWith":true'      "$BASE/string-methods"
Check "indexOf 6"   '"indexOf":6'          "$BASE/string-methods"
Check "slice Hello" '"slice":"Hello"'      "$BASE/string-methods"

Write-Host ""
Write-Host "── Number / NaN / Infinity ──────────────────────────────────────────────"
Check "isInteger(42)=true"     '"isInt":true'        "$BASE/number-methods"
Check "isInteger(3.14)=false"  '"isIntF":false'      "$BASE/number-methods"
Check "isNaN(NaN)=true"        '"isNaN":true'        "$BASE/number-methods"
Check "isNaN(42)=false"        '"isNaNNum":false'     "$BASE/number-methods"
Check "isFinite(42)=true"      '"isFinite":true'      "$BASE/number-methods"
Check "isFinite(Inf)=false"    '"isFiniteInf":false'  "$BASE/number-methods"
Check "Infinity>1M"            '"infPos":true'        "$BASE/number-methods"
Check "NEG_INF<-1M"            '"infNeg":true'        "$BASE/number-methods"

Write-Host ""
Write-Host "── Array statics ────────────────────────────────────────────────────────"
Check "Array.isArray([])=true"  '"isArray":true'     "$BASE/array-statics"
Check "Array.isArray(x)=false"  '"isArrayStr":false' "$BASE/array-statics"

Write-Host ""
Write-Host "── Object statics ───────────────────────────────────────────────────────"
Check "Object.hasOwn"  '"hasOwn":true'  "$BASE/object-statics"

Write-Host ""
Write-Host "── Date.now ─────────────────────────────────────────────────────────────"
Check "Date.now is number"   '"isNumber":true'  "$BASE/date-now"
Check "Date.now non-neg"     '"isNonNeg":true'  "$BASE/date-now"

Write-Host ""
Write-Host "── Math extras ──────────────────────────────────────────────────────────"
Check "Math.sign(5)=1"    '"sign_pos":1'    "$BASE/math-extras"
Check "Math.sign(-3)=-1"  '"sign_neg":-1'   "$BASE/math-extras"
Check "Math.hypot(3,4)=5" '"hypot":5'       "$BASE/math-extras"
Check "Math.clamp 150→100" '"clamp":100'    "$BASE/math-extras"

Write-Host ""
Write-Host "── RegExp matchAll ──────────────────────────────────────────────────────"
Check "matchAll count=3"     '"count":3'      "$BASE/matchall"
Check "matchAll first valid" '"first":true'   "$BASE/matchall"

Write-Host ""
Write-Host "── Promise.race ─────────────────────────────────────────────────────────"
Check "Promise.race ok"  '"ok":true'  "$BASE/promise-race"

Write-Host ""
Write-Host "── Generator + break ────────────────────────────────────────────────────"
Check "generator 10 values"  '"ok":true'      "$BASE/generator-take"
Check "generator 0..9"       '"result":[0,1'  "$BASE/generator-take"

Write-Host ""
Write-Host "── Async iterator ───────────────────────────────────────────────────────"
Check "async iter 5 items"  '"ok":true'    "$BASE/async-iterator"
Check "async iter length=5" '"length":5'   "$BASE/async-iterator"

Write-Host ""
Write-Host "── BigInt basics ────────────────────────────────────────────────────────"
Check "100n + 200n = 300"  '"sum":300'   "$BASE/bigint-basic"
Check "bigint ok=true"     '"ok":true'   "$BASE/bigint-basic"

# ─── Summary ──────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host ("=" * 60)
Write-Host "Results: $PASS passed, $FAIL failed"
if ($FAIL -gt 0) {
    Write-Host ""
    Write-Host "Failed tests:" -ForegroundColor Red
    foreach ($e in $ERRORS) { Write-Host "  - $e" -ForegroundColor Red }
    exit 1
}
Write-Host "All tests passed." -ForegroundColor Green
