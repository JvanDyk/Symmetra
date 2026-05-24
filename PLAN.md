# Symmetra — Master Plan

**Goal:** A production-grade TypeScript-to-CUDA server engine with no meaningful gaps vs Node.js for real server workloads.

Status legend: ✅ Done  ⚠️ Partial  🔲 Not started

---

## 1. Testing — make the test suite speak for itself

These are the highest-leverage tasks. Tests reveal real bugs; without them nothing else can be trusted.

### 1.1 Unit tests (no GPU required) — build with CMakeLists in `tests/`
| Test | Status | File |
|------|--------|------|
| `UrlParser` — all WHATWG cases (scheme, authority, IPv6, percent-decode) | ✅ | `tests/unit/test_url_parser.cpp` |
| `UrlSearchParams` — encode/decode roundtrip, multi-value, delete | ✅ | `tests/unit/test_url_parser.cpp` |
| `TimerQueue` — setTimeout fires, setInterval repeats, clearTimer stops, ordering | ✅ | `tests/unit/test_timer_queue.cpp` |
| `ThreadPool` — concurrent futures, queue depth, destructor drains | ✅ | `tests/unit/test_thread_pool.cpp` |
| `GpuAllocator` heap — split block, coalesce, alignment, iterator-bug regression | ⚠️ Written | `tests/unit/test_allocator.cpp` (needs CUDA GPU to run) |
| `VramPipe` — push/pull ordering, backpressure, close unblocks | ⚠️ Written | `tests/unit/test_vram_pipe.cpp` (needs CUDA GPU to run) |

**How to run:**
```bash
mkdir build/tests && cd build/tests
cmake ../../tests && cmake --build . && ctest --output-on-failure
```

### 1.2 Integration tests (requires running server)
| Test | Status | File |
|------|--------|------|
| All core language features (see `run_tests.sh`) | ✅ Written | `tests/integration/run_tests.sh` |
| Windows version | ✅ Written | `tests/integration/run_tests.ps1` |
| TextEncoder UTF-8 roundtrip (multi-byte) | ✅ | covered in shell script |
| URL parse + URLSearchParams ops | ✅ | covered in shell script |
| setTimeout fires + clearTimeout cancels | ✅ | covered in shell script |
| AbortController signal + listener | ✅ | covered in shell script |
| KV put/get/delete | ✅ | covered in shell script |
| GPU map/reduce on Float32Array | ✅ | covered in shell script |
| `typeof` correct for literals | ✅ | `/typeof-check` route |
| String methods: padStart/padEnd/trim/replaceAll/repeat/slice | ✅ | `/string-methods` route |
| `NaN`, `Infinity`, `Number.*` statics | ✅ | `/number-methods` route |
| `Array.isArray`, `Array.from` | ✅ | `/array-statics` route |
| `Object.hasOwn`, `Object.fromEntries` | ✅ | `/object-statics` route |
| `Date.now()` returns non-negative number | ✅ | `/date-now` route |
| Math extras: `sign`, `clz32`, `hypot`, `log1p` | ✅ | `/math-extras` route |
| `RegExp.matchAll` returns all positions | ✅ | `/matchall` route |
| `Promise.race` picks fastest | ✅ | `/promise-race` route |
| Generator + break after N values | ✅ | `/generator-take` route |
| `Symbol.asyncIterator` / `for await...of` | ✅ | `/async-iterator` route |
| `BigInt` basic arithmetic | ✅ | `/bigint-basic` route |

**How to run:**
```bash
sym ./tests/integration/server.ts &
bash tests/integration/run_tests.sh
```

### 1.3 Missing unit tests to write next
| Test | Priority | File |
|------|----------|------|
| `GpuAllocator` — heap alloc/free/coalesce (requires CUDA context) | High | `tests/unit/test_allocator.cpp` ⚠️ Written |
| `StreamPool` — acquire/release concurrency under saturation | High | `tests/unit/test_stream_pool.cpp` ✅ Written |
| `GpuStream / VramPipe` — end-of-stream sentinel, early-exit no-deadlock | High | `tests/unit/test_vram_pipe.cpp` ⚠️ Written |
| `AbortSignal` — VRAM flag set, listener fires, double-abort safe | Medium | `tests/unit/test_abort_signal.cpp` ✅ Written |
| `HttpKernels` — header parser correctness, JSON serialiser correctness | Medium | `tests/unit/test_http_kernels.cpp` ✅ Written |
| `TextKernel` — UTF-8 encode/decode for all 4-byte sequences | Medium | `tests/unit/test_text_kernel.cpp` ✅ Written |
| `GpuAllocator` slab — bump-pointer alloc, reset is O(1) | Medium | (merge into test_allocator) |
| `BatchScheduler` — fill timeout, route isolation, drain, metrics, concurrency | ✅ Done (23/23 pass, CPU-only) | `tests/unit/test_batch_scheduler.cpp` |

---

## 2. Language gaps to implement

Ordered by how often real server code needs them.

### 2.1 High priority (block common npm packages)

| Gap | What's needed | Status |
|-----|---------------|--------|
| `Date.now()` real-time | CPU timestamp fed via CUDA event; pinned host variable updated by timer thread | ✅ Stub in prelude; `DateNow.h` for production use |
| `String.prototype.matchAll` | GPU NFA returns all match positions | ✅ Done — `RegExp.matchAll()` + `symm_str_match_all()` |
| Dynamic `RegExp` with named groups | Parse named groups at NVRTC JIT time | ⚠️ Static patterns work; named groups require NVRTC JIT |
| `Symbol.asyncIterator` on custom classes | GPU state machine derivation for async generators | ✅ Done — `emitClass` generates `__asyncIter_step` + `__asyncIterator()` |
| `Promise.race` with early abort | Wire AbortSignal to losing streams | ✅ Done — `_PromiseVal` struct in prelude; race() emits first-wins |
| `typeof` correct type strings | Compile-time inference from literal types | ✅ Done — TypeofExpr emitter updated |
| `NaN`, `Infinity`, global constants | Device constants in prelude | ✅ Done |
| `Number.*` static methods | `isNaN`, `isFinite`, `isInteger`, `isSafeInteger` | ✅ Done — `Number` struct |
| `String` prototype methods | `padStart`, `padEnd`, `trim`, `replaceAll`, `repeat`, `slice`, etc. | ✅ Done — `_SBuf` return-by-value helpers |
| `Array.isArray`, `Array.from` | Static method dispatch in codegen | ✅ Done |
| `Object.hasOwn`, `Object.fromEntries` | Static method dispatch in codegen | ✅ Done |
| `for await` / `Symbol.asyncIterator` | Parser + codegen + state machine | ✅ Done |
| `BigInt` basics | `typedef long long bigint` + `n` suffix codegen | ✅ Done (prelude typedef; parser needs `n` suffix) |
| `Math.sign`, `Math.clz32`, `Math.imul` | Extra Math functions | ✅ Done — added to MATH_REMAP and prelude |
| `Buffer` (Node.js compat) | Struct extending ByteArray | ✅ Done |

### 2.2 Medium priority (needed for library compatibility)

| Gap | What's needed |
|-----|---------------|
| WebSocket upgrade (`upgrade(ctx)`) | Persistent per-connection CUstream; framing kernel; ping/pong |
| `Proxy` / `Reflect` traps | Compile-time trap resolution — emit wrapper device functions |
| `FormData` / `Blob` / multipart | Boundary-split kernel; MIME type detection |
| `Date` full object | `new Date(ms)`, `.getFullYear()`, `.toISOString()` — purely CPU-side |
| `Buffer` (Node.js) | Map to `GpuBuffer<Uint8Array>`; port `.toString('base64')`, `.from()` |

### 2.3 Low priority

| Gap | What's needed |
|-----|---------------|
| Full Unicode RegExp (`u` flag) | Unicode table in VRAM (~250 KB); DFA state explosion for large charsets |
| `Intl.NumberFormat` | Too complex for GPU; delegate to microservice |
| `import.meta.url` | No module metadata in kernel; could inject as a compile-time constant |

---

## 3. Engine robustness

These are correctness, performance, and hardening tasks for the C++ runtime.

### 3.1 Correctness — must fix before first public release

| Task | Status | File |
|------|--------|------|
| `GpuAllocator::heapAlloc` iterator invalidation | ✅ Fixed | `GpuAllocator.cpp` |
| `GpuStream` pipeline double-free | ✅ Fixed | `GpuStream.cpp` |
| `GpuReadable::pipe()` deadlock on early consumer exit | ✅ Fixed | `GpuStream.cpp` |
| `cuMemAllocAsync` → `cuMemAlloc` (CUDA 10.x compat) | ✅ Fixed | `GpuStream.cpp` |
| `HttpKernels` JSON serialiser race (missing `__syncthreads`) | ✅ Fixed | `HttpKernels.cu` |
| `GpuServer` single `recv()` truncates large bodies | ✅ Fixed | `GpuServer.cpp` |
| `GpuServer` ioThreads default less than streams | ✅ Fixed | `GpuServer.h` |
| `GpuServer` `Connection: close` on every response | ✅ Fixed | `GpuServer.cpp` |
| `GpuServer` response length via `strlen` | ✅ Fixed | `GpuServer.cpp` |
| `TimerQueue` `cancelled_` set unbounded growth | ✅ Fixed | `TimerQueue.cpp` |
| NvrtcCompiler PTX in heap after load | ✅ Fixed | `NvrtcCompiler.cpp` |
| `GpuWorker::terminate()` TOCTOU race | ✅ Fixed | `GpuWorker.cpp` |
| `GpuServer` signal handling unsafe in multi-threaded context | ✅ Fixed | `GpuServer.cpp` |
| `WarpScheduler::occupancy()` creates `cudaDeviceProp` on every call | ✅ Fixed | `WarpScheduler.h` |

### 3.2 Scalability improvements

| Task | Impact | Notes |
|------|--------|-------|
| Decouple accept from GPU dispatch (truly async handleConn) | ✅ Done | `BatchScheduler` + `acceptLoopBatch()` — accept threads submit and immediately loop back |
| HTTP/2 support | High | Multiplexed streams; requires TLS + HPACK |
| Per-stream VRAM response length header | Medium | Already started; kernel must write `d_respLen` |
| `GpuAllocator` heap: size-class free-lists | Medium | Replace O(n) first-fit with O(1) size-class lookup |
| `StreamPool` NUMA-aware stream assignment | Low | Pin streams to GPU L2 partitions for cache locality |
| Connection keep-alive timeout | Medium | Close idle connections after N seconds to reclaim stream slots |

### 3.3 Security hardening

| Task | Status | Notes |
|------|--------|-------|
| PTX zeroed after `cuModuleLoadData` | ✅ Done | `NvrtcCompiler.cpp` |
| AntiDebug on all platforms | ⚠️ Partial | Linux: ptrace check; Windows: IsDebuggerPresent; needs macOS |
| Input sanitisation: request path traversal | 🔲 | Reject `../` in path before route match |
| Request body size cap enforced strictly | 🔲 | `recvAll()` should return 413 if body > `maxBodyBytes` |
| Rate limiting built-in (per-IP) | 🔲 | Token bucket in VRAM; configurable via `RuntimeOptions` |
| TLS / HTTPS support | 🔲 | Requires a CPU-side TLS handshake (mbed-TLS or similar) before VRAM dispatch |

---

## 4. Public documentation

| Document | Status |
|----------|--------|
| `docs/getting-started.md` — install + first server | ✅ Done |
| `docs/compatibility.md` — full JS/TS gap matrix | ✅ Done (this session) |
| `docs/architecture.md` — internals + GPU pipeline | ✅ Done |
| `docs/api/core.md` — `@symmetra/core` reference | ✅ Done |
| `docs/api/http.md` — Router + SymmCtx reference | ✅ Done |
| `docs/api/kv.md` — KvStore reference | ✅ Done |
| `docs/api/fs.md` — filesystem API reference | ✅ Done |
| `docs/api/crypto.md` — crypto + JWT reference | ✅ Done |
| `docs/api/net.md` — HTTP client + DNS reference | ✅ Done |
| `docs/api/db.md` — postgres/sqlite/redis reference | ✅ Done |
| Benchmark results page | 🔲 |
| Migration guide: Express → Symmetra | ✅ Done |
| Migration guide: Fastify → Symmetra | ✅ Done |

---

## 5. Examples — what's done and what's missing

| Example | Status |
|---------|--------|
| `examples/hello/` | ✅ Done |
| `examples/http-server/` | ✅ Done |
| `examples/database/` | ✅ Done |
| `examples/files/` | ✅ Done |
| `examples/crypto-auth/` | ✅ Done |
| `examples/env-config/` | ✅ Done |
| `examples/http-client/` | ✅ Done |
| `examples/fullstack/` | ✅ Done |
| `examples/benchmark/` | ✅ Done |
| `examples/streams/` | ✅ Done |
| `examples/timers/` | ✅ Done |
| `examples/abort/` | ✅ Done |
| `examples/url-text/` | ✅ Done |
| `examples/classes/` | ✅ Done |
| `examples/workers/` — GpuWorker message passing | ✅ Done |
| `examples/websocket/` — when WS is implemented | 🔲 |
| `examples/rate-limiter/` — production rate limiting | 🔲 |
| `examples/streaming-upload/` — large file ingest | 🔲 |

---

## 6. Distribution and release

| Task | Status |
|------|--------|
| GitHub Releases workflow (Actions) | ✅ Documented in RELEASE.md |
| `install.ps1` / `install.sh` | ✅ Done |
| `sym doctor` command | 🔲 — check GPU, driver, NVRTC, report missing items clearly |
| Signed binaries (GPG or Windows Authenticode) | 🔲 |
| Auto-update check (`sym update`) | 🔲 |
| Changelog (`CHANGELOG.md`) | 🔲 |
| Versioned release tags | 🔲 |

---

## Immediate next steps (ordered)

1. ✅ **Build and run unit tests** — 3/3 pass (`test_url_parser`, `test_timer_queue`, `test_thread_pool`)
2. ✅ **Fix `GpuWorker::terminate()` TOCTOU** — `std::call_once` guarantees one-shot shutdown
3. ✅ **Fix signal handling** — `sigaction` + `pthread_sigmask` to block SIGINT in IO threads
4. ✅ **Fix `WarpScheduler::occupancy()`** — single `cudaGetDeviceProperties` call, cached forever
5. ✅ **Write `test_allocator.cpp` / `test_vram_pipe.cpp`** — written; require CUDA GPU to run
6. ✅ **Write `test_abort_signal.cpp` / `test_stream_pool.cpp` / `test_http_kernels.cpp` / `test_text_kernel.cpp`**
7. ✅ **Implement language gap fills** — `typeof`, `NaN`/`Infinity`, `Number.*`, `String.*` methods, `Array.isArray`, `Object.hasOwn`, `Math.sign/clz32/imul`, `Buffer`, `BigInt`, `Date` stub, `Symbol.asyncIterator`, `for await`, full `RegExp.matchAll`, `Promise.race`
8. ✅ **Extend integration tests** — 22 new routes + 50+ new assertions in `run_tests.sh`/`run_tests.ps1`
9. **Run integration tests** — `sym ./tests/integration/server.ts` then `bash tests/integration/run_tests.sh`
10. ✅ **Add API reference docs** — `docs/api/http.md`, `core.md`, `kv.md`, `fs.md`, `crypto.md`, `net.md`, `db.md`
11. **Add `sym doctor` command** — essential for onboarding new users
12. **Implement WebSocket upgrade** — unlocks real-time apps
13. ✅ **Write `examples/workers/`** — GpuWorker message passing, pool, background jobs
14. ✅ **`Date.now()` real-time** — `_symm_now_ms` global in PRELUDE, refreshed before each kernel launch via `cuMemcpyHtoDAsync`; `NvrtcCompiler` binds module global after load
15. ✅ **Write migration guides** — `docs/migrate-from-express.md`, `docs/migrate-from-fastify.md`
16. **Write benchmark results page** — throughput numbers vs Node.js / Bun / Fastify
17. **Add `sym doctor` command** — GPU, CUDA driver, NVRTC, esbuild check with clear error messages
18. **Add `CHANGELOG.md`** — prepare for first public release

### Recent codegen fixes (this session)
19. ✅ **Template literal in expr context** — now emits `([&]()->_SBuf{ ... }())` IIFE instead of placeholder
20. ✅ **`for...of` on strings** — AST-level type detection; emits char-by-char `char[2]` loop for string types
21. ✅ **`parseInt(str, radix)`** — `symm_parse_int_radix(s, r)` helper for any base 2–36
22. ✅ **`process.uptime()`** — `_symm_start_ms` GPU global set at module load; `_Process::uptime()` returns `(now - start) / 1000`
23. ✅ **`emitLiteral` string escaping** — properly escapes `"`, `\n`, `\r`, `\t`, `\0` in emitted C++ string literals
24. ✅ **`ObjectExpr` dynamic values** — when object has non-literal props, emits IIFE building JSON in `_SBuf`
25. ✅ **`??` double-evaluation** — fixed to use lambda `auto _nc = lhs; return _nc ? _nc : rhs`
26. ✅ **`&&=`, `||=`, `??=`** — logical assignment operators emitted correctly
27. ✅ **`Array.prototype.reduce/slice/concat/flat`** — GPU-safe templated helpers + codegen dispatch
28. ✅ **`String.prototype.charAt/at`** — mapped to `symm_str_at` (returns `_SBuf`, not `char`)
29. ✅ **`JSON.stringify`** — strings are now JSON-quoted in GPU output
30. ✅ **`_SBuf` `symm_write`/`symm_json_val` overloads** — prevents template fallback from casting `_SBuf` to `float`
31. ✅ **`OptChainExpr` `.` vs `->` selector** — uses `->` only for ctx/this pointers
32. ✅ **Codegen unit test** — `engine/src/compiler/test_codegen.cpp` with 60+ tests, enabled via `-DSYMM_BUILD_TESTS=ON`

---

## What is NOT a gap (common misconceptions)

| You might think... | Reality |
|---------------------|---------|
| "Classes aren't supported" | Full class system including generics, mixins, private fields, decorators |
| "You can't use npm packages" | Node.js built-ins are remapped; packages that don't use OS APIs work |
| "No error handling" | `try/catch/finally` fully supported; exceptions are result codes in CUDA |
| "Async/await doesn't work" | Full async/await + Promise.all/race/any — parallel CUDA stream dispatch |
| "No streaming" | `Readable/Writable/Transform/pipeline` all implemented as VRAM ring buffers |
| "No standard timers" | `setTimeout/setInterval/clearTimeout/clearInterval` all implemented |
| "No fetch cancellation" | `AbortController/AbortSignal` implemented as a VRAM flag |
| "Workers aren't supported" | `GpuWorker` exists with its own stream and slab — different API, same concept |
