// @symmetra/core
// GPU context, buffer types, and compute primitives for Symmetra.
// These types are resolved by the Symmetra compiler — no JavaScript executes this code.

// ── Typed arrays ────────────────────────────────────────────────────────────

type TypedArray = Float32Array | Float64Array | Int32Array | Uint32Array |
                  Uint8Array   | Int16Array   | Uint16Array

// ── GPU buffer ────────────────────────────────────────────────────────────────
// A typed array resident in VRAM. All operations compile to parallel CUDA kernels.

export type GpuBuffer<T extends TypedArray = Float32Array> = {
  readonly _brand: 'GpuBuffer'
  readonly _type:  T
  readonly length:     number
  readonly byteLength: number

  // Parallel operations — each compiles to a __global__ kernel
  map<U extends TypedArray>(fn: (x: number, i: number) => number): GpuBuffer<U>
  filter(fn: (x: number, i: number) => boolean): GpuBuffer<T>
  reduce(fn: (acc: number, x: number) => number, init: number): number
  forEach(fn: (x: number, i: number) => void): void

  // Slice / copy — zero-copy view when possible
  slice(start?: number, end?: number): GpuBuffer<T>

  // Transfer back to CPU — avoid in hot paths
  toCpu(): T
  toArray(): number[]
}

// ── GpuMap — VRAM-resident hash map ──────────────────────────────────────────
// Compiles from standard JS Map<K, V>. Uses open-addressing on GPU.

export interface GpuMap<K, V> {
  get(key: K): V | undefined
  set(key: K, value: V): this
  has(key: K): boolean
  delete(key: K): boolean
  clear(): void
  readonly size: number
  forEach(fn: (value: V, key: K, map: GpuMap<K, V>) => void): void
  keys(): IterableIterator<K>
  values(): IterableIterator<V>
  entries(): IterableIterator<[K, V]>
  [Symbol.iterator](): IterableIterator<[K, V]>
}

export declare function GpuMapOf<K, V>(entries?: readonly [K, V][]): GpuMap<K, V>

// ── GpuSet — VRAM-resident hash set ──────────────────────────────────────────
// Compiles from standard JS Set<T>.

export interface GpuSet<T> {
  add(value: T): this
  has(value: T): boolean
  delete(value: T): boolean
  clear(): void
  readonly size: number
  forEach(fn: (value: T, value2: T, set: GpuSet<T>) => void): void
  values(): IterableIterator<T>
  keys(): IterableIterator<T>
  entries(): IterableIterator<[T, T]>
  [Symbol.iterator](): IterableIterator<T>
}

export declare function GpuSetOf<T>(values?: readonly T[]): GpuSet<T>

// ── GpuRegExp — GPU DFA compiled at kernel compile time ───────────────────────
// Standard RegExp literal syntax compiles to a GPU DFA. Zero runtime JIT cost.

export interface GpuRegExp {
  test(str: string): boolean
  exec(str: string): RegExpExecArray | null
  readonly source: string
  readonly flags:  string
  readonly global: boolean
  readonly ignoreCase: boolean
  readonly multiline:  boolean
}

// ── GpuEventEmitter — VRAM-resident event dispatch ────────────────────────────
// Compiled from Node.js EventEmitter or bare on/emit patterns.

export interface GpuEventEmitter<Events extends Record<string, unknown[]> = Record<string, unknown[]>> {
  on<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  once<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  off<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  emit<K extends keyof Events>(event: K, ...args: Events[K]): boolean
  removeAllListeners(event?: keyof Events): this
  listenerCount(event: keyof Events): number
}

export declare class EventEmitter<
  Events extends Record<string, unknown[]> = Record<string, unknown[]>
> implements GpuEventEmitter<Events> {
  on<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  once<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  off<K extends keyof Events>(event: K, listener: (...args: Events[K]) => void): this
  emit<K extends keyof Events>(event: K, ...args: Events[K]): boolean
  removeAllListeners(event?: keyof Events): this
  listenerCount(event: keyof Events): number
}

// ── GpuPromise utilities ──────────────────────────────────────────────────────
// Promise.all / race / any compile to parallel CUDA stream dispatch.
// These augment the standard Promise global — no import needed.

export interface GpuPromiseConstructor {
  all<T extends readonly unknown[]>(values: [...{ [K in keyof T]: Promise<T[K]> }]): Promise<T>
  race<T>(values: Iterable<Promise<T>>): Promise<T>
  any<T>(values: Iterable<Promise<T>>): Promise<T>
  allSettled<T extends readonly unknown[]>(
    values: [...{ [K in keyof T]: Promise<T[K]> }]
  ): Promise<{ [K in keyof T]: PromiseSettledResult<T[K]> }>
  resolve<T>(value: T): Promise<T>
  reject(reason?: unknown): Promise<never>
}

// ── GPU compute API ───────────────────────────────────────────────────────────

export interface GpuApi {
  // Hash a string or buffer entirely on GPU — no CPU round-trip
  fingerprint(data: string | GpuBuffer): Promise<bigint>

  // Allocate a typed array in VRAM
  alloc<T extends TypedArray>(length: number): GpuBuffer<T>

  // Upload a CPU array to VRAM via pinned memory (async DMA transfer)
  upload<T extends TypedArray>(data: T): Promise<GpuBuffer<T>>

  // Execute a raw parallel kernel across n elements
  parallel(n: number, fn: (tid: number) => void): Promise<void>

  // System info
  readonly deviceName:   string
  readonly computeSm:    string
  readonly vramTotal:    number
  readonly vramFree:     number
  readonly streamCount:  number
}

export declare const gpu: GpuApi

// ── Handler context ───────────────────────────────────────────────────────────

export interface SymmCtx {
  // Request
  readonly method:  string
  readonly path:    string
  readonly query:   Record<string, string>
  readonly params:  Record<string, string>
  readonly headers: Record<string, string>

  // Single accessor — returns the raw header value
  header(name: string): string | undefined

  // Body — returns a GpuBuffer already in VRAM (zero-copy)
  body<T extends TypedArray = Float32Array>(): Promise<GpuBuffer<T>>
  json<T = unknown>(): Promise<T>
  text(): Promise<string>

  // Response
  status(code: number): this
  json(data: unknown): void
  text(data: string): void
  send(data: GpuBuffer | Uint8Array): void
  header(name: string, value: string): this

  // Utilities
  readonly gpu: GpuApi
  readonly kv:  KvApi
}

// ── VRAM key-value store ──────────────────────────────────────────────────────

export interface KvApi {
  get<T = unknown>(key: string): Promise<T | null>
  set<T = unknown>(key: string, value: T, opts?: { ttlMs?: number }): Promise<void>
  del(key: string): Promise<void>
  has(key: string): Promise<boolean>
  keys(pattern?: string): Promise<string[]>
}

// ── GPU console ───────────────────────────────────────────────────────────────
// console.log / warn / error write to a GPU-side ring buffer in VRAM.
// The runtime flushes it to stdout after each request. No CPU involvement in the hot path.
// Standard global — no import needed.

export interface GpuConsole {
  log(...args: unknown[]): void
  warn(...args: unknown[]): void
  error(...args: unknown[]): void
  info(...args: unknown[]): void
  debug(...args: unknown[]): void
  time(label?: string): void
  timeEnd(label?: string): void
  assert(condition: boolean, ...args: unknown[]): void
}

// ── Workers ───────────────────────────────────────────────────────────────────
// GpuWorker maps to the Node.js worker_threads.Worker API.
// Each worker gets its own CUDA stream and VRAM slab — fully isolated.
// Communication is via postMessage / onmessage (structured clone via GPU JSON).

export interface WorkerOptions {
  // Private VRAM heap for this worker. Default: 4 MB.
  heapBytes?: number
  // Allow the worker to receive GpuBuffer pointers from the parent (zero-copy transfer).
  shareGpuMemory?: boolean
  // Diagnostic name shown in verbose output.
  name?: string
}

export interface MessageEvent<T = unknown> {
  readonly data: T
  readonly type: 'message' | 'error' | 'exit'
}

export interface WorkerPort {
  postMessage(data: unknown, transfer?: GpuBuffer[]): void
  onmessage: ((event: MessageEvent) => void) | null
  close(): void
}

export declare class GpuWorker {
  constructor(scriptPath: string, options?: WorkerOptions)

  // Send structured data (or a GpuBuffer) to the worker.
  // Transferred GpuBuffers are zero-copy — ownership moves to the worker.
  postMessage(data: unknown, transfer?: GpuBuffer[]): void

  // Register a callback for messages received from the worker.
  onmessage: ((event: MessageEvent) => void) | null

  // Register a callback for worker errors.
  onerror: ((event: MessageEvent<Error>) => void) | null

  // Wait for the worker to send one message, then resolve.
  nextMessage<T = unknown>(): Promise<T>

  // Terminate the worker immediately.
  terminate(): void

  readonly running: boolean
}

// ── MessageChannel — pair of connected ports ──────────────────────────────────

export declare class MessageChannel {
  readonly port1: WorkerPort
  readonly port2: WorkerPort
}

// ── SharedArrayBuffer + Atomics — shared VRAM ─────────────────────────────────
// SharedGpuBuffer is a VRAM region accessible from both parent and worker streams.
// Atomics operations compile to CUDA atomic intrinsics (atomicAdd, atomicCAS, etc.)
// — no CPU involved, no memory bus crossing.

export interface SharedGpuBuffer<T extends TypedArray = Int32Array> {
  readonly _brand:      'SharedGpuBuffer'
  readonly _type:       T
  readonly byteLength:  number
}

export declare function sharedBuffer<T extends TypedArray>(byteLength: number): SharedGpuBuffer<T>

// Matches the standard Atomics API; compiles to CUDA atomic device functions.
export declare const Atomics: {
  add(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  sub(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  and(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  or (buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  xor(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  load(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number): number
  store(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
  compareExchange(
    buffer: SharedGpuBuffer<Int32Array | Uint32Array>,
    index:  number,
    expected: number,
    replacement: number
  ): number
  exchange(buffer: SharedGpuBuffer<Int32Array | Uint32Array>, index: number, value: number): number
}

// ── Scheduler / threading options ─────────────────────────────────────────────

export interface SchedulerOptions {
  // Number of CUDA streams in the stream pool.
  // Each stream processes one request simultaneously.
  // Default: 32. Maximum: 64.
  streams?: number

  // CPU thread pool size for TCP accept/recv/send.
  // Default: hardware_concurrency() (one per CPU core).
  ioThreads?: number

  // VRAM slab size per request arena (bytes). Default: 4MB.
  // Increase for handlers that allocate large intermediate arrays.
  requestSlabBytes?: number

  // Persistent VRAM heap size (bytes). Default: 512MB.
  // Used for KV store, compiled eval kernels, and cross-request caches.
  heapBytes?: number
}

// ── Streams ───────────────────────────────────────────────────────────────────
// Node.js-compatible Readable / Writable / Transform / pipeline.
// Each stage maps to a VRAM ring buffer; data never leaves GPU memory between stages.

export interface ReadableOptions {
  highWaterMark?: number   // VRAM ring buffer capacity in chunks. Default: 8.
}

export interface WritableOptions {
  highWaterMark?: number
}

export interface GpuReadable {
  pipe<T extends GpuWritable>(dest: T, opts?: { end?: boolean }): T
  on(event: 'data',  listener: (chunk: GpuBuffer<Uint8Array>) => void): this
  on(event: 'end',   listener: () => void): this
  on(event: 'error', listener: (err: Error) => void): this
  destroy(err?: Error): this
  readonly readable: boolean
}

export interface GpuWritable {
  write(chunk: GpuBuffer<Uint8Array> | Uint8Array | string): boolean
  end(chunk?: GpuBuffer<Uint8Array> | Uint8Array | string): this
  on(event: 'finish', listener: () => void): this
  on(event: 'error',  listener: (err: Error) => void): this
  destroy(err?: Error): this
  readonly writable: boolean
}

export type GpuTransform = GpuReadable & GpuWritable

export declare class Readable implements GpuReadable {
  constructor(opts?: ReadableOptions)
  pipe<T extends GpuWritable>(dest: T, opts?: { end?: boolean }): T
  on(event: 'data',  listener: (chunk: GpuBuffer<Uint8Array>) => void): this
  on(event: 'end',   listener: () => void): this
  on(event: 'error', listener: (err: Error) => void): this
  destroy(err?: Error): this
  readonly readable: boolean

  // Factory: wrap a file path in a VRAM-backed readable stream.
  static from(path: string, opts?: { chunkSize?: number }): Readable
}

export declare class Writable implements GpuWritable {
  constructor(opts?: WritableOptions)
  write(chunk: GpuBuffer<Uint8Array> | Uint8Array | string): boolean
  end(chunk?: GpuBuffer<Uint8Array> | Uint8Array | string): this
  on(event: 'finish', listener: () => void): this
  on(event: 'error',  listener: (err: Error) => void): this
  destroy(err?: Error): this
  readonly writable: boolean

  // Factory: wrap a file path in a VRAM-backed writable stream.
  static toFile(path: string): Writable
}

export declare class Transform implements GpuTransform {
  constructor(opts?: ReadableOptions & WritableOptions)
  pipe<T extends GpuWritable>(dest: T, opts?: { end?: boolean }): T
  write(chunk: GpuBuffer<Uint8Array> | Uint8Array | string): boolean
  end(chunk?: GpuBuffer<Uint8Array> | Uint8Array | string): this
  on(event: 'data',   listener: (chunk: GpuBuffer<Uint8Array>) => void): this
  on(event: 'end',    listener: () => void): this
  on(event: 'finish', listener: () => void): this
  on(event: 'error',  listener: (err: Error) => void): this
  destroy(err?: Error): this
  readonly readable: boolean
  readonly writable: boolean
}

// pipeline(): connects source → [transforms] → dest.
// Returns a Promise that resolves when all data has been flushed.
// Equivalent to Node.js stream/promises pipeline().
export declare function pipeline(
  source:     GpuReadable,
  ...stages:  [...GpuTransform[], GpuWritable]
): Promise<void>

// ── Timers ────────────────────────────────────────────────────────────────────
// CPU min-heap fires callbacks onto a dedicated GPU timer stream.
// Standard globals — no import needed.

export type TimerId = number & { readonly _brand: 'TimerId' }

export declare function setTimeout(fn: () => void, delayMs: number): TimerId
export declare function setInterval(fn: () => void, intervalMs: number): TimerId
export declare function clearTimeout(id: TimerId): void
export declare function clearInterval(id: TimerId): void

// ── AbortController / AbortSignal ────────────────────────────────────────────
// AbortSignal compiles to a VRAM uint8_t flag.
// controller.abort() calls cuMemsetD8 — instant, no kernel launch.
// Kernels check the flag at yield points; @symmetra/net fetch passes it as a
// device pointer into the GPU network kernel.

export interface AbortSignal {
  readonly aborted: boolean
  addEventListener(type: 'abort', listener: () => void): void
  removeEventListener(type: 'abort', listener: () => void): void
}

export declare class AbortController {
  constructor()
  readonly signal: AbortSignal
  abort(): void
}

// ── URL / URLSearchParams ─────────────────────────────────────────────────────
// CPU-side WHATWG URL parser (not a hot path). Parsed fields are uploaded to
// the request VRAM slab via the standard SymmCtx upload path.

export declare class URL {
  constructor(url: string, base?: string | URL)

  href:     string
  protocol: string
  username: string
  password: string
  hostname: string
  port:     string
  host:     string
  origin:   string
  pathname: string
  search:   string
  hash:     string

  readonly searchParams: URLSearchParams

  toString(): string
  toJSON():   string
}

export declare class URLSearchParams {
  constructor(init?: string | Record<string, string> | Iterable<[string, string]>)

  get(name: string): string | null
  getAll(name: string): string[]
  has(name: string): boolean
  set(name: string, value: string): void
  append(name: string, value: string): void
  delete(name: string): void
  toString(): string

  keys():    IterableIterator<string>
  values():  IterableIterator<string>
  entries(): IterableIterator<[string, string]>
  forEach(fn: (value: string, key: string, parent: URLSearchParams) => void): void
  [Symbol.iterator](): IterableIterator<[string, string]>

  readonly size: number
}

// ── TextEncoder / TextDecoder ─────────────────────────────────────────────────
// encode() launches symm_text_encode — one GPU thread per code point.
// decode() launches symm_text_decode — one GPU thread per UTF-8 start byte.
// Both use a two-pass prefix-sum so output length is determined on-device.

export declare class TextEncoder {
  readonly encoding: 'utf-8'

  // Returns a GpuBuffer<Uint8Array> in VRAM — no CPU round-trip.
  encode(input: string): GpuBuffer<Uint8Array>

  // Encodes into an existing VRAM buffer; returns { read, written }.
  encodeInto(input: string, dest: GpuBuffer<Uint8Array>): { read: number; written: number }
}

export declare class TextDecoder {
  constructor(label?: string, opts?: { fatal?: boolean; ignoreBOM?: boolean })
  readonly encoding: string
  readonly fatal:     boolean
  readonly ignoreBOM: boolean

  // Decodes a VRAM buffer of UTF-8 bytes back to a JS string.
  decode(input?: GpuBuffer<Uint8Array> | Uint8Array, opts?: { stream?: boolean }): string
}

// ── Runtime options ───────────────────────────────────────────────────────────

export interface RuntimeOptions {
  port?:         number    // Default: 8080
  logLevel?:     'debug' | 'info' | 'warn' | 'error'
  maxBodyBytes?: number
  scheduler?:    SchedulerOptions
}
