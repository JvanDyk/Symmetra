# Symmetra — Complete JS/TS Implementation Plan

**Goal:** Every JavaScript and TypeScript language feature that can run on GPU hardware is implemented correctly, completely, and with tests.  
**Reference:** [`public/docs/js-ts-coverage.html`](public/docs/js-ts-coverage.html) — live filter/search matrix of all ~280 features.  
**Architecture:** [`public/docs/engine-architecture.html`](public/docs/engine-architecture.html)

Status legend: ✅ Done · ⚠️ Partial · 🔲 Not started · ⛔ N/A (excluded by design)

---

## How to read this plan

Each phase is self-contained and produces working, tested code. Phases are ordered by **impact × difficulty** — earlier phases unlock the most real-world TypeScript code. Every task lists:
- **Where:** which file(s) to touch
- **How:** the concrete GPU/C++ implementation strategy
- **Test:** the integration or unit test to add

---

## Phase A — Critical Correctness  
*These block virtually all real npm packages. Fix first.*  
**Estimated total: ~6 days**

---

### A-1 · `try/catch/finally` + `throw`  ⏱ 3 days

**Problem:** GPU kernels cannot throw C++ exceptions. Any real TypeScript server uses try/catch.

**Implementation — Codegen.cpp:**

1. Extend `SymmCtx` with two fields (already have `io_stage`):
   ```cpp
   bool        _threw    = false;
   char        _err_msg[256] = {};
   int         _err_code = 0;   // 0 = no error
   ```

2. Emit `throw expr` as:
   ```cpp
   symm_strcpy(ctx->_err_msg, expr.message, 256);
   ctx->_threw = true;
   ctx->_err_code = 500;
   goto __catch_N;   // jump to nearest enclosing catch
   ```

3. Emit `try { body } catch (e) { handler } finally { fin }` as:
   ```cpp
   {
     bool _threw_N = false;
     do {
       /* try body — any throw sets ctx->_threw and breaks */
       body;
     } while(0);
     __catch_N:;
     if (ctx->_threw) {
       ctx->_threw = false;          // reset for catch handler
       auto e = Error{ctx->_err_msg};
       handler;
     }
     fin;   // always
   }
   ```

4. `GpuServer::sendBatchResponses` already reads `ctx->resp_status`; if kernel left `_threw=true` after all catch, emit 500.

**Files:** `engine/src/compiler/Codegen.cpp` (emitTryStmt, emitThrowStmt), `engine/src/parser/SymmParser.h` (TryStmt node already likely parsed), `engine/src/runtime/BatchContext.h` (SymmCtx fields)

**Test:** `/try-catch` route: `try { throw new Error('oops') } catch(e) { return e.message }` → `"oops"`

---

### A-2 · `Map` and `Set` — fix key comparison  ⏱ ½ day

**Problem:** Current `Map._e[i].k == k` compares pointers, not string content. `map.set('foo', 1); map.get('foo')` returns 0 because the two `'foo'` literals may be at different addresses.

**Implementation — Codegen prelude:**

Replace `Map` and `Set` structs:
```cpp
struct Map {
    __MapEntry _e[64]; int _n = 0;
    __device__ void set(const char* k, float v) {
        for (int i=0;i<_n;i++) if (symm_str_eq(_e[i].k,k)){_e[i].v=v;return;}
        if (_n<64) { _e[_n].k=k; _e[_n].v=v; _n++; }
    }
    __device__ float get(const char* k) const {
        for (int i=0;i<_n;i++) if (symm_str_eq(_e[i].k,k)) return _e[i].v;
        return 0.f;
    }
    __device__ bool has(const char* k) const {
        for (int i=0;i<_n;i++) if (symm_str_eq(_e[i].k,k)) return true;
        return false;
    }
    __device__ bool _delete(const char* k) {
        for (int i=0;i<_n;i++) if (symm_str_eq(_e[i].k,k)){
            _e[i]=_e[--_n]; return true;
        }
        return false;
    }
    __device__ void clear() { _n=0; }
    __device__ int size() const { return _n; }
};
```

Also add a typed `MapN<K,V>` template for numeric keys and a `MapStr<V>` alias that stores a 64-byte key copy (for keys not guaranteed to be constant-pool pointers):
```cpp
struct __MapStrEntry { char k[64]; char v[256]; };
struct MapStr {
    __MapStrEntry _e[32]; int _n=0;
    __device__ void set(const char* k, const char* v) {
        for(int i=0;i<_n;i++) if(symm_str_eq(_e[i].k,k)){
            symm_strcpy(_e[i].v,v,256);return;
        }
        if(_n<32){symm_strcpy(_e[_n].k,k,64);symm_strcpy(_e[_n].v,v,256);_n++;}
    }
    // … get/has/delete
};
```

Codegen chooses `Map` (const char* pointer keys, sufficient for literal keys) vs `MapStr` (copied string keys, for computed keys) based on whether keys are string literals at compile time.

**Files:** `engine/src/compiler/Codegen.cpp` (prelude)

**Test:** `/map-string-keys` → `{ const m = new Map(); m.set('hello', 42); return m.get('hello') }` → `42`

---

### A-3 · `JSON.parse` — GPU recursive descent  ⏱ 2 days

**Problem:** Request bodies almost universally arrive as JSON. Without `JSON.parse`, handlers must use `symm_body_get_str/int` helpers which are limited.

**Implementation — Codegen prelude:**

Add a GPU-side JSON parser (~200 lines) that produces a `GpuObject`:

```cpp
struct JsonVal {
    enum Kind { Null, Bool, Int, Float, Str, Arr, Obj } kind;
    bool        b;
    long long   i;
    float       f;
    char        s[256];
    struct JsonVal* arr[16]; int arr_n;
    struct JsonVal* obj_v[16]; char obj_k[16][64]; int obj_n;
    __device__ const char* str()  const { return s; }
    __device__ float       num()  const { return kind==Int?(float)i:f; }
    __device__ bool        bool_()const { return b; }
    __device__ JsonVal*    get(const char* key) const {
        for(int j=0;j<obj_n;j++) if(symm_str_eq(obj_k[j],key)) return obj_v[j];
        return nullptr;
    }
    __device__ JsonVal*    at(int idx) const {
        return (idx>=0&&idx<arr_n)?arr[idx]:nullptr;
    }
};

// Forward declare pool allocator (thread-local stack pool)
__device__ JsonVal* _json_alloc(JsonVal pool[], int& pi) {
    return (pi < 64) ? &pool[pi++] : nullptr;
}

__device__ const char* _json_skip_ws(const char* p) {
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    return p;
}

__device__ JsonVal* _json_parse_val(const char*& p, JsonVal pool[], int& pi);

// … full recursive parser for string/number/bool/null/array/object

__device__ JsonVal* symm_json_parse(const char* src, JsonVal pool[64], int& pi) {
    pi = 0;
    const char* p = src;
    return _json_parse_val(p, pool, pi);
}
```

Codegen emits `JSON.parse(expr)` as:
```cpp
JsonVal _jpool_N[64]; int _jpi_N = 0;
JsonVal* _jparsed_N = symm_json_parse(expr, _jpool_N, _jpi_N);
```

The pool lives on the thread's GPU stack (64 × ~400 bytes = ~25 KB — fits within the 64 KB per-thread limit on A100).

**Files:** `engine/src/compiler/Codegen.cpp` (prelude + emitCall for JSON.parse)

**Test:** `/json-parse` route: parses `{"name":"Alice","age":30}` from body, returns `name` field.

---

### A-4 · BigInt `n` literal suffix  ⏱ ½ day

**Problem:** `100n + 200n` fails to parse because `100` is tokenised as a number and `n` becomes an unexpected identifier.

**Implementation — SymmParser.cpp:**

In the number literal tokenisation path, after reading digits, peek at next char:
```cpp
if (isdigit(c) || c == '.' || ...) {
    // read number token
    while (isdigit(peek())) advance();
    // BigInt suffix
    if (peek() == 'n' && !isalpha(peekAt(1))) {
        advance(); // consume 'n'
        tok.kind = TK_BIGINT;
        tok.text += "LL";   // C++ long long suffix
    }
}
```

Codegen emits `TK_BIGINT` as `(long long)NNN` or `NNNll`.

**Files:** `engine/src/parser/SymmParser.cpp`

**Test:** `/bigint-suffix` → `100n + 200n === 300n` → `true`

---

## Phase B — Common Language Features  
*Used in the majority of modern TypeScript codebases.*  
**Estimated total: ~5 days**

---

### B-1 · Error subclasses  ⏱ ½ day

Add to prelude in `Codegen.cpp`:
```cpp
struct TypeError      : Error { __device__ TypeError(const char* m="") : Error(m) {} };
struct RangeError     : Error { __device__ RangeError(const char* m="") : Error(m) {} };
struct SyntaxError    : Error { __device__ SyntaxError(const char* m="") : Error(m) {} };
struct ReferenceError : Error { __device__ ReferenceError(const char* m="") : Error(m) {} };
struct URIError       : Error { __device__ URIError(const char* m="") : Error(m) {} };
```
Also add `cause` field to `Error`:
```cpp
struct Error {
    const char* message;
    const char* cause_msg = nullptr;
    __device__ Error(const char* m="", const char* c=nullptr)
        : message(m), cause_msg(c) {}
};
```

**Test:** `new TypeError('bad type')` usable in catch blocks.

---

### B-2 · Class getters and setters  ⏱ 1.5 days

**Parser:** Add `isGetter` and `isSetter` flags to `MethodDef` AST node. When parsing class body, detect `get` or `set` keyword before method name.

**Codegen — emitClass:**
For each getter/setter pair, generate backing field + accessor methods:
```cpp
// For: get foo() { return this._foo; }
// Emit:
float _foo_backing = 0.f;
__device__ float get_foo() const { return _foo_backing; }
// For: set foo(v) { this._foo = v; }
__device__ void set_foo(float v) { _foo_backing = v; }
```

Member accesses to `obj.foo` where `foo` is a getter → emit `obj.get_foo()`.  
Assignments `obj.foo = v` where `foo` is a setter → emit `obj.set_foo(v)`.

This requires a codegen symbol table tracking which identifiers are getter/setter names for which class types.

**Files:** `engine/src/parser/SymmParser.h` (MethodDef), `engine/src/parser/SymmParser.cpp`, `engine/src/compiler/Codegen.cpp` (emitClass, emitMemberExpr, emitAssign)

**Test:** `/getter-setter` → class `Circle { get area() { return Math.PI * this.r**2 } }` → correct area

---

### B-3 · Private class fields `#field`  ⏱ ½ day

**Parser:** When lexer encounters `#`, read the following identifier and emit a single token `TK_PRIVATE_FIELD` with text `_priv_name`.

**Codegen:** All `#name` references compile to `_priv_name` struct field access. No true encapsulation needed on GPU.

**Files:** `engine/src/parser/SymmParser.cpp` (lexer), `engine/src/compiler/Codegen.cpp` (emitMemberExpr)

---

### B-4 · Logical assignment operators  ⏱ ¼ day

**Parser:** Add 3 token types: `TK_AND_ASSIGN` (&&=), `TK_OR_ASSIGN` (||=), `TK_NULLISH_ASSIGN` (??=).

**Codegen — emitAssign:**
```
a &&= b   →   if(a) a = b
a ||= b   →   if(!a) a = b
a ??= b   →   if(a == nullptr) a = b
```

**Files:** `engine/src/parser/SymmParser.cpp`, `engine/src/compiler/Codegen.cpp`

---

### B-5 · Regular TypeScript enums  ⏱ 1 day

**Parser:** `parseEnum()` already partially exists (esbuild may transform enums). Handle the case where esbuild emits the IIFE enum pattern:
```js
var Color; (function(Color){ Color["Red"]=0; Color["Green"]=1; })(Color||={});
```
Codegen detects this pattern and emits:
```cpp
namespace Color { const int Red=0; const int Green=1; }
```

For string enums:
```cpp
namespace Dir { const char* Up="UP"; const char* Down="DOWN"; }
```

Also teach `typeof` and switch/case to work with enum values.

**Files:** `engine/src/compiler/Codegen.cpp` (detect IIFE enum pattern in emitExprStmt)

**Test:** `/enum-numeric` and `/enum-string` routes.

---

### B-6 · Static class fields  ⏱ ½ day

**Parser:** `isStatic` flag on property nodes (likely already present).

**Codegen:** Static fields with initializers emit as `__device__` file-scope variables with a class-prefix name:
```cpp
__device__ int Foo_count = 0;  // for: class Foo { static count = 0; }
```
References to `Foo.count` or `this.constructor.count` → `Foo_count`.

**Files:** `engine/src/compiler/Codegen.cpp` (emitClass)

---

### B-7 · Labelled break/continue  ⏱ ½ day

**Parser:** Add `LabelledStmt` AST node. Track label → loop association in a symbol stack.

**Codegen:** 
```
outer: for(...) { inner: for(...) { break outer; } }
```
→
```cpp
for(...) { /* outer */
    for(...) { /* inner */
        goto __break_outer;
    }
}
__break_outer:;
```

**Files:** `engine/src/parser/SymmParser.h`, `engine/src/parser/SymmParser.cpp`, `engine/src/compiler/Codegen.cpp`

---

## Phase C — Standard Library Completion  
*Fills the gaps in Array, String, Number, Object APIs.*  
**Estimated total: ~5 days**

---

### C-1 · `Array.at()` / `String.prototype.at()`  ⏱ ¼ day
```cpp
__device__ T symm_arr_at(GpuArray<T>& arr, int i) {
    int idx = i < 0 ? arr._n + i : i;
    return (idx >= 0 && idx < arr._n) ? arr._d[idx] : T{};
}
__device__ _SBuf symm_str_at(const char* s, int i) {
    int len = symm_strlen(s);
    int idx = i < 0 ? len + i : i;
    _SBuf out; out._b[0]='\0';
    if(idx>=0&&idx<len){out._b[0]=s[idx];out._b[1]='\0';}
    return out;
}
```

---

### C-2 · `Array.flat()` / `flatMap()`  ⏱ 1 day

For static depth=1, codegen unrolls:
```cpp
// arr.flat() where arr is GpuArray<GpuArray<T>>
GpuArray<T> _flat; 
for(int i=0;i<arr._n;i++) for(int j=0;j<arr._d[i]._n;j++) _flat.push(arr._d[i]._d[j]);
```
`flatMap(f)` → `map(f).flat()`.

---

### C-3 · `Array.findLast()` / `findLastIndex()`  ⏱ ¼ day
Reverse scan in codegen emitter.

---

### C-4 · `Array.fill()` / `copyWithin()`  ⏱ ¼ day
```cpp
__device__ void symm_arr_fill(T* data, int n, T val, int start, int end) {
    for(int i=start;i<end&&i<n;i++) data[i]=val;
}
```

---

### C-5 · `Array.toSorted()` / `toReversed()` / `toSpliced()` / `with()`  ⏱ ½ day
Non-mutating ES2023 methods — emit a copy then call the mutating version.

---

### C-6 · `Number.prototype.toFixed/toPrecision/toExponential`  ⏱ ½ day
Add to `_SBuf symm_num_to_fixed(float v, int digits)`:
```cpp
__device__ _SBuf symm_num_to_fixed(float v, int d) {
    _SBuf out;
    // integer part
    long long iv = (long long)v;
    float frac = fabsf(v - (float)iv);
    int off = symm_write_int(out._b, iv);
    if(d>0){ out._b[off++]='.'; for(int i=0;i<d;i++){frac*=10;int dig=(int)frac;out._b[off++]='0'+dig;frac-=dig;} }
    out._b[off]='\0'; return out;
}
```
Wire in `emitCall` for `n.toFixed(d)`.

---

### C-7 · `String.fromCharCode` / `codePointAt` / `search` / `match`  ⏱ ½ day

```cpp
__device__ _SBuf symm_str_from_charcode(int c) {
    _SBuf out; out._b[0]=(char)c; out._b[1]='\0'; return out;
}
__device__ int symm_str_search(const char* s, const char* pattern) {
    RegExp re(pattern, ""); 
    MatchResult m = re.exec(s);
    return m.matched ? m.start : -1;
}
```

---

### C-8 · `Math.random()` via CURAND  ⏱ 1 day

Add `curand_state` array to `BatchContext` (one state per slot, 48 bytes each × 256 = 12 KB).

In `symm_dispatch_batch`, before calling the handler:
```cuda
curandState rng = d_rng_states[tid];
ctx.rng = &rng;
// ... call handler ...
d_rng_states[tid] = rng;
```

In the prelude, `Math.random()` → `curand_uniform(ctx->rng)`.

Seed the states once at startup using `curand_init(seed, tid, 0, &state)`.

**Files:** `engine/src/runtime/BatchContext.h` (add d_rngBuf), `engine/src/compiler/Codegen.cpp` (prelude + Math.random dispatch), `engine/src/kernels/genesis.cu` (init kernel)

---

### C-9 · `Object.is()` / `structuredClone()`  ⏱ ½ day

`Object.is(a, b)`: NaN-aware and -0-aware equality.
`structuredClone(obj)`: emit a field-by-field copy; detect cycles would be too expensive — document the limitation.

---

### C-10 · `Map.forEach/keys/values/entries` iteration  ⏱ ½ day

Emit `for(int __i=0;__i<map._n;__i++)` body with destructured `[k,v]` inside.

---

### C-11 · Numeric separators `1_000_000`  ⏱ ¼ day
Lexer: strip underscores inside numeric tokens.

---

### C-12 · RegExp flags `i` (case-insensitive), `s` (dotAll), `{n,m}` quantifiers  ⏱ 1 day

**i flag:** In NFA character comparison, `tolower(a) == tolower(b)`.  
**s flag:** `.` matches `\n`.  
**{n,m}:** Expand `a{3,5}` → `aaa(?:a(?:a)?)?` in the NFA builder.

---

## Phase D — Advanced Language Features  
*Powers complex patterns: async generators, Symbols, module-like namespacing.*  
**Estimated total: ~6 days**

---

### D-1 · `console.log()` — VRAM debug ring buffer  ⏱ 1 day

Add to `SymmCtx`:
```cpp
char debug_log[4096];  // per-slot debug ring buffer
int  debug_len = 0;
```

In prelude, `console.log(args...)`:
```cpp
struct _Console {
    SymmCtx* _ctx;
    template<typename... Args>
    __device__ void log(Args... args) {
        // serialize args into ctx->debug_log using symm_write
    }
} console{&ctx};
```

After kernel, `GpuServer::batchLaunch` reads `ctx->debug_log` and writes to stderr with slot prefix: `[batch:slot:3] Hello world`.

**Files:** `engine/src/compiler/Codegen.cpp` (prelude, emitCall for console.*), `engine/src/runtime/BatchContext.h` (add debug buffer field), `engine/src/runtime/GpuServer.cpp` (download + print debug log)

---

### D-2 · Named RegExp capture groups  ⏱ 2 days

At NVRTC JIT time, Codegen scans all RegExp literals for `(?<name>...)` syntax. It builds a compile-time `name → group_index` map and injects it into the `RegExp` struct:
```cpp
RegExp re("/(?<year>\\d{4})-(?<month>\\d{2})/", "");
// Codegen injects:
re._group_names[0] = "year";   re._group_idx[0] = 1;
re._group_names[1] = "month";  re._group_idx[1] = 2;
re._n_names = 2;
```

`exec()` returns a `MatchResult` with `.groups.year` accessible via `matchResult.group("year")`.

---

### D-3 · `yield*` delegation  ⏱ 1 day

When a generator body contains `yield* otherGen()`, Codegen inlines the inner generator's switch cases into the outer generator's state machine, renumbering states.

---

### D-4 · Tagged template literals  ⏱ 1 day

```js
sql`SELECT * FROM ${table} WHERE id = ${id}`
```

Codegen emits:
```cpp
const char* _parts[] = {"SELECT * FROM ", " WHERE id = ", ""};
// call the tag function with (parts, table, id)
auto _result = sql(_parts, 2, table, id);
```

The tag function must be declared by the user as a `__device__` function accepting `(const char** strings, int n, ...)`.

---

### D-5 · Async generator full CPS  ⏱ 2 days

Extend `GpuAsyncIterState` to support `await` expressions inside the generator body. Each await becomes a new `io_stage` in the split-kernel pattern. The state machine step function checks `ctx->io_stage` and jumps to the correct continuation point.

---

## Phase E — Networking & Real-Time I/O  
*The Tier-3 split-kernel infrastructure for fetch, WebSocket, streaming.*  
**Estimated total: ~12 days**

---

### E-1 · `fetch()` via `@symmetra/net` — Tier-3 split-kernel CPS  ⏱ 5 days

**Architecture (already designed, now implement):**

1. **VRAM I/O queue:** Each kernel slot has a `SymmIoRequest` in VRAM:
   ```cpp
   struct SymmIoRequest {
       int   type;         // 0=none, 1=http_get, 2=http_post, 3=db_query
       char  url[512];
       char  method[8];
       char  body[4096];
       char  headers[1024];
       int   stage;        // which io_stage to resume at
   };
   ```

2. **Kernel A** writes to `d_io_requests[tid]`, sets `ctx->io_stage = N`, returns early.

3. **CPU I/O proxy thread pool** (`engine/src/runtime/IoProxy.cpp`, new file): polls VRAM queue, executes HTTP request using libcurl or platform HTTP client, writes response to `d_io_responses[tid]`, flags `d_io_ready[tid]`.

4. **Kernel B (continuation)** launched on same stream: reads `ctx->io_stage`, jumps to stage N in the handler's switch, reads response from `d_io_responses[tid]`.

5. **`@symmetra/net` JS API:**
   ```typescript
   // @symmetra/net
   export declare function fetch(url: string, options?: RequestInit): Promise<Response>;
   ```
   Codegen recognises `@symmetra/net` imports and emits the split-kernel CPS pattern.

**Files:** New `engine/src/runtime/IoProxy.cpp/.h`, `engine/src/runtime/BatchContext.h` (add d_ioRequestBuf, d_ioResponseBuf), `engine/src/compiler/Codegen.cpp` (emitAwaitExpr for fetch), `engine/src/runtime/GpuServer.cpp` (integrate IoProxy)

---

### E-2 · `@symmetra/db` — database split-kernel CPS  ⏱ 4 days

Same CPS infrastructure as E-1. The IoProxy recognises `type=3` (DB query) requests and dispatches to libpq (PostgreSQL), SQLite, or a Redis client depending on the connection string.

```typescript
// @symmetra/db
export declare function query(sql: string, params?: any[]): Promise<Row[]>;
```

---

### E-3 · `Date.now()` — real-time via timer thread  ⏱ 1 day

New CPU timer thread in `GpuServer::run()`:
```cpp
std::thread timerThread([this]{
    while(_running) {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        cuMemcpyHtoD(_d_timestamp, &ms, sizeof(uint64_t));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
});
```

In prelude, `Date.now()` → `*_d_timestamp` (a `__device__` global).

---

### E-4 · WebSocket upgrade  ⏱ 2 days

CPU-side: Detect `Upgrade: websocket` header in `GpuServer::handleConn`. Perform WS handshake (SHA-1 + base64 on CPU). Assign a dedicated CUDA stream to the persistent connection.

GPU-side: Emit a `symm_ws_frame_kernel` that parses the WS frame header and routes the payload to the handler. The handler can call `ctx->ws_send(data, len)` to queue a response frame.

---

## Phase F — TypeScript Features & Polish  
*Completeness and correctness for TypeScript-specific constructs.*  
**Estimated total: ~6 days**

---

### F-1 · TypeScript `enum`  ⏱ 1 day

Emit numeric enum as `namespace + const int`:
```cpp
namespace Color { 
    constexpr int Red=0, Green=1, Blue=2;
    __device__ const char* _name(int v) {
        if(v==0) return "Red"; if(v==1) return "Green"; if(v==2) return "Blue";
        return "Unknown";
    }
}
```

String enum as `const char*` constants.

---

### F-2 · `using` / `await using` declarations  ⏱ 1 day

`using file = openFile(path)` emits a RAII scope:
```cpp
auto file = openFile(path);
struct _Using_file { decltype(file)& _r; __device__ ~_Using_file(){ _r[Symbol_dispose](); } } _scope_file{file};
```
GPU destructors run at scope exit.

---

### F-3 · Decorators  ⏱ 2 days

For the common patterns (class, method):
- `@injectable class Foo {}` → Codegen wraps class in a factory pattern
- `@route('GET', '/foo') fn()` → registers route at startup in the route table

Full TC39 Stage 3 decorator semantics on GPU requires careful handling of `context` object. Start with the most common patterns (dependency injection, routing).

---

### F-4 · TypeScript namespaces  ⏱ ½ day

`namespace Foo { export function bar() {} }` → flatten to `Foo_bar()` with a namespace comment.

---

### F-5 · `Date` full implementation  ⏱ 1 day

Calendar arithmetic from milliseconds timestamp using standard algorithms (Tomohiko Sakamoto's day-of-week, Gregorian calendar conversion). All getter methods + `toISOString()` + `toLocaleString()` (simplified).

---

### F-6 · `Performance.now()` / `console.time`  ⏱ ½ day

`clock64()` → divide by SM clock rate → float milliseconds.

---

### F-7 · `Blob` / `FormData` / multipart parser  ⏱ 1.5 days

`Blob`: ByteArray + MIME type field.  
`FormData` parser: scan the request body for `boundary=` in Content-Type header; split on boundary; parse part headers and values.

---

## Phase G — Optimisation & Hardening  
*Performance, security, and developer experience improvements.*  
**Estimated total: ongoing**

---

### G-1 · `instanceof` with type tags  ⏱ 1 day

Inject `__type_id` integer field into every generated struct. `instanceof Foo` → `x.__type_id == Foo::__ID`.

---

### G-2 · `in` operator  ⏱ ½ day

`"key" in obj` → compile-time template that checks if the struct has a member named `key`. For dynamic GpuObject → iterate `_e` array.

---

### G-3 · Map capacity upgrade  ⏱ 1 day

Current hard cap of 64 entries. Upgrade to a template parameter: `Map<V, 256>`. Use open-addressing hash table with FNV-1a hash of the string key for O(1) average lookup:
```cpp
__device__ uint32_t _fnv1a(const char* s) {
    uint32_t h = 2166136261u;
    while(*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}
```

---

### G-4 · `RegExp` lookahead/lookbehind  ⏱ 3 days

Lookahead: at assertion position, speculatively advance NFA without consuming; check if sub-pattern matches; backtrack.  
Lookbehind: run NFA in reverse from current position.

---

### G-5 · `sym doctor` command  ⏱ 1 day

CLI health check: GPU present, CUDA version, NVRTC version, available VRAM, BatchContext size, estimate of max RPS given current config.

---

### G-6 · Source maps for GPU errors  ⏱ 2 days

When a GPU kernel leaves `_err_msg` set, the error response includes a source location mapping back to the original `.ts` line (stored in a VRAM line-number map injected by Codegen).

---

### G-7 · `WebSocket` production-grade  ⏱ 2 days

Ping/pong, close handshake, continuation frames, per-message compression (permessage-deflate) using GPU zlib kernel.

---

### G-8 · `yield*` delegation + async generators  ⏱ 2 days

See Phase D-3 / D-5.

---

## Quick-reference: Phase→Feature→File mapping

| Phase | Key Deliverable | Primary Files |
|-------|----------------|---------------|
| A-1 | try/catch/finally + throw | `Codegen.cpp`, `SymmParser.h`, `BatchContext.h` |
| A-2 | Map/Set string key fix | `Codegen.cpp` (prelude) |
| A-3 | JSON.parse GPU parser | `Codegen.cpp` (prelude) |
| A-4 | BigInt n suffix | `SymmParser.cpp` |
| B-1 | Error subclasses | `Codegen.cpp` (prelude) |
| B-2 | Getters/setters | `SymmParser.cpp`, `Codegen.cpp` |
| B-3 | Private fields #field | `SymmParser.cpp`, `Codegen.cpp` |
| B-4 | &&= ||= ??= | `SymmParser.cpp`, `Codegen.cpp` |
| B-5 | TS enums | `Codegen.cpp` |
| C-1..12 | Array/String/Number/Math API | `Codegen.cpp` (prelude + emitCall) |
| D-1 | console.log ring buffer | `Codegen.cpp`, `BatchContext.h`, `GpuServer.cpp` |
| D-2 | Named RegExp groups | `Codegen.cpp` |
| E-1 | fetch() split-kernel CPS | New `IoProxy.cpp`, `Codegen.cpp`, `BatchContext.h` |
| E-3 | Date.now() real-time | `GpuServer.cpp`, `Codegen.cpp` |
| E-4 | WebSocket | `GpuServer.cpp`, `Codegen.cpp` |
| F-1 | TS enums full | `Codegen.cpp` |
| F-2 | using/await using | `SymmParser.cpp`, `Codegen.cpp` |
| G-3 | Hash-table Map | `Codegen.cpp` (prelude) |

---

## Test strategy per phase

Each phase must ship with:
1. **Unit test** (no GPU) for any new parser/codegen logic
2. **Integration route** in `tests/integration/server.ts`
3. **Shell assertion** in `tests/integration/run_tests.sh` and `.ps1`

New routes to add by phase end:

| Route | Tests |
|-------|-------|
| `/try-catch` | throw caught, message returned |
| `/try-finally` | finally runs even on throw |
| `/map-string-keys` | map.get() with non-literal key strings |
| `/json-parse` | parses request body JSON |
| `/bigint-suffix` | 100n + 200n |
| `/error-subclasses` | new TypeError(), catch(e instanceof TypeError) |
| `/getter-setter` | class with get/set, correct values |
| `/enum-numeric` / `/enum-string` | enum value access |
| `/array-modern` | .at(), .flat(), .findLast(), .toSorted() |
| `/number-format` | toFixed(), toPrecision() |
| `/math-random` | Math.random() in [0,1) |
| `/console-debug` | console.log appears in debug output |
| `/fetch-basic` | await fetch('https://…') (E phase) |
| `/websocket` | WS echo handler (E phase) |

---

## Effort summary

| Phase | Focus | Effort |
|-------|-------|--------|
| A | Critical correctness (try/catch, Map, JSON.parse, BigInt) | ~6 days |
| B | Common features (errors, getters, private, enums) | ~5 days |
| C | Standard library (Array/String/Number/Math) | ~5 days |
| D | Advanced (console, named groups, generators) | ~6 days |
| E | Networking (fetch, WebSocket, Date, Streams) | ~12 days |
| F | TypeScript (enums, using, decorators, Date full) | ~6 days |
| G | Optimisation & hardening | ongoing |
| **Total** | | **~40 dev-days** |

---

*This plan is the single source of truth for Symmetra's JS/TS completion roadmap. Update feature statuses in [`public/docs/js-ts-coverage.html`](public/docs/js-ts-coverage.html) as items are implemented.*
