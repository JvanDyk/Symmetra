# `@symmetra/core` — Core Runtime Reference

The core package exposes low-level GPU primitives, worker threads, typed buffers, and stream utilities. Everything in `@symmetra/http` and `@symmetra/kv` is built on top of these.

## Installation

```typescript
import { GpuBuffer, GpuWorker, GpuReadable, GpuWritable } from '@symmetra/core';
```

---

## `GpuBuffer<T>`

A typed array allocated in VRAM. Zero-copy — no data ever leaves the GPU unless you explicitly call `.toArray()`.

### Construction

`GpuBuffer` instances are not created directly by user code. They arrive as the return value of `ctx.body<Float32Array>()` or as output of `.map()` / `.filter()`.

### Properties

#### `buf.length: number`
Number of elements.

#### `buf.byteLength: number`
Total byte size.

### Methods

#### `buf.map(fn: (value: T, index: number) => T): GpuBuffer<T>`

Apply `fn` to every element in parallel. Compiles to a `__global__` CUDA kernel — one thread per element.

```typescript
const squared = data.map(x => x * x);
const sineMap = data.map(x => Math.sin(x) * 0.5 + 0.5);
```

#### `buf.filter(fn: (value: T, index: number) => boolean): GpuBuffer<T>`

Return a new buffer containing only elements for which `fn` returns `true`. Uses a prefix-sum compaction kernel.

```typescript
const positive = data.filter(x => x > 0);
```

#### `buf.reduce(fn: (acc: T, value: T) => T, initial: T): T`

Reduce all elements to a single value. Uses a parallel tree-reduction kernel.

```typescript
const sum = data.reduce((a, b) => a + b, 0);
const max = data.reduce((a, b) => (a > b ? a : b), -Infinity);
```

#### `buf.toArray(): T[]`

Copy data from VRAM to host and return a plain JS array. Incurs a D→H memcpy. Avoid inside hot handlers.

```typescript
const arr = data.toArray();
```

#### `buf.slice(start: number, end?: number): GpuBuffer<T>`

Return a view into a sub-range. Zero-copy — no data is moved.

---

## `GpuWorker`

An independent CUDA stream with its own memory slab. Use for background tasks that must not block request handlers.

### Construction

```typescript
const worker = new GpuWorker();
```

### Methods

#### `worker.postMessage(data: unknown): void`

Send a value to the worker. Serialised to JSON and pinned to VRAM.

#### `worker.onmessage: ((data: unknown) => void) | null`

Receive a value back from the worker.

#### `worker.terminate(): void`

Drain the worker's queue and release its CUDA resources.

### Worker script

Inside a worker, use `self.onmessage` and `self.postMessage`:

```typescript
// worker.ts
self.onmessage = async (data: Float32Array) => {
  const result = data.map(x => Math.sqrt(x));
  self.postMessage(result);
};
```

```typescript
// server.ts
import { GpuWorker } from '@symmetra/core';

const worker = new GpuWorker();
worker.onmessage = (result) => {
  console.log('worker result:', result);
};
worker.postMessage(myFloat32Array);
```

---

## Streams

### `GpuReadable<T>`

An async-iterable stream of chunks produced in VRAM.

```typescript
async function* producer(): AsyncGenerator<Float32Array> {
  for (let i = 0; i < 10; i++) {
    yield new Float32Array([i, i * 2, i * 3]);
  }
}

const readable = GpuReadable.from(producer());

for await (const chunk of readable) {
  console.log(chunk.toArray());
}
```

#### Methods

| Method | Description |
|--------|-------------|
| `readable.pipe(writable)` | Pipe this stream to a `GpuWritable` |
| `readable.map(fn)` | Transform each chunk |
| `readable.filter(fn)` | Filter chunks |

### `GpuWritable<T>`

A writable sink that receives chunks.

```typescript
const writable = new GpuWritable<Float32Array>({
  write(chunk) {
    // process chunk in VRAM
  },
  close() {
    // stream ended
  },
});
```

### `pipeline(readable, ...transforms, writable): Promise<void>`

Pipe a readable through zero or more transform stages into a writable. Backpressure is handled automatically via `VramPipe` ring buffers.

```typescript
import { pipeline } from '@symmetra/core';

await pipeline(
  sourceReadable,
  async function* transform(src) {
    for await (const chunk of src) {
      yield chunk.map(x => x * 2);
    }
  },
  sinkWritable,
);
```

---

## `AbortController` / `AbortSignal`

Standard Web API — implemented as a VRAM boolean flag. Aborting from CPU is lock-free and visible to all GPU threads on the next iteration.

```typescript
const controller = new AbortController();
const { signal } = controller;

signal.addEventListener('abort', () => {
  console.log('aborted!');
});

// Later:
controller.abort();
console.log(signal.aborted); // true
```

Passing `signal` to a fetch or timer:

```typescript
const timer = setTimeout(() => resolve('timeout'), 5000);
signal.addEventListener('abort', () => clearTimeout(timer));
```

---

## Timers

Standard Web API — implemented by `TimerQueue` (CPU-side, feeds GPU handlers via pinned callbacks).

```typescript
// Fire once after 100ms
const id = setTimeout(() => {
  console.log('fired');
}, 100);

// Cancel before it fires
clearTimeout(id);

// Repeat every 1 second
const intervalId = setInterval(() => {
  console.log('tick');
}, 1000);

// Stop
clearInterval(intervalId);
```

---

## `TextEncoder` / `TextDecoder`

Standard Web API — implemented as GPU UTF-8 kernels.

```typescript
const encoder = new TextEncoder();
const decoder = new TextDecoder();

const bytes   = encoder.encode('Hello, 世界!');  // Uint8Array in VRAM
const text    = decoder.decode(bytes);            // back to string
```

---

## `URL` / `URLSearchParams`

Standard Web API — implemented as GPU string kernels following the WHATWG URL spec.

```typescript
const u = new URL('https://example.com/path?q=hello&limit=10');
console.log(u.protocol);   // "https:"
console.log(u.hostname);   // "example.com"
console.log(u.pathname);   // "/path"
console.log(u.search);     // "?q=hello&limit=10"

const params = new URLSearchParams(u.search);
console.log(params.get('q'));      // "hello"
console.log(params.get('limit')); // "10"
```

---

## `Buffer` (Node.js compat)

`Buffer` extends `Uint8Array` with Node.js-compatible static factory methods.

```typescript
const b1 = Buffer.from('hello', 'utf8');
const b2 = Buffer.from([0x48, 0x69]);
const b3 = Buffer.alloc(16);

console.log(b1.toString('utf8'));    // "hello"
console.log(b1.toString('hex'));     // "68656c6c6f"
console.log(b1.toString('base64')); // "aGVsbG8="
```

---

## Global constants and functions

| Symbol | Type | Value |
|--------|------|-------|
| `NaN` | `number` | IEEE 754 Not-a-Number |
| `Infinity` | `number` | +∞ |
| `NEGATIVE_INFINITY` | `number` | −∞ |
| `isNaN(x)` | `(number) => boolean` | True if `x` is NaN |
| `isFinite(x)` | `(number) => boolean` | True if `x` is finite |
| `parseInt(s, radix?)` | `(string, number?) => number` | Parse integer |
| `parseFloat(s)` | `(string) => number` | Parse float |

### `Number` static methods

```typescript
Number.isNaN(NaN)        // true
Number.isNaN(42)         // false
Number.isFinite(42)      // true
Number.isFinite(Infinity) // false
Number.isInteger(3)      // true
Number.isInteger(3.5)    // false
Number.isSafeInteger(Number.MAX_SAFE_INTEGER) // true
Number.MAX_SAFE_INTEGER  // 9007199254740991
Number.MIN_SAFE_INTEGER  // -9007199254740991
Number.EPSILON           // 2.220446049250313e-16
```

### `Math` — all standard methods

All standard `Math` methods are supported and map to CUDA intrinsics:

```typescript
Math.sin(x), Math.cos(x), Math.tan(x)
Math.asin(x), Math.acos(x), Math.atan(x), Math.atan2(y, x)
Math.sqrt(x), Math.cbrt(x)
Math.pow(x, y), Math.exp(x), Math.log(x), Math.log2(x), Math.log10(x)
Math.abs(x), Math.ceil(x), Math.floor(x), Math.round(x), Math.trunc(x)
Math.min(a, b), Math.max(a, b)
Math.sign(x)    // -1, 0, or 1
Math.clz32(x)   // count leading zeros (32-bit)
Math.imul(a, b) // 32-bit integer multiply
Math.hypot(a, b)
Math.log1p(x), Math.expm1(x)
Math.fround(x)  // nearest float32 value
Math.PI, Math.E, Math.LN2, Math.LN10, Math.LOG2E, Math.LOG10E, Math.SQRT2
```

### `Array` static methods

```typescript
Array.isArray([])        // true
Array.isArray('hello')   // false
Array.from({ length: 5 }, (_, i) => i)  // [0, 1, 2, 3, 4]
Array.of(1, 2, 3)        // [1, 2, 3]
```

### `Object` static methods

```typescript
Object.hasOwn(obj, key)          // true if obj has own property key
Object.keys(obj)                 // array of own enumerable property names
Object.values(obj)               // array of own enumerable property values
Object.entries(obj)              // array of [key, value] pairs
Object.fromEntries(entries)      // build object from entries
Object.assign(target, ...srcs)   // shallow merge
```
