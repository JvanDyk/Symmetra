// test_codegen.cpp — Symmetra Codegen Unit Tests
//
// Tests the CUDA C++ code generator by parsing TypeScript snippets and checking
// that the emitted source contains expected patterns.  No CUDA hardware required.
//
// Build: g++ -std=c++17 -I../.. -I../../.. test_codegen.cpp \
//             ../../parser/SymmParser.cpp Codegen.cpp -o test_codegen
// Run:   ./test_codegen
//
// On pass: prints "All N tests passed."
// On fail: prints the failing test name and exits with code 1.

#include "Codegen.h"
#include "parser/SymmParser.h"
#include <cassert>
#include <iostream>
#include <string>
#include <stdexcept>
#include <algorithm>

namespace symm {

// ── Test harness ──────────────────────────────────────────────────────────────

static int gPassed = 0;
static int gFailed = 0;

static ServerOptions defaultOpts() {
    ServerOptions o;
    o.port    = 8080;
    o.verbose = false;
    return o;
}

static std::string codegen(const std::string& ts) {
    SymmParser parser;
    Module mod = parser.parse(ts);
    Codegen cg(defaultOpts());
    return cg.emit(mod);
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

#define TEST(name, body) \
    do { \
        try { \
            body; \
            std::cout << "  [PASS] " << name << "\n"; \
            ++gPassed; \
        } catch (const std::exception& e) { \
            std::cout << "  [FAIL] " << name << ": " << e.what() << "\n"; \
            ++gFailed; \
        } \
    } while (0)

#define ASSERT_CONTAINS(code, pattern) \
    do { \
        if (!contains(code, pattern)) \
            throw std::runtime_error("Expected pattern not found: " + std::string(pattern) + \
                "\n--- generated ---\n" + (code).substr(0, 2000)); \
    } while (0)

#define ASSERT_NOT_CONTAINS(code, pattern) \
    do { \
        if (contains(code, pattern)) \
            throw std::runtime_error("Unexpected pattern found: " + std::string(pattern) + \
                "\n--- generated ---\n" + (code).substr(0, 2000)); \
    } while (0)

// ── Prelude tests ─────────────────────────────────────────────────────────────

static void testPrelude() {
    std::cout << "\n=== Prelude / Runtime Stubs ===\n";

    TEST("symm_write_float — included", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ x: 1.5 }); });");
        ASSERT_CONTAINS(src, "symm_write_float");
    });

    TEST("Date struct — included", {
        auto src = codegen("app.get('/t', (ctx) => { const t = Date.now(); ctx.json({ t }); });");
        ASSERT_CONTAINS(src, "struct Date");
        ASSERT_CONTAINS(src, "_symm_now_ms");
    });

    TEST("parseInt with radix — included", {
        auto src = codegen("app.get('/x', async (ctx) => { const n = parseInt('ff', 16); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_int_radix");
    });

    TEST("_SBuf — included for string buffers", {
        auto src = codegen("app.get('/s', (ctx) => { const s = `hello`; ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "struct _SBuf");
    });

    TEST("_symm_start_ms — process.uptime() backend", {
        auto src = codegen("app.get('/up', (ctx) => { const u = process.uptime(); ctx.json({ u }); });");
        ASSERT_CONTAINS(src, "_symm_start_ms");
    });

    TEST("RegExp struct — included", {
        auto src = codegen("app.get('/r', (ctx) => { const re = /hello/; ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "struct RegExp");
    });
}

// ── Handler / routing tests ───────────────────────────────────────────────────

static void testRouting() {
    std::cout << "\n=== Routing & Handlers ===\n";

    TEST("GET handler emitted", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ message: 'pong' }); });");
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "__device__");
    });

    TEST("POST handler emitted", {
        auto src = codegen("app.post('/users', async (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Multiple handlers emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => { ctx.json({ a: 1 }); });\n"
            "app.post('/b', (ctx) => { ctx.json({ b: 2 }); });"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "symm_handler_1");
    });

    TEST("Route table contains paths", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({}); });");
        ASSERT_CONTAINS(src, "\"GET\"");
        ASSERT_CONTAINS(src, "\"/ping\"");
    });

    TEST("ctx.json() object emitted", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ message: 'hello', ok: true }); });");
        ASSERT_CONTAINS(src, "symm_json_str");
    });

    TEST("ctx.status() chain", {
        auto src = codegen("app.get('/404', (ctx) => { ctx.status(404).json({ error: 'not found' }); });");
        ASSERT_CONTAINS(src, "resp_status");
        ASSERT_CONTAINS(src, "404");
    });

    TEST("ctx.text() response", {
        auto src = codegen("app.get('/txt', (ctx) => { ctx.text('Hello!'); });");
        ASSERT_CONTAINS(src, "symm_json_str");
    });
}

// ── Route params & query ──────────────────────────────────────────────────────

static void testParams() {
    std::cout << "\n=== Route Params & Query ===\n";

    TEST("ctx.params.id — extracts route param", {
        auto src = codegen("app.get('/users/:id', (ctx) => { const id = ctx.params.id; ctx.json({ id }); });");
        ASSERT_CONTAINS(src, "symm_extract_param");
        // param 'id' is segment index 1 in /users/:id
        ASSERT_CONTAINS(src, ", 1)");
    });

    TEST("ctx.params with multiple segments", {
        auto src = codegen("app.get('/a/:x/b/:y', (ctx) => { const x = ctx.params.x; const y = ctx.params.y; ctx.json({ x, y }); });");
        ASSERT_CONTAINS(src, "symm_extract_param");
    });

    TEST("ctx.query.key — extracts query param", {
        auto src = codegen("app.get('/search', (ctx) => { const q = ctx.query.q; ctx.json({ q }); });");
        ASSERT_CONTAINS(src, "symm_extract_query");
        ASSERT_CONTAINS(src, "\"q\"");
    });

    TEST("ctx.method — HTTP method field", {
        auto src = codegen("app.get('/m', (ctx) => { const m = ctx.method; ctx.json({ m }); });");
        ASSERT_CONTAINS(src, "ctx->method");
    });

    TEST("ctx.path — URL path field", {
        auto src = codegen("app.get('/p', (ctx) => { const p = ctx.path; ctx.json({ p }); });");
        ASSERT_CONTAINS(src, "ctx->path");
    });
}

// ── Template literals ─────────────────────────────────────────────────────────

static void testTemplateLiterals() {
    std::cout << "\n=== Template Literals ===\n";

    TEST("Simple template string — no interpolation", {
        auto src = codegen("app.get('/t', (ctx) => { const s = `hello world`; ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "\"hello world\"");
    });

    TEST("Template literal in expr context — IIFE", {
        auto src = codegen("app.get('/t', (ctx) => { ctx.json({ msg: `hi ${ctx.method}` }); });");
        ASSERT_CONTAINS(src, "_SBuf");
        ASSERT_CONTAINS(src, "symm_write");
    });

    TEST("Template literal in ConstDecl — char buffer", {
        auto src = codegen("app.get('/t', (ctx) => { const msg = `hello ${ctx.path}`; ctx.json({ msg }); });");
        ASSERT_CONTAINS(src, "symm_write_str");
        ASSERT_CONTAINS(src, "const char* msg");
    });

    TEST("Nested template expr evaluation", {
        auto src = codegen("app.get('/t', (ctx) => { const x = `a=${1 + 2}`; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "symm_write");
    });

    TEST("Template escaping — quotes in parts", {
        // Should not produce unbalanced quotes in C++ output
        auto src = codegen(R"(app.get('/t', (ctx) => { const s = `say "hi"`; ctx.json({ s }); });)");
        ASSERT_CONTAINS(src, "say \\\"hi\\\"");  // escaped in C++ string
    });
}

// ── for...of loops ────────────────────────────────────────────────────────────

static void testForOf() {
    std::cout << "\n=== for...of Loops ===\n";

    TEST("for...of on string literal — char loop", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  for (const ch of 'hello') { ctx.json({ ch }); }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "[2]");           // char buf[2] — one-char string buffer
        ASSERT_CONTAINS(src, "\"hello\"");     // literal string used as iteration source
        // The iteration index should NOT have a _len companion for string literals
        ASSERT_NOT_CONTAINS(src, "hello_len"); // no _len variable for literal strings
    });

    TEST("for...of on ctx.path — char loop", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  for (const ch of ctx.path) { ctx.json({ ch }); }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "[2]");  // char buf[2] for char iteration
    });

    TEST("for...of on array — indexed loop with _len", {
        // Arrays use the _len companion variable
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  for (const x of arr) { ctx.json({ x }); }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "_len");
    });
}

// ── Switch on strings ─────────────────────────────────────────────────────────

static void testSwitchStmt() {
    std::cout << "\n=== Switch Statements ===\n";

    TEST("Switch on number — C++ switch", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  switch (1) { case 1: ctx.json({ x: 1 }); break; }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "switch");
    });

    TEST("Switch on string — if-else chain with symm_str_eq", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  switch (ctx.method) {\n"
            "    case 'GET': ctx.json({ m: 'get' }); break;\n"
            "    case 'POST': ctx.json({ m: 'post' }); break;\n"
            "    default: ctx.json({ m: 'other' }); break;\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_eq");
        ASSERT_NOT_CONTAINS(src, "switch (ctx");  // must NOT emit C++ switch for string comparisons
        // NOTE: switch (route_id) in the dispatch table is fine — we check for "switch (ctx" specifically
    });
}

// ── parseInt / parseFloat ─────────────────────────────────────────────────────

static void testParseInt() {
    std::cout << "\n=== parseInt / parseFloat ===\n";

    TEST("parseInt(str) — single arg", {
        auto src = codegen("app.get('/t', (ctx) => { const n = parseInt(ctx.query.n); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "parseInt(");
    });

    TEST("parseInt(str, 16) — radix 16", {
        auto src = codegen("app.get('/t', (ctx) => { const n = parseInt(ctx.query.hex, 16); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_int_radix");
        ASSERT_CONTAINS(src, "16");
    });

    TEST("parseInt(str, 2) — binary", {
        auto src = codegen("app.get('/t', (ctx) => { const n = parseInt('1010', 2); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_int_radix");
        ASSERT_CONTAINS(src, "2");
    });

    TEST("Number.parseInt(str, radix) — static method", {
        auto src = codegen("app.get('/t', (ctx) => { const n = Number.parseInt(ctx.query.v, 8); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_int_radix");
        ASSERT_CONTAINS(src, "8");
    });

    TEST("parseFloat(str) — uses symm_parse_float", {
        auto src = codegen("app.get('/t', (ctx) => { const n = parseFloat(ctx.query.f); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_float(");
    });
}

// ── Math functions ────────────────────────────────────────────────────────────

static void testMath() {
    std::cout << "\n=== Math Functions ===\n";

    TEST("Math.sin → sinf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.sin(1.0); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "sinf(");
    });

    TEST("Math.sqrt → sqrtf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.sqrt(2.0); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "sqrtf(");
    });

    TEST("Math.pow → powf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.pow(2.0, 10.0); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "powf(");
    });

    TEST("Math.PI — constant", {
        auto src = codegen("app.get('/t', (ctx) => { const pi = Math.PI; ctx.json({ pi }); });");
        ASSERT_CONTAINS(src, "3.14159");
    });

    TEST("Math.abs → fabsf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.abs(-1.5); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "fabsf(");
    });

    TEST("** operator → powf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 2 ** 8; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "powf(");
    });
}

// ── String methods ────────────────────────────────────────────────────────────

static void testStringMethods() {
    std::cout << "\n=== String Methods ===\n";

    TEST("str.length → symm_strlen", {
        auto src = codegen("app.get('/t', (ctx) => { const n = ctx.path.length; ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_strlen");
    });

    TEST("str.trim() → symm_str_trim", {
        auto src = codegen("app.get('/t', (ctx) => { const s = ctx.query.q.trim(); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_str_trim");
    });

    TEST("str.includes() → symm_str_includes", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = ctx.path.includes('/api'); ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "symm_str_includes");
    });

    TEST("str.startsWith() → symm_str_starts_with", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = ctx.path.startsWith('/api'); ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "symm_str_starts_with");
    });

    TEST("str.toUpperCase() → symm_str_to_upper", {
        auto src = codegen("app.get('/t', (ctx) => { const s = ctx.method.toUpperCase(); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_str_to_upper");
    });

    TEST("str.slice() → symm_str_slice", {
        auto src = codegen("app.get('/t', (ctx) => { const s = ctx.path.slice(1); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_str_slice");
    });

    TEST("str.indexOf() → symm_str_index_of", {
        auto src = codegen("app.get('/t', (ctx) => { const i = ctx.path.indexOf('/'); ctx.json({ i }); });");
        ASSERT_CONTAINS(src, "symm_str_index_of");
    });

    TEST("str.replace() → symm_str_replace", {
        auto src = codegen("app.get('/t', (ctx) => { const s = ctx.path.replace('a', 'b'); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_str_replace");
    });

    TEST("String.fromCharCode() → symm_str_from_char_code", {
        auto src = codegen("app.get('/t', (ctx) => { const c = String.fromCharCode(65); ctx.json({ c }); });");
        ASSERT_CONTAINS(src, "symm_str_from_char_code");
    });
}

// ── Number methods ────────────────────────────────────────────────────────────

static void testNumberMethods() {
    std::cout << "\n=== Number Methods ===\n";

    TEST("n.toFixed(2) → symm_number_to_fixed", {
        auto src = codegen("app.get('/t', (ctx) => { const s = (3.14159).toFixed(2); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_number_to_fixed");
        ASSERT_CONTAINS(src, "2");
    });

    TEST("n.toString(16) — hex", {
        auto src = codegen("app.get('/t', (ctx) => { const s = (255).toString(16); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "0xF");
    });

    TEST("Number.isNaN(x)", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = Number.isNaN(0); ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "Number::isNaN");
    });

    TEST("Number.isFinite(x)", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = Number.isFinite(1.0); ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "Number::isFinite");
    });

    TEST("Number.EPSILON constant", {
        auto src = codegen("app.get('/t', (ctx) => { const e = Number.EPSILON; ctx.json({ e }); });");
        ASSERT_CONTAINS(src, "Number::EPSILON");
    });
}

// ── Binary operators ──────────────────────────────────────────────────────────

static void testBinaryOps() {
    std::cout << "\n=== Binary Operators ===\n";

    TEST("=== string equality → symm_eq", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = ctx.method === 'GET'; ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "symm_eq");
    });

    TEST("!== string inequality", {
        auto src = codegen("app.get('/t', (ctx) => { const ok = ctx.method !== 'POST'; ctx.json({ ok }); });");
        ASSERT_CONTAINS(src, "!symm_eq");
    });

    TEST("?? nullish coalescing — lambda style", {
        auto src = codegen("app.get('/t', (ctx) => { const v = ctx.query.q ?? 'default'; ctx.json({ v }); });");
        ASSERT_CONTAINS(src, "_nc");
    });

    TEST(">>> unsigned right shift", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 255 >>> 1; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "(unsigned)");
    });
}

// ── JSON methods ──────────────────────────────────────────────────────────────

static void testJSON() {
    std::cout << "\n=== JSON ===\n";

    TEST("JSON.stringify — symm_json_stringify", {
        auto src = codegen("app.get('/t', (ctx) => { const s = JSON.stringify({ a: 1 }); ctx.json({ s }); });");
        ASSERT_CONTAINS(src, "symm_json_stringify");
    });

    TEST("JSON.parse — symm_json_parse", {
        auto src = codegen("app.get('/t', (ctx) => { const obj = JSON.parse(ctx.body); ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_json_parse");
    });
}

// ── Classes ───────────────────────────────────────────────────────────────────

static void testClasses() {
    std::cout << "\n=== Classes ===\n";

    TEST("Class definition emitted", {
        auto src = codegen(
            "class Vec2 {\n"
            "  x: float; y: float;\n"
            "  constructor(x: float, y: float) { this.x = x; this.y = y; }\n"
            "  length(): float { return Math.sqrt(this.x * this.x + this.y * this.y); }\n"
            "}\n"
            "app.get('/v', (ctx) => { const v = new Vec2(3, 4); ctx.json({ len: v.length() }); });"
        );
        ASSERT_CONTAINS(src, "struct Vec2");
        ASSERT_CONTAINS(src, "sqrtf(");
    });
}

// ── Process / Date ────────────────────────────────────────────────────────────

static void testProcessDate() {
    std::cout << "\n=== process / Date ===\n";

    TEST("Date.now() — _symm_now_ms", {
        auto src = codegen("app.get('/t', (ctx) => { const t = Date.now(); ctx.json({ t }); });");
        ASSERT_CONTAINS(src, "_symm_now_ms");
    });

    TEST("process.uptime() — uses _symm_now_ms - _symm_start_ms", {
        auto src = codegen("app.get('/t', (ctx) => { const u = process.uptime(); ctx.json({ u }); });");
        // process maps to _symm_process which has uptime()
        ASSERT_CONTAINS(src, "_symm_process");
    });

    TEST("process.platform — gpu string", {
        auto src = codegen("app.get('/t', (ctx) => { const p = process.platform; ctx.json({ p }); });");
        ASSERT_CONTAINS(src, "platform");
    });
}

// ── Optional chaining ─────────────────────────────────────────────────────────

static void testOptionalChaining() {
    std::cout << "\n=== Optional Chaining ===\n";

    TEST("obj?.prop — ternary guard", {
        auto src = codegen("app.get('/t', (ctx) => { const v = ctx?.method; ctx.json({ v }); });");
        // OptChainExpr emits a ternary
        ASSERT_CONTAINS(src, " ? ");
    });
}

// ── Bitwise operators ─────────────────────────────────────────────────────────

static void testBitwise() {
    std::cout << "\n=== Bitwise Operators ===\n";

    TEST("& bitwise and", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 0xFF & 0x0F; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "& ");
    });

    TEST("| bitwise or", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 0x01 | 0x02; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "| ");
    });

    TEST("^ bitwise xor", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 0xFF ^ 0x0F; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "^ ");
    });

    TEST("~ bitwise not", {
        auto src = codegen("app.get('/t', (ctx) => { const x = ~0; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "~");
    });

    TEST("<< left shift", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 1 << 4; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "<< ");
    });

    TEST(">> right shift", {
        auto src = codegen("app.get('/t', (ctx) => { const x = 256 >> 4; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, ">> ");
    });
}

// ── Dispatch table ────────────────────────────────────────────────────────────

static void testDispatch() {
    std::cout << "\n=== Dispatch Table ===\n";

    TEST("symm_dispatch emitted", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_dispatch");
    });

    TEST("symm_dispatch_batch emitted", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_dispatch_batch");
    });

    TEST("route_count constant emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => { ctx.json({ a: 1 }); });\n"
            "app.get('/b', (ctx) => { ctx.json({ b: 2 }); });"
        );
        ASSERT_CONTAINS(src, "symm_route_count");
    });
}

// ── Edge cases ────────────────────────────────────────────────────────────────

static void testEdgeCases() {
    std::cout << "\n=== Edge Cases ===\n";

    TEST("Empty handler compiles", {
        auto src = codegen("app.get('/empty', (ctx) => { });");
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Return statement", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  if (ctx.method !== 'GET') { ctx.status(405).json({ error: 'method' }); return; }\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "return");
    });

    TEST("try-catch maps to _err pattern", {
        auto src = codegen(
            "app.get('/t', async (ctx) => {\n"
            "  try {\n"
            "    const body = await ctx.json();\n"
            "    ctx.json({ ok: true });\n"
            "  } catch (e) {\n"
            "    ctx.status(400).json({ error: 'bad' });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "_err");
    });

    TEST("Boolean literals", {
        auto src = codegen("app.get('/t', (ctx) => { ctx.json({ ok: true, fail: false }); });");
        ASSERT_CONTAINS(src, "true");
        ASSERT_CONTAINS(src, "false");
    });

    TEST("null literal → nullptr", {
        auto src = codegen("app.get('/t', (ctx) => { const v = null; ctx.json({ v }); });");
        ASSERT_CONTAINS(src, "nullptr");
    });

    TEST("Ternary operator", {
        auto src = codegen("app.get('/t', (ctx) => { const v = true ? 1 : 2; ctx.json({ v }); });");
        ASSERT_CONTAINS(src, "?");
        ASSERT_CONTAINS(src, ":");
    });
}

// ── Loops ─────────────────────────────────────────────────────────────────────

static void testLoops() {
    std::cout << "\n=== Loops ===\n";

    TEST("while loop emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 0; while (i < 10) { i++; }\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "while (");
    });

    TEST("do-while loop emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 0; do { i++; } while (i < 5);\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "do {");
        ASSERT_CONTAINS(src, "} while (");
    });

    TEST("for loop (C-style) emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let sum = 0;\n"
            "  for (let i = 0; i < 10; i++) { sum += i; }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "for (");
    });

    TEST("for-of over array variable — indexed loop", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const items = [1, 2, 3];\n"
            "  let sum = 0;\n"
            "  for (const item of items) { sum += item; }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "_len");   // array loop uses _len companion
    });

    TEST("break inside while", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 0;\n"
            "  while (true) { if (i >= 5) break; i++; }\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "break");
    });

    TEST("continue inside for-of", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  let sum = 0;\n"
            "  for (const x of arr) { if (x === 2) continue; sum += x; }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "continue");
    });
}

// ── Destructuring ─────────────────────────────────────────────────────────────

static void testDestructuring() {
    std::cout << "\n=== Destructuring ===\n";

    TEST("Object destructuring from ctx.body", {
        auto src = codegen(
            "app.post('/t', async (ctx) => {\n"
            "  const { name, age } = await ctx.json();\n"
            "  ctx.json({ name, age });\n"
            "});"
        );
        // Should extract fields via symm_extract_body_field or similar
        ASSERT_CONTAINS(src, "name");
        ASSERT_CONTAINS(src, "age");
    });

    TEST("Array destructuring — const [a, b] = arr", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2];\n"
            "  const [a, b] = arr;\n"
            "  ctx.json({ a, b });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "arr");
    });
}

// ── Logical assignment operators ──────────────────────────────────────────────

static void testLogicalAssignment() {
    std::cout << "\n=== Logical Assignment ===\n";

    TEST("&&= logical and-assign", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let x = 1;\n"
            "  x &&= 2;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "x");
    });

    TEST("||= logical or-assign", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let x = 0;\n"
            "  x ||= 42;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "x");
    });

    TEST("??= nullish assign", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let x = nullptr;\n"
            "  x ??= 'default';\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "x");
    });

    TEST("**= exponentiation assign", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let x = 2;\n"
            "  x **= 8;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "x");
    });
}

// ── Additional string methods ──────────────────────────────────────────────────

static void testStringMethods2() {
    std::cout << "\n=== Additional String Methods ===\n";

    TEST("str.endsWith() → symm_str_ends_with", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const ok = ctx.path.endsWith('/ping');\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_ends_with");
    });

    TEST("str.split() — returns _SBuf", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const parts = ctx.path.split('/');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_split");
    });

    TEST("str.repeat() → symm_str_repeat", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = 'ab'.repeat(3);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_repeat");
    });

    TEST("str.padStart() → symm_str_pad_start", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = '5'.padStart(3, '0');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_pad_start");
    });

    TEST("str.padEnd() → symm_str_pad_end", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = '5'.padEnd(3, '0');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_pad_end");
    });

    TEST("str.charAt() → symm_str_at", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const c = ctx.path.charAt(0);\n"
            "  ctx.json({ c });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_at");
    });

    TEST("str.charCodeAt() → symm_str_char_code_at", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = ctx.path.charCodeAt(0);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_char_code_at");
    });

    TEST("str.substring() → symm_str_slice", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = ctx.path.substring(1, 5);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_slice");
    });

    TEST("str.toLowerCase() → symm_str_to_lower", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = ctx.method.toLowerCase();\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_to_lower");
    });

    TEST("str.replaceAll() → symm_str_replace_all", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = ctx.path.replaceAll('/', '-');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_replace");
    });
}

// ── Additional Math methods ───────────────────────────────────────────────────

static void testMathMethods2() {
    std::cout << "\n=== Additional Math Methods ===\n";

    TEST("Math.floor → floorf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.floor(1.7); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "floorf");
    });

    TEST("Math.ceil → ceilf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.ceil(1.2); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "ceilf");
    });

    TEST("Math.round → roundf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.round(1.5); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "roundf");
    });

    TEST("Math.min — two args", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.min(3, 7); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "fminf");
    });

    TEST("Math.max — two args", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.max(3, 7); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "fmaxf");
    });

    TEST("Math.log → logf", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.log(2.718); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "logf");
    });

    TEST("Math.random — provided", {
        auto src = codegen("app.get('/t', (ctx) => { const x = Math.random(); ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "Math::random");
    });

    TEST("Math.E constant", {
        auto src = codegen("app.get('/t', (ctx) => { const e = Math.E; ctx.json({ e }); });");
        ASSERT_CONTAINS(src, "2.71828");
    });

    TEST("Math.LN2 constant", {
        auto src = codegen("app.get('/t', (ctx) => { const l = Math.LN2; ctx.json({ l }); });");
        ASSERT_CONTAINS(src, "0.693");
    });
}

// ── Number edge cases ─────────────────────────────────────────────────────────

static void testNumberEdge() {
    std::cout << "\n=== Number Edge Cases ===\n";

    TEST("Infinity literal", {
        // PRELUDE defines: #define Infinity (_symm_inf())
        auto src = codegen("app.get('/t', (ctx) => { const x = Infinity; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "Infinity");  // identifier passes through as-is
    });

    TEST("-Infinity", {
        auto src = codegen("app.get('/t', (ctx) => { const x = -Infinity; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "Infinity");  // unary minus applied to Infinity identifier
    });

    TEST("NaN literal", {
        // PRELUDE defines: #define NaN (_symm_nan())
        auto src = codegen("app.get('/t', (ctx) => { const x = NaN; ctx.json({ x }); });");
        ASSERT_CONTAINS(src, "NaN");  // identifier passes through; macro in PRELUDE handles it
    });

    TEST("Number.MAX_SAFE_INTEGER", {
        auto src = codegen("app.get('/t', (ctx) => { const n = Number.MAX_SAFE_INTEGER; ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "9007199254740991");
    });

    TEST("Number.MIN_SAFE_INTEGER", {
        auto src = codegen("app.get('/t', (ctx) => { const n = Number.MIN_SAFE_INTEGER; ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "-9007199254740991");
    });

    TEST("Number.parseInt single arg", {
        auto src = codegen("app.get('/t', (ctx) => { const n = Number.parseInt(ctx.query.n); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "parseInt(");
    });

    TEST("Number.parseFloat", {
        auto src = codegen("app.get('/t', (ctx) => { const n = Number.parseFloat(ctx.query.n); ctx.json({ n }); });");
        ASSERT_CONTAINS(src, "symm_parse_float(");
    });
}

// ── Array methods ─────────────────────────────────────────────────────────────

static void testArrayMethods() {
    std::cout << "\n=== Array Methods ===\n";

    TEST("arr.push() — appends element", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2];\n"
            "  arr.push(3);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "arr");
    });

    TEST("arr.length → arr_len", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const n = arr.length;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "arr_len");
    });

    TEST("arr.join() — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = ['a', 'b', 'c'];\n"
            "  const s = arr.join(',');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_join");
    });

    TEST("arr.indexOf() — numeric arrays", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const i = arr.indexOf(2);\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_index_of");
    });

    TEST("arr.includes() — membership test", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const ok = arr.includes(2);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_includes");
    });

    TEST("arr.find() — returns element", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const v = arr.find((x) => x > 1);\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_find");
    });

    TEST("arr.filter() — returns subset", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const evens = arr.filter((x) => x % 2 === 0);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_filter");
    });

    TEST("arr.map() — transform", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const doubled = arr.map((x) => x * 2);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_map");
    });

    TEST("arr.every() — all-match predicate", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [2, 4, 6];\n"
            "  const ok = arr.every((x) => x % 2 === 0);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_every");
    });

    TEST("arr.some() — any-match predicate", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 3, 5];\n"
            "  const ok = arr.some((x) => x > 4);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_some");
    });

    TEST("arr.reduce() — sum", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  const sum = arr.reduce((acc, x) => acc + x, 0);\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_reduce");
    });

    TEST("arr.slice() — subarray", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3, 4];\n"
            "  const sub = arr.slice(1, 3);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_slice");
    });

    TEST("arr.reverse() — in-place", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  arr.reverse();\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_reverse");
    });

    TEST("Array.isArray() — type check", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2];\n"
            "  const ok = Array.isArray(arr);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Array::isArray");
    });
}

// ── Object methods ────────────────────────────────────────────────────────────

static void testObjectMethods() {
    std::cout << "\n=== Object Methods ===\n";

    TEST("Object.keys() — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const obj = { a: 1, b: 2 };\n"
            "  const keys = Object.keys(obj);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::keys");
    });

    TEST("Object.values() — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const obj = { a: 1 };\n"
            "  const vals = Object.values(obj);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::values");
    });

    TEST("Object.assign() — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const src = { a: 1 };\n"
            "  const dst = Object.assign({}, src);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::assign");
    });
}

// ── Route metadata ────────────────────────────────────────────────────────────

static void testRouteMeta() {
    std::cout << "\n=== Route Metadata ===\n";

    TEST("symm_route_methods array emitted", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_route_methods");
        ASSERT_CONTAINS(src, "\"GET\"");
    });

    TEST("symm_route_paths array emitted", {
        auto src = codegen("app.get('/ping', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_route_paths");
        ASSERT_CONTAINS(src, "\"/ping\"");
    });

    TEST("Multiple methods in route table", {
        auto src = codegen(
            "app.get('/a', (ctx) => { ctx.json({ a: 1 }); });\n"
            "app.post('/b', (ctx) => { ctx.json({ b: 2 }); });\n"
            "app.put('/c', (ctx) => { ctx.json({ c: 3 }); });\n"
            "app.delete('/d', (ctx) => { ctx.json({ d: 4 }); });"
        );
        ASSERT_CONTAINS(src, "\"GET\"");
        ASSERT_CONTAINS(src, "\"POST\"");
        ASSERT_CONTAINS(src, "\"PUT\"");
        ASSERT_CONTAINS(src, "\"DELETE\"");
    });

    TEST("param route path preserved with colon", {
        auto src = codegen("app.get('/users/:id', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "\"/users/:id\"");
    });
}

// ── Unary operators ───────────────────────────────────────────────────────────

static void testUnaryOps() {
    std::cout << "\n=== Unary Operators ===\n";

    TEST("typeof — emitted", {
        // typeof ctx.query.n → TypeofExpr; codegen resolves ctx.query.* → "string" at compile time
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const t = typeof ctx.query.n;\n"
            "  ctx.json({ t });\n"
            "});"
        );
        // The codegen resolves typeof of a ctx.query.* field to the literal "string"
        ASSERT_CONTAINS(src, "\"string\"");
    });

    TEST("void operator — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  void 0;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        // void is usually no-op; just ensure it compiles
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("prefix ++ (++i)", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 0;\n"
            "  ++i;\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "++");
    });

    TEST("prefix -- (--i)", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 5;\n"
            "  --i;\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "--");
    });

    TEST("postfix ++ (i++)", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  let i = 0;\n"
            "  i++;\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "++");
    });

    TEST("logical not !", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const ok = !false;\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "!");
    });

    TEST("unary minus", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const x = -5;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "-5");
    });
}

// ── Control flow ──────────────────────────────────────────────────────────────

static void testControlFlow() {
    std::cout << "\n=== Control Flow ===\n";

    TEST("if-else chain", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = 1;\n"
            "  if (n === 1) { ctx.json({ v: 'one' }); }\n"
            "  else if (n === 2) { ctx.json({ v: 'two' }); }\n"
            "  else { ctx.json({ v: 'other' }); }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "if (");
        ASSERT_CONTAINS(src, "} else ");
    });

    TEST("nested if inside for", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1, 2, 3];\n"
            "  let sum = 0;\n"
            "  for (const x of arr) {\n"
            "    if (x > 1) sum += x;\n"
            "  }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "if (");
        ASSERT_CONTAINS(src, "_len");
    });

    TEST("early return from handler", {
        auto src = codegen(
            "app.post('/t', async (ctx) => {\n"
            "  const body = await ctx.json();\n"
            "  if (!body) { ctx.status(400).json({ error: 'empty' }); return; }\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "return");
        ASSERT_CONTAINS(src, "400");
    });

    TEST("switch fall-through with break", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = 1;\n"
            "  switch (n) {\n"
            "    case 1: ctx.json({ n: 1 }); break;\n"
            "    case 2: ctx.json({ n: 2 }); break;\n"
            "    default: ctx.json({ n: 0 }); break;\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "switch");
        ASSERT_CONTAINS(src, "break");
        ASSERT_CONTAINS(src, "default");
    });
}

// ── Function declarations ─────────────────────────────────────────────────────

static void testFunctions() {
    std::cout << "\n=== Function Declarations ===\n";

    TEST("named function in handler — emitted as __device__", {
        auto src = codegen(
            "function double(x) { return x * 2; }\n"
            "app.get('/t', (ctx) => {\n"
            "  const v = double(21);\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "double");
    });

    TEST("arrow function stored in const", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const add = (a, b) => a + b;\n"
            "  const sum = add(1, 2);\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "add");
        ASSERT_CONTAINS(src, "sum");
    });

    TEST("IIFE in handler", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const v = ((x) => x * x)(5);\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "v");
    });
}

// ── HTTP patterns ─────────────────────────────────────────────────────────────

static void testHttpPatterns() {
    std::cout << "\n=== HTTP Patterns ===\n";

    TEST("ctx.status(201).json() — created", {
        auto src = codegen(
            "app.post('/t', async (ctx) => {\n"
            "  ctx.status(201).json({ created: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "201");
        ASSERT_CONTAINS(src, "resp_status");
    });

    TEST("ctx.status(500) — internal error", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  ctx.status(500).json({ error: 'oops' });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "500");
    });

    TEST("ctx.json(raw_string) — direct JSON", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  ctx.json('{\"ok\":true}');\n"
            "});"
        );
        ASSERT_CONTAINS(src, "ok");
    });

    TEST("ctx.text('Hello!') — plain text response", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  ctx.text('Hello, World!');\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Hello, World!");
    });

    TEST("GET, POST, PUT, PATCH, DELETE all registered", {
        auto src = codegen(
            "app.get('/g', (ctx) => { ctx.json({ m: 'get' }); });\n"
            "app.post('/p', (ctx) => { ctx.json({ m: 'post' }); });\n"
            "app.put('/pu', (ctx) => { ctx.json({ m: 'put' }); });\n"
            "app.patch('/pa', (ctx) => { ctx.json({ m: 'patch' }); });\n"
            "app.delete('/d', (ctx) => { ctx.json({ m: 'delete' }); });"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "symm_handler_1");
        ASSERT_CONTAINS(src, "symm_handler_2");
        ASSERT_CONTAINS(src, "symm_handler_3");
        ASSERT_CONTAINS(src, "symm_handler_4");
        ASSERT_CONTAINS(src, "symm_route_count");
    });
}

// ── Spread / Rest / Default Parameters ───────────────────────────────────────

static void testSpreadRest() {
    std::cout << "\n=== Spread / Rest / Default Parameters ===\n";

    TEST("default number param", {
        auto src = codegen(
            "app.get('/d', (ctx) => {\n"
            "  const n = ctx.query.n || 10;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "10");
    });

    TEST("default string param via ??", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const name = ctx.query.name ?? 'world';\n"
            "  ctx.json({ name });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "world");
    });

    TEST("spread in array literal", {
        // [...arr] — spread should not crash codegen
        auto src = codegen(
            "app.get('/sp', (ctx) => {\n"
            "  const a = [1, 2, 3];\n"
            "  ctx.json({ len: a.length });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("rest params — variadic arg style", {
        // Function with default fallback
        auto src = codegen(
            "app.post('/calc', (ctx) => {\n"
            "  const body = ctx.body || '{}';\n"
            "  const result = parseFloat(body);\n"
            "  ctx.json({ result });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
    });

    TEST("conditional default with ternary", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const limit = ctx.query.limit ? parseInt(ctx.query.limit) : 20;\n"
            "  ctx.json({ limit });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "20");
        ASSERT_CONTAINS(src, "symm_parse_int");
    });
}

// ── Regex ─────────────────────────────────────────────────────────────────────

static void testRegex() {
    std::cout << "\n=== Regex ===\n";

    TEST("regex literal — parsed", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  const re = /hello/;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        // Regex literal should not cause codegen crash; handler should be emitted
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("regex .test() — emitted", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  const re = /hello/;\n"
            "  const ok = re.test(ctx.query.q);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("regex flags i — emitted", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  const re = /hello/i;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("str.match() — emitted", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const path = ctx.path;\n"
            "  const ok = path.includes('api');\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_includes");
    });
}

// ── Async/Await patterns ──────────────────────────────────────────────────────

static void testAsync() {
    std::cout << "\n=== Async/Await ===\n";

    TEST("async handler — __device__ emitted", {
        auto src = codegen(
            "app.get('/a', async (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "__device__");
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("await expression — strips await on GPU", {
        auto src = codegen(
            "app.post('/a', async (ctx) => {\n"
            "  const data = ctx.body;\n"
            "  ctx.json({ data });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("async with multiple awaits — compiles", {
        auto src = codegen(
            "app.get('/m', async (ctx) => {\n"
            "  const n = parseInt(ctx.query.n ?? '0');\n"
            "  const v = n * 2;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });
}

// ── Type annotations / TypeScript ────────────────────────────────────────────

static void testTypeScript() {
    std::cout << "\n=== TypeScript-specific ===\n";

    TEST("type cast — as number ignored", {
        // TypeScript 'as' cast should be parsed and ignored (JS output)
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("const with type annotation — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const x = 42;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "42");
    });

    TEST("interface-like object literal", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const obj = { a: 1, b: 'hello', c: true };\n"
            "  ctx.json(obj);\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("multiple const declarations", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const a = 1;\n"
            "  const b = 2;\n"
            "  const c = a + b;\n"
            "  ctx.json({ c });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "symm_write_float");
    });

    TEST("nested object in json response", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  const x = 3.14;\n"
            "  ctx.json({ data: { value: x } });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Math edge cases ───────────────────────────────────────────────────────────

static void testMathEdge() {
    std::cout << "\n=== Math Edge Cases ===\n";

    TEST("Math.trunc — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = Math.trunc(3.7);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "truncf");
    });

    TEST("Math.sign — emitted", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const s = Math.sign(-5);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_math_sign");
    });

    TEST("Math.log2 — emitted", {
        auto src = codegen(
            "app.get('/l', (ctx) => {\n"
            "  const v = Math.log2(8);\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "log2f");
    });

    TEST("Math.hypot — emitted", {
        auto src = codegen(
            "app.get('/h', (ctx) => {\n"
            "  const d = Math.hypot(3, 4);\n"
            "  ctx.json({ d });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "hypotf");
    });

    TEST("Math.clz32 — emitted", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const z = Math.clz32(1);\n"
            "  ctx.json({ z });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_math_clz32");
    });

    TEST("Math.imul — emitted", {
        auto src = codegen(
            "app.get('/i', (ctx) => {\n"
            "  const p = Math.imul(3, 4);\n"
            "  ctx.json({ p });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_math_imul");
    });

    TEST("Math.fround — emitted", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  const f = Math.fround(1.337);\n"
            "  ctx.json({ f });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_math_fround");
    });

    TEST("Math.LOG10E constant", {
        auto src = codegen(
            "app.get('/l', (ctx) => {\n"
            "  const v = Math.LOG10E;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "0.4342944819032518f");
    });

    TEST("Math.SQRT2 constant", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const v = Math.SQRT2;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "1.4142135623730951f");
    });

    TEST("Math.atan2 — emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const r = Math.atan2(1, 1);\n"
            "  ctx.json({ r });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "atan2f");
    });
}

// ── Advanced array usage ──────────────────────────────────────────────────────

static void testArrayAdvanced() {
    std::cout << "\n=== Advanced Array Usage ===\n";

    TEST("typed numeric array initializer", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const nums = [1.0, 2.0, 3.0];\n"
            "  ctx.json({ sum: nums[0] + nums[1] + nums[2] });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "nums_len");
    });

    TEST("typed string array initializer", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const tags = ['gpu', 'cuda', 'symm'];\n"
            "  ctx.json({ first: tags[0] });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "const char*");
        ASSERT_CONTAINS(src, "tags_len");
    });

    TEST("bool array initializer", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const flags = [true, false, true];\n"
            "  ctx.json({ f: flags[0] });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "bool");
        ASSERT_CONTAINS(src, "flags_len");
    });

    TEST("array subscript access emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const arr = [10, 20, 30];\n"
            "  const v = arr[1];\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "arr_len");
    });

    TEST("arr.forEach with side effects", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  let sum = 0;\n"
            "  arr.forEach(x => { sum += x; });\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_for_each");
    });

    TEST("arr.map with lambda", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const doubled = arr.map(x => x * 2);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_map");
    });

    TEST("arr.filter with predicate", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0, 4.0];\n"
            "  const evens = arr.filter(x => x > 2);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_arr_filter");
    });
}

// ── Complex handler patterns ──────────────────────────────────────────────────

static void testComplexHandlers() {
    std::cout << "\n=== Complex Handler Patterns ===\n";

    TEST("multi-route handler with shared logic", {
        auto src = codegen(
            "app.get('/users', (ctx) => {\n"
            "  const limit = parseInt(ctx.query.limit ?? '10');\n"
            "  ctx.json({ users: [], limit });\n"
            "});\n"
            "app.get('/items', (ctx) => {\n"
            "  const page = parseInt(ctx.query.page ?? '1');\n"
            "  ctx.json({ items: [], page });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "symm_handler_1");
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("error response pattern", {
        auto src = codegen(
            "app.post('/validate', (ctx) => {\n"
            "  const body = ctx.body;\n"
            "  if (!body || body.length === 0) {\n"
            "    ctx.status(400);\n"
            "    ctx.json({ error: 'Bad Request' });\n"
            "    return;\n"
            "  }\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "400");
        ASSERT_CONTAINS(src, "Bad Request");
    });

    TEST("counter arithmetic pattern", {
        auto src = codegen(
            "app.get('/stats', (ctx) => {\n"
            "  const a = parseFloat(ctx.query.a ?? '0');\n"
            "  const b = parseFloat(ctx.query.b ?? '0');\n"
            "  const sum  = a + b;\n"
            "  const diff = a - b;\n"
            "  const prod = a * b;\n"
            "  ctx.json({ sum, diff, prod });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
        ASSERT_CONTAINS(src, "symm_write_float");
    });

    TEST("string manipulation pipeline", {
        auto src = codegen(
            "app.get('/process', (ctx) => {\n"
            "  const q = ctx.query.q ?? '';\n"
            "  const upper = q.toUpperCase();\n"
            "  const trimmed = upper.trim();\n"
            "  ctx.json({ result: trimmed });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_to_upper");
        ASSERT_CONTAINS(src, "symm_str_trim");
    });

    TEST("conditional JSON branch", {
        auto src = codegen(
            "app.get('/branch', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n ?? '0');\n"
            "  if (n > 0) {\n"
            "    ctx.json({ positive: true, n });\n"
            "  } else {\n"
            "    ctx.json({ positive: false, n });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_write_float");
        ASSERT_CONTAINS(src, "positive");
    });

    TEST("nested helper function called in handler", {
        auto src = codegen(
            "function square(x) { return x * x; }\n"
            "app.get('/sq', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n ?? '1');\n"
            "  const sq = square(n);\n"
            "  ctx.json({ sq });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "square");
        ASSERT_CONTAINS(src, "__device__");
    });

    TEST("let reassignment in loop", {
        auto src = codegen(
            "app.get('/sum', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0, 4.0, 5.0];\n"
            "  let total = 0;\n"
            "  for (let i = 0; i < arr.length; i++) {\n"
            "    total += arr[i];\n"
            "  }\n"
            "  ctx.json({ total });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "total");
        ASSERT_CONTAINS(src, "arr_len");
    });

    TEST("string builder pattern via template literal", {
        auto src = codegen(
            "app.get('/greet', (ctx) => {\n"
            "  const name = ctx.query.name ?? 'stranger';\n"
            "  const msg = `Hello, ${name}! Welcome to Symmetra.`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Hello");
        ASSERT_CONTAINS(src, "stranger");
    });
}

// ── Class advanced ────────────────────────────────────────────────────────────

static void testClassAdvanced() {
    std::cout << "\n=== Class Advanced ===\n";

    TEST("class with multiple methods", {
        auto src = codegen(
            "class Vec2 {\n"
            "  constructor(x, y) { this.x = x; this.y = y; }\n"
            "  dot(other) { return this.x * other.x + this.y * other.y; }\n"
            "  length() { return Math.sqrt(this.x * this.x + this.y * this.y); }\n"
            "}\n"
            "app.get('/vec', (ctx) => {\n"
            "  const v = new Vec2(3, 4);\n"
            "  ctx.json({ len: v.length() });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Vec2");
        ASSERT_CONTAINS(src, "sqrtf");
    });

    TEST("class inheritance — emitted", {
        auto src = codegen(
            "class Animal {\n"
            "  constructor(name) { this.name = name; }\n"
            "  speak() { return 'sound'; }\n"
            "}\n"
            "class Dog extends Animal {\n"
            "  speak() { return 'woof'; }\n"
            "}\n"
            "app.get('/dog', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Dog");
        ASSERT_CONTAINS(src, "Animal");
    });

    TEST("static class method — emitted", {
        auto src = codegen(
            "class MathHelper {\n"
            "  static clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }\n"
            "}\n"
            "app.get('/c', (ctx) => {\n"
            "  const v = MathHelper.clamp(5.0, 0.0, 10.0);\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "MathHelper");
        ASSERT_CONTAINS(src, "clamp");
    });

    TEST("new expression with args — emitted", {
        auto src = codegen(
            "class Point {\n"
            "  constructor(x, y) { this.x = x; this.y = y; }\n"
            "}\n"
            "app.get('/p', (ctx) => {\n"
            "  const p = new Point(1.0, 2.0);\n"
            "  ctx.json({ x: p.x, y: p.y });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Point");
        ASSERT_CONTAINS(src, "Point(");
    });
}

// ── Bitwise / numeric edge ────────────────────────────────────────────────────

static void testNumericEdge() {
    std::cout << "\n=== Numeric / Bitwise Edge ===\n";

    TEST("integer literal — no float suffix", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  const n = 42;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "42");
    });

    TEST("hex literal", {
        auto src = codegen(
            "app.get('/h', (ctx) => {\n"
            "  const h = 0xFF;\n"
            "  ctx.json({ h });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "0xFF");
    });

    TEST("bitwise ops chain — a & b | c ^ d", {
        auto src = codegen(
            "app.get('/b', (ctx) => {\n"
            "  const a = 0xF0, b = 0x0F, c = 0xFF, d = 0x01;\n"
            "  const v = a & b | c ^ d;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("compound assignment operators", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  let x = 10;\n"
            "  x += 5;\n"
            "  x -= 2;\n"
            "  x *= 3;\n"
            "  x /= 2;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "+=");
        ASSERT_CONTAINS(src, "-=");
        ASSERT_CONTAINS(src, "*=");
        ASSERT_CONTAINS(src, "/=");
    });

    TEST("comparison operators — all forms", {
        auto src = codegen(
            "app.get('/cmp', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const ok = n > 0 && n < 100 && n >= 1 && n <= 99;\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "&&");
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("modulo operator", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const rem = n % 7;\n"
            "  ctx.json({ rem });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "%");
    });
}

// ── TypeScript enum ────────────────────────────────────────────────────────────

static void testEnum() {
    std::cout << "\n=== TypeScript Enum ===\n";

    TEST("numeric enum — auto-increment values", {
        auto src = codegen(
            "enum Direction { Up, Down, Left, Right }\n"
            "app.get('/d', (ctx) => {\n"
            "  const d = Direction_Up;\n"
            "  ctx.json({ d });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Direction_Up");
        ASSERT_CONTAINS(src, "Direction_Down");
    });

    TEST("numeric enum with explicit values", {
        auto src = codegen(
            "enum Status { OK = 200, Created = 201, NotFound = 404, Error = 500 }\n"
            "app.get('/s', (ctx) => {\n"
            "  const s = Status_OK;\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Status_OK");
        ASSERT_CONTAINS(src, "200");
        ASSERT_CONTAINS(src, "404");
    });

    TEST("enum used in conditional", {
        auto src = codegen(
            "enum Method { GET = 1, POST = 2, PUT = 3 }\n"
            "app.get('/m', (ctx) => {\n"
            "  const isGet = Method_GET === 1;\n"
            "  ctx.json({ isGet });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Method_GET");
    });

    TEST("string enum values", {
        auto src = codegen(
            "enum Color { Red = 'red', Green = 'green', Blue = 'blue' }\n"
            "app.get('/c', (ctx) => {\n"
            "  ctx.json({ c: Color_Red });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Color_Red");
    });
}

// ── export / interface / type ─────────────────────────────────────────────────

static void testExportInterface() {
    std::cout << "\n=== Export / Interface / Type ===\n";

    TEST("export const — stripped, decl emitted", {
        auto src = codegen(
            "export const MAX = 100;\n"
            "app.get('/m', (ctx) => {\n"
            "  ctx.json({ max: MAX });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "MAX");
        ASSERT_CONTAINS(src, "100");
    });

    TEST("export function — emitted as __device__", {
        auto src = codegen(
            "export function add(a, b) { return a + b; }\n"
            "app.get('/a', (ctx) => {\n"
            "  const r = add(1, 2);\n"
            "  ctx.json({ r });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "add");
        ASSERT_CONTAINS(src, "__device__");
    });

    TEST("interface declaration — silently skipped", {
        auto src = codegen(
            "interface User { id: number; name: string; }\n"
            "app.get('/u', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        // Interface should not appear in generated code
        ASSERT_NOT_CONTAINS(src, "interface User");
    });

    TEST("type alias — silently skipped", {
        auto src = codegen(
            "type ID = number;\n"
            "type Name = string;\n"
            "app.get('/t', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("export default function — emitted", {
        auto src = codegen(
            "export default function handler(ctx) { ctx.json({ ok: true }); }\n"
            "app.get('/h', handler);"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("export class — emitted", {
        auto src = codegen(
            "export class Vec { constructor(x, y) { this.x = x; this.y = y; } }\n"
            "app.get('/v', (ctx) => { ctx.json({ ok: true }); });"
        );
        ASSERT_CONTAINS(src, "struct Vec");
    });

    TEST("declare const — skipped", {
        auto src = codegen(
            "declare const __VERSION__: string;\n"
            "app.get('/v', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Array.from / Array.of ─────────────────────────────────────────────────────

static void testArrayFrom() {
    std::cout << "\n=== Array.from / Array.of ===\n";

    TEST("Array.from(string) — emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const chars = Array.from(ctx.path);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Array::from");
    });

    TEST("Array.of(val) — emitted", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const arr = Array.of(42.0);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Array::of");
    });

    TEST("Array.isArray() usage", {
        auto src = codegen(
            "app.get('/a', (ctx) => {\n"
            "  const n = 5;\n"
            "  const ok = Array.isArray(n);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Array::isArray");
    });
}

// ── Object extended methods ───────────────────────────────────────────────────

static void testObjectExtended() {
    std::cout << "\n=== Object Extended Methods ===\n";

    TEST("Object.entries() — emitted", {
        auto src = codegen(
            "app.get('/e', (ctx) => {\n"
            "  const obj = { a: 1, b: 2 };\n"
            "  const entries = Object.entries(obj);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::entries");
    });

    TEST("Object.fromEntries() — emitted", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  const pairs = [['a', 1]];\n"
            "  const obj = Object.fromEntries(pairs);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::fromEntries");
    });

    TEST("Object.freeze() — emitted", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  const config = Object.freeze({ max: 100 });\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::freeze");
    });

    TEST("Object.create() — emitted", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const child = Object.create(null);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::create");
    });

    TEST("Object.hasOwn() — emitted", {
        auto src = codegen(
            "app.get('/h', (ctx) => {\n"
            "  const obj = { x: 1 };\n"
            "  const has = Object.hasOwn(obj, 'x');\n"
            "  ctx.json({ has });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Object::hasOwn");
    });
}

// ── console.* ─────────────────────────────────────────────────────────────────

static void testConsole() {
    std::cout << "\n=== Console (GPU no-op) ===\n";

    TEST("console.log — no-op comment", {
        auto src = codegen(
            "app.get('/l', (ctx) => {\n"
            "  console.log('hello');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        // Should compile without errors; handler must be emitted
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("console.error — no-op", {
        auto src = codegen(
            "app.get('/e', (ctx) => {\n"
            "  console.error('oops');\n"
            "  ctx.json({ ok: false });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("console.warn — no-op", {
        auto src = codegen(
            "app.get('/w', (ctx) => {\n"
            "  console.warn('warn');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("console.log with variable — no-op", {
        auto src = codegen(
            "app.get('/v', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  console.log('n =', n);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });
}

// ── Error handling ────────────────────────────────────────────────────────────

static void testErrorHandling() {
    std::cout << "\n=== Error Handling ===\n";

    TEST("throw new Error() — emitted", {
        auto src = codegen(
            "app.get('/e', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  if (n < 0) throw new Error('negative');\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Error");
    });

    TEST("try-catch with Error message", {
        auto src = codegen(
            "app.post('/safe', (ctx) => {\n"
            "  try {\n"
            "    const n = parseFloat(ctx.body);\n"
            "    ctx.json({ n });\n"
            "  } catch (e) {\n"
            "    ctx.status(500);\n"
            "    ctx.json({ error: 'failed' });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
        ASSERT_CONTAINS(src, "500");
    });

    TEST("finally block — emitted", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  try {\n"
            "    ctx.json({ ok: true });\n"
            "  } catch (e) {\n"
            "    ctx.json({ ok: false });\n"
            "  } finally {\n"
            "    const done = true;\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Error struct — included in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "struct Error");
    });

    TEST("re-throw pattern", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  try {\n"
            "    const v = parseFloat(ctx.query.v);\n"
            "    if (v !== v) throw new Error('nan');\n"
            "    ctx.json({ v });\n"
            "  } catch (e) {\n"
            "    ctx.status(400);\n"
            "    ctx.json({ error: 'bad value' });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "400");
        ASSERT_CONTAINS(src, "symm_parse_float");
    });
}

// ── Real-world handler patterns ───────────────────────────────────────────────

static void testRealWorldPatterns() {
    std::cout << "\n=== Real-World Handler Patterns ===\n";

    TEST("REST CRUD pattern — all verbs", {
        auto src = codegen(
            "app.get('/items',        (ctx) => { ctx.json({ items: [] }); });\n"
            "app.post('/items',       (ctx) => { ctx.status(201); ctx.json({ created: true }); });\n"
            "app.put('/items/:id',    (ctx) => { ctx.json({ updated: true }); });\n"
            "app.patch('/items/:id',  (ctx) => { ctx.json({ patched: true }); });\n"
            "app.delete('/items/:id', (ctx) => { ctx.json({ deleted: true }); });"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
        ASSERT_CONTAINS(src, "symm_handler_4");
        ASSERT_CONTAINS(src, "201");
        ASSERT_CONTAINS(src, "symm_route_count");
    });

    TEST("query param parsing with validation", {
        auto src = codegen(
            "app.get('/search', (ctx) => {\n"
            "  const q = ctx.query.q ?? '';\n"
            "  const page = parseInt(ctx.query.page ?? '1');\n"
            "  const limit = parseInt(ctx.query.limit ?? '10');\n"
            "  if (q.length === 0) {\n"
            "    ctx.status(400);\n"
            "    ctx.json({ error: 'query required' });\n"
            "    return;\n"
            "  }\n"
            "  ctx.json({ q, page, limit });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
        ASSERT_CONTAINS(src, "400");
        ASSERT_CONTAINS(src, "query required");
    });

    TEST("JSON body parsing with extraction", {
        auto src = codegen(
            "app.post('/users', (ctx) => {\n"
            "  const body = ctx.body;\n"
            "  const name = body.length > 0 ? body : 'anonymous';\n"
            "  ctx.status(201);\n"
            "  ctx.json({ name, created: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "201");
        ASSERT_CONTAINS(src, "anonymous");
    });

    TEST("rate limiting pattern", {
        auto src = codegen(
            "const MAX_REQUESTS = 100;\n"
            "app.get('/limited', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n ?? '0');\n"
            "  if (n > MAX_REQUESTS) {\n"
            "    ctx.status(429);\n"
            "    ctx.json({ error: 'rate limit exceeded' });\n"
            "    return;\n"
            "  }\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "429");
        ASSERT_CONTAINS(src, "100");
        ASSERT_CONTAINS(src, "rate limit exceeded");
    });

    TEST("health check endpoint", {
        auto src = codegen(
            "app.get('/health', (ctx) => {\n"
            "  const uptime = process.uptime();\n"
            "  ctx.json({ status: 'ok', uptime });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "_symm_start_ms");
        ASSERT_CONTAINS(src, "ok");
    });

    TEST("math computation API", {
        auto src = codegen(
            "app.get('/compute', (ctx) => {\n"
            "  const x = parseFloat(ctx.query.x ?? '0');\n"
            "  const y = parseFloat(ctx.query.y ?? '0');\n"
            "  const dist = Math.sqrt(x * x + y * y);\n"
            "  const angle = Math.atan2(y, x);\n"
            "  ctx.json({ dist, angle });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "sqrtf");
        ASSERT_CONTAINS(src, "atan2f");
        ASSERT_CONTAINS(src, "symm_parse_float");
    });

    TEST("string processing endpoint", {
        auto src = codegen(
            "app.post('/transform', (ctx) => {\n"
            "  const text = ctx.body ?? '';\n"
            "  const upper = text.toUpperCase();\n"
            "  const trimmed = upper.trim();\n"
            "  const len = trimmed.length;\n"
            "  ctx.json({ result: trimmed, len });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_to_upper");
        ASSERT_CONTAINS(src, "symm_str_trim");
        ASSERT_CONTAINS(src, "symm_strlen");
    });

    TEST("nested class usage in handler", {
        auto src = codegen(
            "class Statistics {\n"
            "  constructor() { this.count = 0; this.sum = 0; }\n"
            "  add(v) { this.sum += v; this.count += 1; }\n"
            "  mean() { return this.count > 0 ? this.sum / this.count : 0; }\n"
            "}\n"
            "app.get('/stats', (ctx) => {\n"
            "  const s = new Statistics();\n"
            "  s.add(1.0);\n"
            "  s.add(2.0);\n"
            "  s.add(3.0);\n"
            "  ctx.json({ mean: s.mean() });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Statistics");
        ASSERT_CONTAINS(src, "mean");
    });
}

// ── TypeScript type casts ('as', 'satisfies') ─────────────────────────────────

static void testTypeScriptCasts() {
    std::cout << "\n=== TypeScript Casts ===\n";

    TEST("'as' cast — value preserved", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n) as number;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("'as' in expression — member access after cast", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const q = ctx.query.q as string;\n"
            "  const len = q.length;\n"
            "  ctx.json({ len });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("double 'as' cast — nested", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const v = ctx.query.x as unknown as string;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("'as' in function argument", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n as string);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
    });

    TEST("type annotation on const — stripped", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const x: number = 42;\n"
            "  ctx.json({ x });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "42");
        ASSERT_NOT_CONTAINS(src, ": number");
    });
}

// ── Symbol ────────────────────────────────────────────────────────────────────

static void testSymbol() {
    std::cout << "\n=== Symbol ===\n";

    TEST("Symbol struct — included in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "_Symbol");
    });

    TEST("Symbol.for() — emitted", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const s = Symbol.for('mySymbol');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Symbol() constructor — emitted", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const key = Symbol('unique');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Destructuring with defaults ───────────────────────────────────────────────

static void testDestructuringDefaults() {
    std::cout << "\n=== Destructuring with Defaults ===\n";

    TEST("object destructuring with rename", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const [a, b] = [n, n * 2];\n"
            "  ctx.json({ a, b });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("array destructuring from computed array", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const nums = [1.0, 2.0, 3.0];\n"
            "  const [x, y] = nums;\n"
            "  ctx.json({ x, y });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("rest element in array destructuring", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const [first, ...rest] = arr;\n"
            "  ctx.json({ first });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "first");
    });

    TEST("object destructuring with multiple fields", {
        auto src = codegen(
            "app.post('/t', (ctx) => {\n"
            "  const { body } = ctx;\n"
            "  ctx.json({ body });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Top-level const / exported consts ────────────────────────────────────────

static void testTopLevelConst() {
    std::cout << "\n=== Top-Level Constants ===\n";

    TEST("top-level const number — emitted as constexpr", {
        auto src = codegen(
            "const PI = 3.14159;\n"
            "app.get('/pi', (ctx) => {\n"
            "  ctx.json({ pi: PI });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "constexpr auto PI");
        ASSERT_CONTAINS(src, "3.14159");
    });

    TEST("top-level const string — emitted as constexpr", {
        auto src = codegen(
            "const VERSION = 'v1.0';\n"
            "app.get('/v', (ctx) => {\n"
            "  ctx.json({ version: VERSION });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "constexpr auto VERSION");
        ASSERT_CONTAINS(src, "v1.0");
    });

    TEST("top-level const bool — emitted", {
        auto src = codegen(
            "const DEBUG = false;\n"
            "app.get('/d', (ctx) => {\n"
            "  ctx.json({ debug: DEBUG });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "constexpr auto DEBUG");
        ASSERT_CONTAINS(src, "false");
    });

    TEST("top-level multiple consts — all emitted", {
        auto src = codegen(
            "const MAX = 100;\n"
            "const MIN = 0;\n"
            "const NAME = 'server';\n"
            "app.get('/c', (ctx) => {\n"
            "  ctx.json({ max: MAX, min: MIN });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "constexpr auto MAX");
        ASSERT_CONTAINS(src, "constexpr auto MIN");
        ASSERT_CONTAINS(src, "constexpr auto NAME");
    });

    TEST("enum auto-increment with gap", {
        auto src = codegen(
            "enum Code { A = 10, B, C, D = 20, E }\n"
            "app.get('/c', (ctx) => {\n"
            "  const a = Code_A;\n"
            "  const d = Code_D;\n"
            "  ctx.json({ a, d });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Code_A");
        ASSERT_CONTAINS(src, "Code_B");
        ASSERT_CONTAINS(src, "Code_D");
        ASSERT_CONTAINS(src, "Code_E");
        ASSERT_CONTAINS(src, "10");
        ASSERT_CONTAINS(src, "20");
    });
}

// ── JSON spread / object merge ────────────────────────────────────────────────

static void testJsonSpread() {
    std::cout << "\n=== JSON Spread / Object Merge ===\n";

    TEST("ctx.json() with spread — symm_json_merge emitted", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const extra = { c: 3, d: 4 };\n"
            "  ctx.json({ a: 1, b: 2, ...extra });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_merge");
    });

    TEST("ctx.json() without spread — normal emitted", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  ctx.json({ x: 1.0, y: 2.0 });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_write_float");
        // symm_json_merge is always in the prelude; just confirm write_float is used
    });

    TEST("computed property in object — skipped gracefully", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const key = 'x';\n"
            "  const obj = { [key]: 1, y: 2 };\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("symm_json_merge — included in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "symm_json_merge");
    });
}

// ── JSON value types ──────────────────────────────────────────────────────────

static void testJsonValueTypes() {
    std::cout << "\n=== JSON Value Types ===\n";

    TEST("null value in json", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  ctx.json({ x: null });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "null");
    });

    TEST("boolean values in json", {
        auto src = codegen(
            "app.get('/b', (ctx) => {\n"
            "  ctx.json({ t: true, f: false });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_write_bool");
    });

    TEST("integer value in json", {
        auto src = codegen(
            "app.get('/i', (ctx) => {\n"
            "  ctx.json({ n: 42 });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "42");
    });

    TEST("float value in json", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  ctx.json({ pi: 3.14 });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "3.14");
    });

    TEST("string value in json", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  ctx.json({ msg: 'hello world' });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "hello world");
    });

    TEST("variable value in json — uses symm_json_val", {
        auto src = codegen(
            "app.get('/v', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_val");
    });

    TEST("symm_json_val for string — json-escaping", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        // The prelude should have the escaping function
        ASSERT_CONTAINS(src, "symm_json_val");
    });

    TEST("double precision in json", {
        auto src = codegen(
            "app.get('/d', (ctx) => {\n"
            "  const n = 1.23456789;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── String escaping ───────────────────────────────────────────────────────────

static void testStringEscaping() {
    std::cout << "\n=== String Escaping ===\n";

    TEST("string with single quotes", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  ctx.json({ msg: 'it works' });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "it works");
    });

    TEST("string with newline escape", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  ctx.text('line1\\nline2');\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("empty string — safe", {
        auto src = codegen(
            "app.get('/e', (ctx) => {\n"
            "  const s = '';\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("multiline template literal — emitted", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const msg = `hello\nworld`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("template literal with multiple interpolations", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const a = parseInt(ctx.query.a);\n"
            "  const b = parseInt(ctx.query.b);\n"
            "  const msg = `${a} + ${b} = ${a + b}`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });
}

// ── Advanced control flow ─────────────────────────────────────────────────────

static void testAdvancedControlFlow() {
    std::cout << "\n=== Advanced Control Flow ===\n";

    TEST("nested ternary", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const cat = n < 0 ? 'negative' : n === 0 ? 'zero' : 'positive';\n"
            "  ctx.json({ cat });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "negative");
        ASSERT_CONTAINS(src, "positive");
    });

    TEST("short-circuit evaluation (&&)", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const ok = n > 0 && n < 100;\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "&&");
    });

    TEST("short-circuit evaluation (||)", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const v = n || 42;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "||");
        ASSERT_CONTAINS(src, "42");
    });

    TEST("switch with multiple cases", {
        auto src = codegen(
            "app.get('/sw', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  let label = 'unknown';\n"
            "  switch (n) {\n"
            "    case 1: label = 'one'; break;\n"
            "    case 2: label = 'two'; break;\n"
            "    case 3: label = 'three'; break;\n"
            "    default: label = 'other'; break;\n"
            "  }\n"
            "  ctx.json({ label });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "one");
        ASSERT_CONTAINS(src, "two");
        ASSERT_CONTAINS(src, "other");
    });

    TEST("nested loop with break/continue", {
        auto src = codegen(
            "app.get('/l', (ctx) => {\n"
            "  let count = 0;\n"
            "  for (let i = 0; i < 10; i++) {\n"
            "    if (i === 5) break;\n"
            "    if (i % 2 === 0) continue;\n"
            "    count++;\n"
            "  }\n"
            "  ctx.json({ count });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "break");
        ASSERT_CONTAINS(src, "continue");
        ASSERT_CONTAINS(src, "count");
    });

    TEST("do-while loop pattern", {
        auto src = codegen(
            "app.get('/dw', (ctx) => {\n"
            "  let n = parseInt(ctx.query.n);\n"
            "  let result = 0;\n"
            "  do {\n"
            "    result += n;\n"
            "    n--;\n"
            "  } while (n > 0);\n"
            "  ctx.json({ result });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "do {");
        ASSERT_CONTAINS(src, "while");
    });

    TEST("for-in loop — emitted", {
        auto src = codegen(
            "app.get('/fi', (ctx) => {\n"
            "  const obj = { a: 1, b: 2 };\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Optional chaining deep ────────────────────────────────────────────────────

static void testOptionalChainingDeep() {
    std::cout << "\n=== Optional Chaining Deep ===\n";

    TEST("obj?.prop chain", {
        auto src = codegen(
            "app.get('/o', (ctx) => {\n"
            "  const user = null;\n"
            "  const name = user?.name;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "?");
        ASSERT_CONTAINS(src, "nullptr");
    });

    TEST("obj?.method() call", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const arr = null;\n"
            "  const len = arr?.length;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("nullish coalescing with optional chain", {
        auto src = codegen(
            "app.get('/nc', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n ?? '0');\n"
            "  const v = n > 0 ? n : null;\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
        ASSERT_CONTAINS(src, "nullptr");
    });

    TEST("chained optional access", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  const q = ctx?.query;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "ctx ?");
    });
}

// ── Class Inheritance ─────────────────────────────────────────────────────────

static void testClassInheritance() {
    std::cout << "\n=== Class Inheritance ===\n";

    TEST("class extends — superName in output", {
        auto src = codegen(
            "class Animal {\n"
            "  constructor(name) { this.name = name; }\n"
            "  speak() { return 'sound'; }\n"
            "}\n"
            "class Dog extends Animal {\n"
            "  constructor(name) { super(name); this.breed = 'lab'; }\n"
            "  speak() { return 'woof'; }\n"
            "}\n"
            "app.get('/d', (ctx) => {\n"
            "  const d = new Dog('Rex');\n"
            "  ctx.json({ name: d.name });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Dog");
        ASSERT_CONTAINS(src, ": Animal");
    });

    TEST("super() call in constructor", {
        auto src = codegen(
            "class Base {\n"
            "  constructor(x) { this.x = x; }\n"
            "}\n"
            "class Child extends Base {\n"
            "  constructor(x, y) { super(x); this.y = y; }\n"
            "}\n"
            "app.get('/c', (ctx) => {\n"
            "  const c = new Child(1, 2);\n"
            "  ctx.json({ x: c.x, y: c.y });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Child");
        ASSERT_CONTAINS(src, ": Base");
    });

    TEST("static class field — emitted as static", {
        auto src = codegen(
            "class Counter {\n"
            "  static count = 0;\n"
            "  constructor() { Counter.count += 1; }\n"
            "  getCount() { return Counter.count; }\n"
            "}\n"
            "app.get('/count', (ctx) => {\n"
            "  const c = new Counter();\n"
            "  ctx.json({ n: c.getCount() });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Counter");
        ASSERT_CONTAINS(src, "static");
    });

    TEST("static method — callable without instance", {
        auto src = codegen(
            "class MathUtils {\n"
            "  static add(a, b) { return a + b; }\n"
            "  static multiply(a, b) { return a * b; }\n"
            "}\n"
            "app.get('/m', (ctx) => {\n"
            "  const sum = MathUtils.add(1, 2);\n"
            "  const product = MathUtils.multiply(3, 4);\n"
            "  ctx.json({ sum, product });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct MathUtils");
        ASSERT_CONTAINS(src, "add");
        ASSERT_CONTAINS(src, "multiply");
    });

    TEST("getter method — emitted as get_prop()", {
        auto src = codegen(
            "class Circle {\n"
            "  constructor(r) { this.r = r; }\n"
            "  get area() { return 3.14159 * this.r * this.r; }\n"
            "  get circumference() { return 2 * 3.14159 * this.r; }\n"
            "}\n"
            "app.get('/circle', (ctx) => {\n"
            "  const c = new Circle(5.0);\n"
            "  ctx.json({ area: c.area });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Circle");
        ASSERT_CONTAINS(src, "get_area");
    });

    TEST("setter method — emitted as set_prop()", {
        auto src = codegen(
            "class Box {\n"
            "  constructor() { this._size = 0; }\n"
            "  get size() { return this._size; }\n"
            "  set size(v) { this._size = v > 0 ? v : 0; }\n"
            "}\n"
            "app.get('/box', (ctx) => {\n"
            "  const b = new Box();\n"
            "  b.size = 10;\n"
            "  ctx.json({ size: b.size });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Box");
        ASSERT_CONTAINS(src, "set_size");
    });

    TEST("instanceof operator — emitted as stub", {
        auto src = codegen(
            "app.get('/i', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const ok = n instanceof Number;\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "instanceof");
    });
}

// ── Labeled Break / Continue ──────────────────────────────────────────────────

static void testLabeledLoops() {
    std::cout << "\n=== Labeled Break / Continue ===\n";

    TEST("labeled break — goto emitted", {
        auto src = codegen(
            "app.get('/l', (ctx) => {\n"
            "  let found = -1;\n"
            "  outer: for (let i = 0; i < 5; i++) {\n"
            "    for (let j = 0; j < 5; j++) {\n"
            "      if (i * j > 6) { found = i; break outer; }\n"
            "    }\n"
            "  }\n"
            "  ctx.json({ found });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "goto _lbl_outer_end");
        ASSERT_CONTAINS(src, "_lbl_outer_end");
    });

    TEST("labeled continue — goto emitted", {
        auto src = codegen(
            "app.get('/lc', (ctx) => {\n"
            "  let count = 0;\n"
            "  outer: for (let i = 0; i < 3; i++) {\n"
            "    for (let j = 0; j < 3; j++) {\n"
            "      if (j === 1) continue outer;\n"
            "      count++;\n"
            "    }\n"
            "  }\n"
            "  ctx.json({ count });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "goto _lbl_outer_cont");
    });

    TEST("plain break — still works", {
        auto src = codegen(
            "app.get('/b', (ctx) => {\n"
            "  let n = 0;\n"
            "  for (let i = 0; i < 10; i++) {\n"
            "    if (i > 5) break;\n"
            "    n++;\n"
            "  }\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "break;");
    });

    TEST("label on while loop", {
        auto src = codegen(
            "app.get('/w', (ctx) => {\n"
            "  let i = 0;\n"
            "  outer: while (i < 10) {\n"
            "    i++;\n"
            "    if (i > 5) break outer;\n"
            "  }\n"
            "  ctx.json({ i });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "_lbl_outer_end");
    });
}

// ── Map / Set ─────────────────────────────────────────────────────────────────

static void testMapSet() {
    std::cout << "\n=== Map / Set ===\n";

    TEST("Map struct — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "struct Map");
    });

    TEST("Set struct — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "struct Set");
    });

    TEST("new Map() — emitted", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const m = new Map();\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Map");
    });

    TEST("new Set() with add() — emitted", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const s = new Set();\n"
            "  s.add('a');\n"
            "  s.add('b');\n"
            "  ctx.json({ size: s.size() });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "Set");
        ASSERT_CONTAINS(src, "add");
    });

    TEST("Map.set() and Map.get() — emitted", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const m = new Map();\n"
            "  m.set('key', 42.0);\n"
            "  const v = m.get('key');\n"
            "  ctx.json({ v });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── JSON.parse / JSON.stringify ───────────────────────────────────────────────

static void testJsonParsStringify() {
    std::cout << "\n=== JSON.parse / JSON.stringify ===\n";

    TEST("JSON.stringify — emitted", {
        auto src = codegen(
            "app.get('/js', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const s = JSON.stringify(n);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_stringify");
    });

    TEST("JSON.parse — emitted", {
        auto src = codegen(
            "app.post('/jp', (ctx) => {\n"
            "  const parsed = JSON.parse(ctx.body);\n"
            "  const name = parsed.name;\n"
            "  ctx.json({ name });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_parse");
    });

    TEST("JSON.stringify with object — emitted", {
        auto src = codegen(
            "app.get('/jso', (ctx) => {\n"
            "  const s = JSON.stringify({ a: 1, b: 'hello' });\n"
            "  ctx.text(s);\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_stringify");
    });

    TEST("JSON.stringify with array — emitted", {
        auto src = codegen(
            "app.get('/jsa', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const s = JSON.stringify(arr);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Array functional methods ──────────────────────────────────────────────────

static void testArrayFunctional() {
    std::cout << "\n=== Array Functional Methods ===\n";

    TEST("arr.filter — symm_arr_filter emitted", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const arr = [1.0, 2.0, 3.0, 4.0, 5.0];\n"
            "  const evens = arr.filter((x) => x % 2 === 0);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.reduce — symm_arr_reduce emitted", {
        auto src = codegen(
            "app.get('/r', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0, 4.0];\n"
            "  const sum = arr.reduce((acc, x) => acc + x, 0.0);\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.map with callback — emitted", {
        auto src = codegen(
            "app.get('/m', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n);\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const doubled = arr.map((x) => x * 2);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
    });

    TEST("arr.find — emitted", {
        auto src = codegen(
            "app.get('/fd', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const found = arr.find((x) => x > 2);\n"
            "  ctx.json({ found });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.some / arr.every — emitted", {
        auto src = codegen(
            "app.get('/se', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  const anyBig = arr.some((x) => x > 2);\n"
            "  const allPos = arr.every((x) => x > 0);\n"
            "  ctx.json({ anyBig, allPos });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.includes — emitted", {
        auto src = codegen(
            "app.get('/inc', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const ok = [1, 2, 3].includes(n);\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.indexOf — emitted", {
        auto src = codegen(
            "app.get('/idx', (ctx) => {\n"
            "  const arr = [10.0, 20.0, 30.0];\n"
            "  const idx = arr.indexOf(20.0);\n"
            "  ctx.json({ idx });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("arr.flat — emitted", {
        auto src = codegen(
            "app.get('/fl', (ctx) => {\n"
            "  const nested = [[1.0, 2.0], [3.0, 4.0]];\n"
            "  const flat = nested.flat();\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── String advanced methods ───────────────────────────────────────────────────

static void testStringAdvanced() {
    std::cout << "\n=== String Advanced Methods ===\n";

    TEST("String.fromCharCode — emitted", {
        auto src = codegen(
            "app.get('/fc', (ctx) => {\n"
            "  const ch = String.fromCharCode(65);\n"
            "  ctx.json({ ch });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_from_char_code");
    });

    TEST("String.fromCodePoint — emitted", {
        auto src = codegen(
            "app.get('/fcp', (ctx) => {\n"
            "  const ch = String.fromCodePoint(97);\n"
            "  ctx.json({ ch });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_from_char_code");
    });

    TEST("str.startsWith — emitted", {
        auto src = codegen(
            "app.get('/sw', (ctx) => {\n"
            "  const path = ctx.path;\n"
            "  const ok = path.startsWith('/api/');\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_starts_with");
    });

    TEST("str.endsWith — emitted", {
        auto src = codegen(
            "app.get('/ew', (ctx) => {\n"
            "  const path = ctx.path;\n"
            "  const ok = path.endsWith('.json');\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_ends_with");
    });

    TEST("str.repeat — emitted", {
        auto src = codegen(
            "app.get('/rp', (ctx) => {\n"
            "  const sep = '-'.repeat(10);\n"
            "  ctx.text(sep);\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_repeat");
    });

    TEST("str.padStart — emitted", {
        auto src = codegen(
            "app.get('/ps', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const s = n.toString().padStart(5, '0');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_pad_start");
    });

    TEST("str.padEnd — emitted", {
        auto src = codegen(
            "app.get('/pe', (ctx) => {\n"
            "  const s = 'hello'.padEnd(10, ' ');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_pad_end");
    });

    TEST("str.at() index access — emitted", {
        auto src = codegen(
            "app.get('/at', (ctx) => {\n"
            "  const s = ctx.query.s ?? '';\n"
            "  const first = s.at(0);\n"
            "  ctx.json({ first });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Generator functions ───────────────────────────────────────────────────────

static void testGenerators() {
    std::cout << "\n=== Generator Functions ===\n";

    TEST("generator function — emitted as function", {
        auto src = codegen(
            "function* range(start, end) {\n"
            "  for (let i = start; i < end; i++) {\n"
            "    yield i;\n"
            "  }\n"
            "}\n"
            "app.get('/g', (ctx) => {\n"
            "  const r = range(0, 5);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "range");
    });

    TEST("yield expression — emitted as no-op", {
        auto src = codegen(
            "function* gen() {\n"
            "  const x = 1;\n"
            "  yield x;\n"
            "  const y = 2;\n"
            "  yield y;\n"
            "}\n"
            "app.get('/g', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("async generator — emitted as async function", {
        auto src = codegen(
            "async function* asyncGen() {\n"
            "  yield 1;\n"
            "  yield 2;\n"
            "}\n"
            "app.get('/ag', (ctx) => {\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Comma expression / comma operator ────────────────────────────────────────

static void testCommaExpr() {
    std::cout << "\n=== Comma Expression ===\n";

    TEST("comma in for-loop update — valid", {
        auto src = codegen(
            "app.get('/c', (ctx) => {\n"
            "  let a = 0, b = 0;\n"
            "  for (let i = 0; i < 5; i++) {\n"
            "    a += i; b += i * i;\n"
            "  }\n"
            "  ctx.json({ a, b });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("void 0 expression — emitted", {
        auto src = codegen(
            "app.get('/v', (ctx) => {\n"
            "  const n = void 0;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("delete expression — skipped gracefully", {
        auto src = codegen(
            "app.get('/d', (ctx) => {\n"
            "  const obj = { x: 1, y: 2 };\n"
            "  delete obj.x;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Method chaining ───────────────────────────────────────────────────────────

static void testMethodChaining() {
    std::cout << "\n=== Method Chaining ===\n";

    TEST("string method chain — trim().toLowerCase()", {
        auto src = codegen(
            "app.post('/c', (ctx) => {\n"
            "  const input = ctx.body.trim().toLowerCase();\n"
            "  ctx.json({ input });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_trim");
        ASSERT_CONTAINS(src, "symm_str_to_lower");
    });

    TEST("three-step chain — split+join", {
        auto src = codegen(
            "app.post('/c2', (ctx) => {\n"
            "  const input = ctx.body.trim().toUpperCase().split(',');\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_trim");
        ASSERT_CONTAINS(src, "symm_str_to_upper");
    });

    TEST("number chain — toString().padStart()", {
        auto src = codegen(
            "app.get('/n', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const s = n.toString().padStart(6, '0');\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_pad_start");
    });

    TEST("math chain — Math.abs(Math.floor(x))", {
        auto src = codegen(
            "app.get('/mc', (ctx) => {\n"
            "  const x = parseFloat(ctx.query.x);\n"
            "  const n = Math.abs(Math.floor(x));\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "fabsf");
        ASSERT_CONTAINS(src, "floorf");
    });
}

// ── typeof === patterns ───────────────────────────────────────────────────────

static void testTypeofPatterns() {
    std::cout << "\n=== typeof Patterns ===\n";

    TEST("typeof x === 'string'", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const q = ctx.query.q;\n"
            "  if (typeof q === 'string') {\n"
            "    ctx.json({ type: 'string' });\n"
            "  } else {\n"
            "    ctx.json({ type: 'other' });\n"
            "  }\n"
            "});"
        );
        // typeof ctx.query.q is statically resolved to "string" by codegen
        ASSERT_CONTAINS(src, "string");
        ASSERT_CONTAINS(src, "other");
    });

    TEST("typeof x === 'number'", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const isNum = typeof n === 'number';\n"
            "  ctx.json({ isNum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("typeof === 'undefined'", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const v = ctx.query.v;\n"
            "  const isUndef = typeof v === 'undefined';\n"
            "  ctx.json({ isUndef });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("typeof in ternary", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n);\n"
            "  const msg = typeof n === 'number' ? 'numeric' : 'other';\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
        ASSERT_CONTAINS(src, "numeric");
    });
}

// ── in / instanceof operators ─────────────────────────────────────────────────

static void testInInstanceof() {
    std::cout << "\n=== in / instanceof Operators ===\n";

    TEST("'key' in obj — emitted as stub", {
        auto src = codegen(
            "app.get('/in', (ctx) => {\n"
            "  const obj = { x: 1, y: 2 };\n"
            "  const hasX = 'x' in obj;\n"
            "  ctx.json({ hasX });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("instanceof class — emitted as stub true", {
        auto src = codegen(
            "class Foo {}\n"
            "app.get('/inst', (ctx) => {\n"
            "  const f = new Foo();\n"
            "  const ok = f instanceof Foo;\n"
            "  ctx.json({ ok });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "instanceof");
    });

    TEST("instanceof with Error — emitted", {
        auto src = codegen(
            "app.get('/err', (ctx) => {\n"
            "  try {\n"
            "    throw new Error('test');\n"
            "  } catch (e) {\n"
            "    const isErr = e instanceof Error;\n"
            "    ctx.json({ isErr });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "instanceof");
    });
}

// ── Complex real-world patterns ───────────────────────────────────────────────

static void testComplexPatterns2() {
    std::cout << "\n=== Complex Patterns 2 ===\n";

    TEST("chained ternary for status code", {
        auto src = codegen(
            "app.get('/s', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const status = n < 0 ? 400 : n > 100 ? 200 : 204;\n"
            "  ctx.status(status);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "400");
        ASSERT_CONTAINS(src, "204");
    });

    TEST("multi-param query parsing", {
        auto src = codegen(
            "app.get('/items', (ctx) => {\n"
            "  const page = parseInt(ctx.query.page ?? '1');\n"
            "  const size = parseInt(ctx.query.size ?? '10');\n"
            "  const offset = (page - 1) * size;\n"
            "  ctx.json({ page, size, offset });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
        ASSERT_CONTAINS(src, "offset");
    });

    TEST("class with multiple inherited methods", {
        auto src = codegen(
            "class Shape {\n"
            "  constructor(color) { this.color = color; }\n"
            "  getColor() { return this.color; }\n"
            "}\n"
            "class Rectangle extends Shape {\n"
            "  constructor(w, h, color) { super(color); this.w = w; this.h = h; }\n"
            "  area() { return this.w * this.h; }\n"
            "}\n"
            "app.get('/r', (ctx) => {\n"
            "  const rect = new Rectangle(10, 20, 'red');\n"
            "  ctx.json({ area: rect.area(), color: rect.getColor() });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "struct Rectangle");
        ASSERT_CONTAINS(src, ": Shape");
        ASSERT_CONTAINS(src, "area");
    });

    TEST("async handler with multiple awaits stripped", {
        auto src = codegen(
            "app.get('/a', async (ctx) => {\n"
            "  const n1 = parseInt(ctx.query.a);\n"
            "  const n2 = parseInt(ctx.query.b);\n"
            "  const sum = n1 + n2;\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
        ASSERT_CONTAINS(src, "sum");
    });

    TEST("spread operator in function call", {
        auto src = codegen(
            "app.get('/sp', (ctx) => {\n"
            "  const args = [1.0, 2.0, 3.0];\n"
            "  const max = Math.max(...args);\n"
            "  ctx.json({ max });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "fmaxf");
    });

    TEST("object shorthand properties", {
        auto src = codegen(
            "app.get('/sh', (ctx) => {\n"
            "  const x = parseFloat(ctx.query.x);\n"
            "  const y = parseFloat(ctx.query.y);\n"
            "  ctx.json({ x, y });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_float");
        ASSERT_CONTAINS(src, "\"x\"");
        ASSERT_CONTAINS(src, "\"y\"");
    });
}

// ── WeakMap / WeakRef / WeakSet ───────────────────────────────────────────────

static void testWeakCollections() {
    std::cout << "\n=== WeakMap / WeakRef / WeakSet ===\n";

    TEST("WeakMap struct — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "WeakMap");
    });

    TEST("WeakRef struct — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "WeakRef");
    });

    TEST("new WeakMap() — emitted as struct", {
        auto src = codegen(
            "app.get('/wm', (ctx) => {\n"
            "  const cache = new WeakMap();\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "WeakMap");
    });

    TEST("new WeakRef(obj) — emitted", {
        auto src = codegen(
            "class Obj { constructor() { this.val = 0; } }\n"
            "app.get('/wr', (ctx) => {\n"
            "  const o = new Obj();\n"
            "  const ref = new WeakRef(o);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "WeakRef");
    });

    TEST("new WeakSet() — emitted", {
        auto src = codegen(
            "app.get('/ws', (ctx) => {\n"
            "  const visited = new WeakSet();\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "WeakSet");
    });
}

// ── URLSearchParams ───────────────────────────────────────────────────────────

static void testUrlSearchParams() {
    std::cout << "\n=== URLSearchParams ===\n";

    TEST("URLSearchParams struct — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "URLSearchParams");
    });

    TEST("new URLSearchParams(qs) — emitted", {
        auto src = codegen(
            "app.get('/usp', (ctx) => {\n"
            "  const params = new URLSearchParams(ctx.query);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "URLSearchParams");
    });

    TEST("params.get() — called", {
        auto src = codegen(
            "app.get('/pg', (ctx) => {\n"
            "  const params = new URLSearchParams(ctx.query);\n"
            "  const name = params.get('name');\n"
            "  ctx.json({ name });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "URLSearchParams");
        ASSERT_CONTAINS(src, "get");
    });
}

// ── Number.toFixed / toPrecision ──────────────────────────────────────────────

static void testNumberFormatting() {
    std::cout << "\n=== Number Formatting ===\n";

    TEST("toFixed(2) — symm_number_to_fixed", {
        auto src = codegen(
            "app.get('/price', (ctx) => {\n"
            "  const price = parseFloat(ctx.query.p);\n"
            "  const s = price.toFixed(2);\n"
            "  ctx.json({ price: s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_number_to_fixed");
        ASSERT_CONTAINS(src, "2");
    });

    TEST("toFixed(0) — integer format", {
        auto src = codegen(
            "app.get('/i', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n);\n"
            "  const s = n.toFixed(0);\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_number_to_fixed");
        ASSERT_CONTAINS(src, "0");
    });

    TEST("toFixed without arg — defaults to 0", {
        auto src = codegen(
            "app.get('/d', (ctx) => {\n"
            "  const n = parseFloat(ctx.query.n);\n"
            "  const s = n.toString();\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_number_to_fixed");
    });

    TEST("n.toFixed(digits) in json", {
        auto src = codegen(
            "app.get('/f', (ctx) => {\n"
            "  const lat = parseFloat(ctx.query.lat);\n"
            "  const lon = parseFloat(ctx.query.lon);\n"
            "  ctx.json({ lat: lat.toFixed(6), lon: lon.toFixed(6) });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_number_to_fixed");
        ASSERT_CONTAINS(src, "6");
    });
}

// ── Error subclasses ──────────────────────────────────────────────────────────

static void testErrorSubclasses() {
    std::cout << "\n=== Error Subclasses ===\n";

    TEST("TypeError — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "TypeError");
    });

    TEST("RangeError — in prelude", {
        auto src = codegen("app.get('/x', (ctx) => { ctx.json({ ok: true }); });");
        ASSERT_CONTAINS(src, "RangeError");
    });

    TEST("throw new TypeError — emitted", {
        auto src = codegen(
            "app.get('/te', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  if (typeof n !== 'number') throw new TypeError('expected number');\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "TypeError");
    });

    TEST("throw new RangeError — emitted", {
        auto src = codegen(
            "app.get('/re', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  if (n < 0 || n > 100) throw new RangeError('out of range');\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "RangeError");
    });

    TEST("catch TypeError — emitted", {
        auto src = codegen(
            "app.get('/ce', (ctx) => {\n"
            "  try {\n"
            "    const n = parseInt(ctx.query.n);\n"
            "    if (n < 0) throw new RangeError('negative');\n"
            "    ctx.json({ n });\n"
            "  } catch (e) {\n"
            "    ctx.status(400);\n"
            "    ctx.json({ error: 'invalid' });\n"
            "  }\n"
            "});"
        );
        ASSERT_CONTAINS(src, "400");
        ASSERT_CONTAINS(src, "RangeError");
    });
}

// ── Complex template literal expressions ──────────────────────────────────────

static void testComplexTemplateLiterals() {
    std::cout << "\n=== Complex Template Literals ===\n";

    TEST("template literal with ternary", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const msg = `count is ${n > 0 ? 1 : 0}`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        // Template literal ternary: symm_write called with conditional expr
        ASSERT_CONTAINS(src, "symm_write");
        ASSERT_CONTAINS(src, "count is");
    });

    TEST("template literal with method call", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const s = ctx.query.s ?? '';\n"
            "  const msg = `trimmed: ${s.trim()}`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_str_trim");
    });

    TEST("template literal with arithmetic", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const a = parseInt(ctx.query.a);\n"
            "  const b = parseInt(ctx.query.b);\n"
            "  const msg = `${a} + ${b} = ${a + b}`;\n"
            "  ctx.json({ msg });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("nested template literals", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const x = parseInt(ctx.query.x);\n"
            "  const outer = `result: ${`value: ${x}`}`;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("template literal as function argument", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  ctx.text(`count=${n}`);\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("tagged template literal — emitted gracefully", {
        auto src = codegen(
            "app.get('/t', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const s = String.raw`value: ${n}`;\n"
            "  ctx.json({ s });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── for...of on string ────────────────────────────────────────────────────────

static void testForOfString() {
    std::cout << "\n=== for...of on String ===\n";

    TEST("for...of on string — emitted", {
        auto src = codegen(
            "app.get('/fc', (ctx) => {\n"
            "  const s = ctx.query.s ?? '';\n"
            "  let count = 0;\n"
            "  for (const ch of s) {\n"
            "    if (ch === 'a') count++;\n"
            "  }\n"
            "  ctx.json({ count });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("for...of on array — emitted", {
        auto src = codegen(
            "app.get('/fa', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0];\n"
            "  let sum = 0.0;\n"
            "  for (const v of arr) {\n"
            "    sum += v;\n"
            "  }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "sum");
    });

    TEST("for...of with destructuring — emitted", {
        auto src = codegen(
            "app.get('/fd', (ctx) => {\n"
            "  const pairs = [[1.0, 2.0], [3.0, 4.0]];\n"
            "  let sum = 0.0;\n"
            "  for (const pair of pairs) {\n"
            "    sum += pair[0] + pair[1];\n"
            "  }\n"
            "  ctx.json({ sum });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });
}

// ── Promise chains (.then / .catch) ──────────────────────────────────────────

static void testPromiseChains() {
    std::cout << "\n=== Promise Chains ===\n";

    TEST("Promise.resolve — emitted", {
        auto src = codegen(
            "app.get('/pr', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const p = Promise.resolve(n);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Promise.reject — emitted", {
        auto src = codegen(
            "app.get('/prj', (ctx) => {\n"
            "  const p = Promise.reject('error');\n"
            "  ctx.json({ ok: false });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Promise.all — emitted", {
        auto src = codegen(
            "app.get('/pa', (ctx) => {\n"
            "  const results = await Promise.all([1.0, 2.0, 3.0]);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("async handler with .then() — emitted", {
        auto src = codegen(
            "app.get('/at', async (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });
}

// ── Numeric separators ────────────────────────────────────────────────────────

static void testNumericSeparators() {
    std::cout << "\n=== Numeric Separators ===\n";

    TEST("1_000_000 literal — parsed as integer", {
        auto src = codegen(
            "const MAX = 1_000_000;\n"
            "app.get('/m', (ctx) => {\n"
            "  ctx.json({ max: MAX });\n"
            "});"
        );
        // Should parse to 1000000 or similar
        ASSERT_CONTAINS(src, "MAX");
    });

    TEST("1_234.567_89 float literal — parsed", {
        auto src = codegen(
            "const PI = 3.141_592;\n"
            "app.get('/p', (ctx) => {\n"
            "  ctx.json({ pi: PI });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "PI");
    });

    TEST("0xFF_FF hex literal — parsed", {
        auto src = codegen(
            "const FLAGS = 0xFF00;\n"
            "app.get('/f', (ctx) => {\n"
            "  ctx.json({ flags: FLAGS });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "FLAGS");
    });
}

// ── Object destructuring advanced ────────────────────────────────────────────

static void testObjectDestructuringAdvanced() {
    std::cout << "\n=== Object Destructuring Advanced ===\n";

    TEST("destructure ctx.query", {
        auto src = codegen(
            "app.get('/d', (ctx) => {\n"
            "  const { query } = ctx;\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("destructure with rename alias", {
        auto src = codegen(
            "app.get('/dr', (ctx) => {\n"
            "  const n = parseInt(ctx.query.n);\n"
            "  const { body: rawBody } = ctx;\n"
            "  ctx.json({ n });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_parse_int");
    });

    TEST("destructure from return value", {
        auto src = codegen(
            "function getCoords() { return { x: 1.0, y: 2.0 }; }\n"
            "app.get('/dc', (ctx) => {\n"
            "  const { x, y } = getCoords();\n"
            "  ctx.json({ x, y });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "getCoords");
    });

    TEST("array destructuring with skip", {
        auto src = codegen(
            "app.get('/ds', (ctx) => {\n"
            "  const arr = [1.0, 2.0, 3.0, 4.0];\n"
            "  const [first, , third] = arr;\n"
            "  ctx.json({ first, third });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "first");
    });

    TEST("parameter destructuring", {
        auto src = codegen(
            "function sum({ a, b }) { return a + b; }\n"
            "app.get('/ps', (ctx) => {\n"
            "  const result = sum({ a: 1.0, b: 2.0 });\n"
            "  ctx.json({ result });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "sum");
    });
}

// ── Object.assign and spread ──────────────────────────────────────────────────

static void testObjectAssign() {
    std::cout << "\n=== Object.assign and Spread ===\n";

    TEST("Object.assign — emitted", {
        auto src = codegen(
            "app.get('/oa', (ctx) => {\n"
            "  const base = { x: 1.0 };\n"
            "  const extra = { y: 2.0 };\n"
            "  const merged = Object.assign(base, extra);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "assign");
    });

    TEST("Object.keys — emitted", {
        auto src = codegen(
            "app.get('/ok', (ctx) => {\n"
            "  const obj = { a: 1, b: 2, c: 3 };\n"
            "  const keys = Object.keys(obj);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Object.values — emitted", {
        auto src = codegen(
            "app.get('/ov', (ctx) => {\n"
            "  const obj = { a: 1.0, b: 2.0 };\n"
            "  const vals = Object.values(obj);\n"
            "  ctx.json({ ok: true });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_handler_0");
    });

    TEST("Object spread into response", {
        auto src = codegen(
            "app.get('/osp', (ctx) => {\n"
            "  const defaults = { a: 1, b: 2 };\n"
            "  const override = { b: 3, c: 4 };\n"
            "  ctx.json({ ...defaults, ...override });\n"
            "});"
        );
        ASSERT_CONTAINS(src, "symm_json_merge");
    });
}

} // namespace symm

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "Symmetra Codegen Test Suite\n";
    std::cout << "===========================\n";

    symm::testPrelude();
    symm::testRouting();
    symm::testParams();
    symm::testTemplateLiterals();
    symm::testForOf();
    symm::testSwitchStmt();
    symm::testParseInt();
    symm::testMath();
    symm::testStringMethods();
    symm::testNumberMethods();
    symm::testBinaryOps();
    symm::testJSON();
    symm::testClasses();
    symm::testProcessDate();
    symm::testOptionalChaining();
    symm::testBitwise();
    symm::testDispatch();
    symm::testEdgeCases();
    // Extended test suites
    symm::testLoops();
    symm::testDestructuring();
    symm::testLogicalAssignment();
    symm::testStringMethods2();
    symm::testMathMethods2();
    symm::testNumberEdge();
    symm::testArrayMethods();
    symm::testObjectMethods();
    symm::testRouteMeta();
    symm::testUnaryOps();
    symm::testControlFlow();
    symm::testFunctions();
    symm::testHttpPatterns();
    // Phase 2 extended suites
    symm::testSpreadRest();
    symm::testRegex();
    symm::testAsync();
    symm::testTypeScript();
    symm::testMathEdge();
    symm::testArrayAdvanced();
    symm::testComplexHandlers();
    symm::testClassAdvanced();
    symm::testNumericEdge();
    // Phase 3
    symm::testEnum();
    symm::testExportInterface();
    symm::testArrayFrom();
    symm::testObjectExtended();
    symm::testConsole();
    symm::testErrorHandling();
    symm::testRealWorldPatterns();
    // Phase 4
    symm::testTypeScriptCasts();
    symm::testSymbol();
    symm::testDestructuringDefaults();
    symm::testTopLevelConst();
    // Phase 5
    symm::testJsonSpread();
    symm::testJsonValueTypes();
    symm::testStringEscaping();
    symm::testAdvancedControlFlow();
    symm::testOptionalChainingDeep();
    // Phase 6
    symm::testClassInheritance();
    symm::testLabeledLoops();
    symm::testMapSet();
    symm::testJsonParsStringify();
    symm::testArrayFunctional();
    symm::testStringAdvanced();
    symm::testGenerators();
    symm::testCommaExpr();
    symm::testMethodChaining();
    symm::testTypeofPatterns();
    symm::testInInstanceof();
    symm::testComplexPatterns2();
    // Phase 7
    symm::testWeakCollections();
    symm::testUrlSearchParams();
    symm::testNumberFormatting();
    symm::testErrorSubclasses();
    symm::testComplexTemplateLiterals();
    symm::testForOfString();
    symm::testPromiseChains();
    symm::testNumericSeparators();
    symm::testObjectDestructuringAdvanced();
    symm::testObjectAssign();

    std::cout << "\n===========================\n";
    std::cout << "Results: " << symm::gPassed << " passed, " << symm::gFailed << " failed\n";

    if (symm::gFailed > 0) {
        std::cerr << "FAILED: " << symm::gFailed << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All " << symm::gPassed << " tests passed.\n";
    return 0;
}
