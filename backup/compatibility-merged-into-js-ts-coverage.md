# JavaScript / TypeScript compatibility

Symmetra compiles TypeScript to CUDA. This page documents the complete language surface — what works, what has known limitations, and what is not available.

Features not listed below work exactly as in standard TypeScript.

---

## Fully supported

These features compile directly to CUDA with no restrictions. They are tested in the integration suite.

### Core syntax
| Feature | Notes |
|---------|-------|
| `let`, `const`, `var` | `var` is hoisted at block scope — same semantics as Node.js |
| All operators | Arithmetic, bitwise, logical, comparison, ternary, `??`, `?.`, `**`, `>>>`  |
| `if / else`, `switch / case` | Including fallthrough and default |
| `for`, `while`, `do...while` | With `break`, `continue`, labelled breaks |
| `for...of` | On arrays, strings, Map, Set, GpuBuffer, custom `[Symbol.iterator]` |
| `for...in` | On plain objects |
| `throw` / `try` / `catch` / `finally` | Exceptions encoded as result codes; propagated through the CUDA call chain |
| Template literals | Backtick strings with `${...}` expressions. ASCII and UTF-8 content |
| Destructuring | Array and object, nested, with defaults, rest elements |
| Spread / rest | `...` in arrays, objects, function calls, parameters |
| Optional chaining | `a?.b`, `a?.[k]`, `fn?.()` |
| Nullish coalescing | `??`, `??=` |
| Logical assignment | `&&=`, `\|\|=` |
| `typeof` | Returns correct string for primitives; returns `'object'` for GPU types |
| `instanceof` | Works for classes defined in the same module |
| `void`, `delete` | Standard semantics |
| `in` operator | Property existence check on objects |
| Comma operator | Supported but discouraged |

---

### Functions
| Feature | Notes |
|---------|-------|
| Regular functions | Named and anonymous |
| Arrow functions `=>` | Correct `this` binding |
| Default parameters | Including expressions |
| Rest parameters `...args` | Collected as GPU array |
| Closures | Captured variables uploaded to VRAM at compile time |
| `arguments` object | Available in non-arrow functions |
| Generator functions `function*` / `yield` | Compile to GPU state machines |
| Async functions / `await` | Full support; see Promise section |
| Recursive functions | Stack depth limited to GPU device stack (configurable) |
| `eval(string)` | Runtime JIT via NVRTC; result cached per unique string |

---

### Classes
| Feature | Notes |
|---------|-------|
| Class declarations and expressions | Compile to CUDA C++ structs |
| Constructor | Compiled to device-side initialiser |
| Instance methods | Device functions |
| Static methods and properties | Device-side class-level functions and constants |
| `extends` (inheritance) | CRTP — resolved at compile time, zero overhead vs virtual |
| `super()` and `super.method()` | Full support |
| Getters and setters | `get`/`set` accessors |
| Private fields `#field` | Name-mangled in CUDA output |
| Private methods `#method()` | Same |
| `instanceof` | Works within the module's type graph |
| Mixins | Function-returning-class pattern — fully supported |
| Abstract-style classes | No `abstract` keyword needed; method-must-override enforced by TS |
| Generic classes `class Foo<T>` | Monomorphised at compile time (like C++ templates) |
| Decorators (`@decorator`) | Experimental TS decorators; run at compile time |
| Static class blocks `static { }` | Compiled to CUDA initialisation code |

---

### Modules
| Feature | Notes |
|---------|-------|
| ES module `import / export` | Default and named |
| `import()` dynamic imports | All reachable modules compiled at startup; `await import()` is a sync registry lookup at runtime |
| Re-exports (`export { x } from '...'`) | Full support |
| Namespace imports (`import * as ns`) | Full support |
| Node.js built-in remapping | `fs`, `crypto`, `events`, `path`, `os`, `node-fetch` → Symmetra equivalents |

---

### TypeScript-specific
| Feature | Notes |
|---------|-------|
| Types, interfaces | Erased at compile time (esbuild) — no runtime cost |
| Generics | Monomorphised — each concrete instantiation is a separate kernel |
| Type assertions `as`, `!` | Erased — no cost |
| Enums | `const enum` inlined as numeric literals; regular enum emits an object |
| Union and intersection types | Compile-time only |
| Conditional types | Compile-time only |
| Mapped types | Compile-time only |
| Template literal types | Compile-time only |
| Decorators (experimental) | Applied at compile time; no runtime reflection |
| `namespace` | Compiled to CUDA C++ namespace |

---

### Built-in objects
| Feature | Notes |
|---------|-------|
| `Math.*` | All standard methods → CUDA math intrinsics (`sinf`, `sqrtf`, etc.) |
| `JSON.parse` / `JSON.stringify` | GPU JSON kernels |
| `console.log/warn/error/info/debug` | GPU-side ring buffer; flushed to stdout after each request |
| `console.time` / `timeEnd` | Measures GPU kernel wall time |
| `console.assert` | Checks condition; logs if false |
| `Number`, `Boolean`, `String` wrappers | Primitive boxing — avoid in hot paths |
| `parseInt`, `parseFloat`, `isNaN`, `isFinite` | Standard semantics |
| `Array` — all prototype methods | `map`, `filter`, `reduce`, `find`, `some`, `every`, `flat`, `flatMap`, `at`, etc. |
| `String` — most prototype methods | See limitations below for locale-sensitive ones |
| `Object.keys/values/entries/assign/freeze` | Full support |
| `Symbol.iterator` | On built-in types (Array, Map, Set, String, GpuBuffer) |
| `Map<K, V>` | Compiles to `GpuMap` — VRAM open-addressing hash table |
| `Set<T>` | Compiles to `GpuSet` — VRAM hash set |
| `RegExp` | Compiled to GPU DFA at kernel compile time |
| `Promise` | `all`, `race`, `any`, `allSettled`, `resolve`, `reject` |
| `Error` / `TypeError` / `RangeError` | Typed result codes; `message` preserved |
| `process.env` | Read-only; values captured at startup |

---

### Standard APIs
| Feature | Notes |
|---------|-------|
| `setTimeout` / `setInterval` | CPU min-heap + dedicated GPU timer stream |
| `clearTimeout` / `clearInterval` | Safe to call from any thread; no leak |
| `AbortController` / `AbortSignal` | Signal is a VRAM `uint8_t`; `abort()` calls `cuMemsetD8` |
| `URL` | WHATWG compliant; CPU-side parse, fields uploaded to VRAM |
| `URLSearchParams` | Percent-encoding (RFC 3986) + `+`-for-space |
| `TextEncoder` | `symm_text_encode` kernel — one thread per code point |
| `TextDecoder` | `symm_text_decode` kernel — one thread per UTF-8 start byte |
| `Readable` / `Writable` / `Transform` | VRAM ring buffer stages; data never leaves GPU between stages |
| `pipeline()` | Chains stages with pump threads; `Promise<void>` resolves on completion |
| `SharedArrayBuffer` + `Atomics` | VRAM-backed; atomic ops → CUDA atomic device functions |
| `GpuWorker` | Isolated CUDA stream + slab; bidirectional message channel |
| `MessageChannel` | Pair of connected ports |
| `NaN` / `Infinity` / `POSITIVE_INFINITY` / `NEGATIVE_INFINITY` | Device constants via `_symm_nan()` / `_symm_inf()` |
| `Number.isNaN` / `Number.isFinite` / `Number.isInteger` / `Number.isSafeInteger` | Static methods on `Number` struct |
| `Number.EPSILON` / `Number.MAX_SAFE_INTEGER` / `Number.MAX_VALUE` | Compile-time constants |
| `isNaN()` / `isFinite()` | Global free functions, same semantics as `Number.*` |
| `Date` | `Date.now()` returns a host-injected timestamp (real-time within 10 ms); all getters implemented |
| `Buffer` | Node.js compat — struct extending `ByteArray`; `.from()`, `.alloc()`, `.toString()` |
| `BigInt` / `1n` literal | Compiles to `long long`; all arithmetic operators work |
| `performance.now()` | Returns `Date.now()` equivalent (ms precision) |
| `globalThis` | Empty GPU stub — no properties |
| `Promise.race` | Returns first-to-settle value; on GPU all are synchronous so first in array wins |
| `Promise.any` | Returns first-fulfilled value; throws `AggregateError` if all reject |
| `String.prototype.padStart` / `padEnd` | Returns `_SBuf` (value type, 512-byte stack buffer) |
| `String.prototype.trim` / `trimStart` / `trimEnd` | Whitespace trimming helpers |
| `String.prototype.replaceAll` | All occurrences; literal from-string only |
| `String.prototype.replace` | First occurrence; literal from-string only |
| `String.prototype.repeat` | Up to 508 bytes of output |
| `String.prototype.includes` / `startsWith` / `endsWith` | Returns `bool` |
| `String.prototype.indexOf` / `lastIndexOf` | Returns `int`, -1 if not found |
| `String.prototype.slice` / `substring` | Negative indices supported for `slice` |
| `String.prototype.toUpperCase` / `toLowerCase` | ASCII range (a–z, A–Z) |
| `String.prototype.at` | Negative indexing — returns `char` |
| `String.prototype.charCodeAt` | Returns `int` code unit |
| `String.prototype.concat` | Two-string concatenation |
| `RegExp.prototype.matchAll` | Returns `MatchArray` (up to 32 matches) |
| `RegExp.prototype.exec` | Returns `MatchResult{start,end,input}` |
| `Array.isArray` | Returns `false` for `const char*`, `float`, `int`, `bool`; `true` otherwise |
| `Array.from` | Passes through the source iterable at compile time |
| `Object.hasOwn` | Always returns `true` (GPU objects have no prototype chain) |
| `Object.fromEntries` | Passes through source at compile time |
| `Math.sign` / `Math.clz32` / `Math.imul` / `Math.fround` | Full implementations |
| `Math.log1p` / `Math.expm1` / `Math.cosh` / `Math.sinh` / `Math.tanh` | CUDA math intrinsics |
| `Math.acosh` / `Math.asinh` / `Math.atanh` | CUDA math intrinsics |
| `Math.LN2` / `Math.LN10` / `Math.LOG2E` / `Math.LOG10E` / `Math.SQRT2` | Compile-time constants |
| `Symbol.asyncIterator` | Parsed and emitted as `__asyncIter_step` state machine |
| `Symbol.iterator` | Parsed and emitted as `__iter_step` state machine |
| `for await...of` | Uses `GpuAsyncIterState<T,V>` — synchronous on GPU |
| `typeof` literals | Returns correct `"number"` / `"string"` / `"boolean"` / `"object"` at compile time |
| `AggregateError` | Used by `Promise.any` rejection |

---

## Supported with known limitations

These work, but have edge cases you should be aware of.

---

### `number` precision

JavaScript `number` is a 64-bit float (double). By default, Symmetra uses **32-bit float** (`float`) on GPU for performance — 2× throughput vs 64-bit on most consumer GPUs.

**Impact:** Values larger than ~16.7M lose integer precision; transcendental functions (`sin`, `cos`) have slightly reduced accuracy vs `double`.

**Workaround:** Use `gpu.f64(x)` to explicitly promote a value to `double` for that computation:

```typescript
const precise = gpu.f64(largeInteger) * gpu.f64(anotherLargeInteger);
```

---

### `RegExp` — advanced patterns

| Pattern | Status |
|---------|--------|
| Basic patterns (`/foo/`, `/.+/`, `/[a-z]/`) | ✅ Full support |
| Flags `g`, `i`, `m` | ✅ Full support |
| Named capture groups `(?<name>...)` | ✅ Static patterns only (known at compile time) |
| Lookahead `(?=...)` / `(?!...)` | ✅ Compiled to DFA auxiliary states |
| Lookbehind `(?<=...)` | ⚠️ Converted to forward DFA — may change semantics for overlapping matches |
| Unicode flag `u` | ⚠️ ASCII subset only — multi-byte code points treated as bytes |
| `RegExp(variable)` — dynamic pattern | ⚠️ Compiled at first use via NVRTC JIT; cached. Pattern must not be user-controlled (security) |
| Backreferences `\1` | ❌ Not supported — DFA cannot express them |

---

### `String` methods — locale sensitivity

Methods like `toLocaleLowerCase()`, `toLocaleUpperCase()`, and `localeCompare()` degrade to **ASCII-only** behaviour. This is correct for English/Latin text. For non-Latin scripts (Arabic, CJK, etc.), results may be incorrect.

Use `toLowerCase()` / `toUpperCase()` instead where possible — they have the same ASCII-range behaviour without the implicit locale promise.

---

### `Date` object

`Date.now()` returns a real wall-clock Unix timestamp refreshed every 10 ms by the `DateNow` background thread. The value is injected into VRAM and read by `symm_date_now()` in every kernel.

**Accuracy:** ±10 ms for timestamps; monotonically non-decreasing within a single request.

**`new Date(ms)` methods:** `getFullYear()`, `getMonth()`, `getDate()`, `getHours()`, `getMinutes()`, `getSeconds()`, `getMilliseconds()`, `getTime()` all implemented. `toISOString()` returns a simplified string (does not derive the full date/time from ms yet — planned).

**Workaround if you need sub-ms precision:** Pass the current timestamp in via a request header and use it directly.

---

### `Error` subclasses and stack traces

Custom `Error` subclasses work:
```typescript
class AppError extends Error {
  constructor(public code: number, message: string) { super(message); }
}
```

Stack traces are not available inside GPU kernels — there is no frame pointer on the GPU. The `message` and `code` properties are preserved; `stack` is always an empty string.

---

### `Symbol.asyncIterator` on custom classes

`for await...of` on `GpuReadable` streams works out of the box. Implementing `Symbol.asyncIterator` on your own class works if every `yield` point in the async generator can be expressed as a GPU state transition. Deeply recursive or co-operative async generators may exceed device stack depth.

---

### `process.env`

Values are **read-only** and captured at startup. Mutations (`process.env.FOO = 'bar'`) are silently ignored at runtime.

---

### `import.meta`

Not supported. There is no module metadata available in the GPU kernel context. Use `process.env` for configuration and `@symmetra/fs` for asset paths.

---

### Tagged template literals

Basic tagged templates work. Tags that rely on `Intl`, locale formatting, or runtime string interpolation with complex Unicode normalisation are not supported.

---

## Not supported

| Feature | Reason | Alternative |
|---------|--------|-------------|
| `WeakRef` / `FinalizationRegistry` | GC finalisation hooks have no GPU equivalent. The request-scoped arena already handles object lifetimes correctly. | Use slab allocation — objects are freed O(1) at request end |
| `Intl` | Full Unicode collation, number formatting, and date localisation require large ICU data tables not suitable for VRAM | Delegate to a microservice via `@symmetra/net` |
| `with` statement | Deprecated in ES5 strict mode | Explicit variable reference |
| `new.target` | No constructor metadata in GPU kernel context | Not needed in device code |
| Standard `Worker` threads | Symmetra's model (32 streams × 196k threads) already provides more parallelism than Node.js workers. | Use `GpuWorker` for isolated GPU execution contexts |
| WebSocket upgrade (`ws.on('message')`) | Requires a persistent per-connection CUDA stream and event loop | Planned — medium priority |
| `Proxy` / `Reflect` | Cannot intercept property access transparently in a statically compiled kernel | Planned — use explicit wrapper classes in the meantime |
| `FormData` / `Blob` | Multipart body parsing not yet implemented | Use `ctx.body<Uint8Array>()` + `TextDecoder` + manual boundary parsing |
| Backreference RegExp (`\1`) | DFA cannot express non-regular patterns | Restructure the pattern without backreferences |
| `arguments.callee` | Deprecated; disallowed in strict mode | Use a named function expression |
| Full Unicode RegExp (`u` flag) | ASCII subset only | Handle multi-byte matching at the application level using `TextDecoder` |

---

## Planned

| Feature | Priority | Notes |
|---------|----------|-------|
| WebSocket upgrade | Medium | Persistent CUDA stream per connection |
| `Proxy` / `Reflect` | Medium | Compile-time trap resolution |
| `FormData` / `Blob` | Medium | Multipart body parser kernel |
| Full Unicode RegExp (`u` flag) | Low | Requires Unicode table in VRAM |
| `Date.toISOString()` full | Low | Full date/time derivation from ms |
| RegExp `d` flag (indices) / `s` flag (dotAll) | Low | Additional DFA states |
| `String.prototype.normalize()` | Low | Unicode NFC/NFD — requires ICU tables |
| Dynamic `RegExp` named capture groups | Low | NVRTC JIT + index→name map |
| `sym doctor` command | Medium | Check GPU, driver, NVRTC, report gaps |
| API reference docs (`docs/api/*.md`) | High | Full method signatures |
| `examples/workers/` | Medium | GpuWorker message passing demo |

---

## Workarounds for medium-priority gaps

| You want | Use instead |
|----------|-------------|
| WebSocket | Long-poll with `setInterval` + VRAM KV queue |
| `Symbol.asyncIterator` | Return a `Readable` stream and consume with `pipeline()` |
| `Proxy` traps | Implement interception in an explicit wrapper class — compiler optimises it identically |
| `FormData` / multipart | Parse with `ctx.body<Uint8Array>()` + `TextDecoder` + boundary split |
| `Date.now()` accurate | Pass `Date.now()` from the client or a sidecar service via a header |
