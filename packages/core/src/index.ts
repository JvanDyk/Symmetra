// @symmetra/core
// GPU context, buffer types, and compute primitives.
// These types are erased at compile time — the engine resolves them to CUDA calls.

// ── Engine variants ────────────────────────────────────────────────────────────

export type EngineVariant =
  | 'APEX'        // 1024x uint4 vectorized — fastest fingerprinting (~16 GH/s)
  | 'RAYK'        // Rule 60 512x — high-throughput hashing (8.14 GH/s)
  | 'PHI_RAYK'    // Phi-weighted — best for storage/DB cache keys
  | 'RAYK_ADAPTIVE' // Sierpinski detection — URL/path routing
  | 'OMEGA'       // Auto-selects best engine per workload
  | 'RULE60'      // Baseline — minimal overhead, deterministic

// ── GPU buffer type ────────────────────────────────────────────────────────────

// A typed array resident in VRAM. Returned by ctx.body<T>() and gpu.alloc<T>().
// All operations on GpuBuffer run as parallel CUDA kernels.
export type GpuBuffer<T extends TypedArray = Float32Array> = {
  readonly _brand: 'GpuBuffer'
  readonly _type: T
  readonly length: number
  readonly byteLength: number

  // Parallel operations — each compiles to a __global__ kernel
  map<U extends TypedArray>(fn: (x: number, i: number) => number): GpuBuffer<U>
  filter(fn: (x: number, i: number) => boolean): GpuBuffer<T>
  reduce(fn: (acc: number, x: number) => number, init: number): number

  // Transfer back to CPU (use sparingly — defeats the purpose)
  toCpu(): T
}

type TypedArray = Float32Array | Float64Array | Int32Array | Uint32Array |
                  Uint8Array   | Int16Array   | Uint16Array

// ── GPU compute API ────────────────────────────────────────────────────────────

export interface GpuApi {
  // Fingerprint a string or buffer using a TM engine (default: APEX)
  fingerprint(data: string | GpuBuffer, opts?: { engine?: EngineVariant }): Promise<bigint>

  // Run the RAYK engine directly
  rayk(data: string | GpuBuffer, opts?: {
    generations?: number
    engine?: EngineVariant
  }): Promise<bigint>

  // Allocate a typed array in VRAM
  alloc<T extends TypedArray>(length: number): GpuBuffer<T>

  // Upload a CPU array to VRAM (pinned memory path — async H2D)
  upload<T extends TypedArray>(data: T): Promise<GpuBuffer<T>>
}

// The gpu object is injected by the Symmetra runtime — import it from @symmetra/http
export declare const gpu: GpuApi

// ── Symmetra handler context ──────────────────────────────────────────────────

export interface SymmCtx {
  // Request
  readonly method:  string
  readonly path:    string
  readonly query:   Record<string, string>
  readonly params:  Record<string, string>   // path parameters (:id, :name…)
  readonly headers: Record<string, string>

  // Body — returns a GpuBuffer already in VRAM (zero-copy)
  body<T extends TypedArray = Float32Array>(): Promise<GpuBuffer<T>>
  json<T = unknown>(): Promise<T>

  // Response
  status(code: number): this
  json(data: unknown): void
  text(data: string): void
  buffer(data: GpuBuffer): void

  // GPU utilities
  readonly gpu: GpuApi
  readonly kv:  KvApi
}

// ── VRAM key-value store ───────────────────────────────────────────────────────

export interface KvApi {
  get<T = unknown>(key: string): Promise<T | null>
  set<T = unknown>(key: string, value: T, opts?: { ttlMs?: number }): Promise<void>
  del(key: string): Promise<void>
  has(key: string): Promise<boolean>
}

// ── Options ───────────────────────────────────────────────────────────────────

export interface RuntimeOptions {
  port?:    number          // Default: 8080
  streams?: number          // CUDA stream pool size. Default: 32
  engine?:  EngineVariant   // Default TM engine. Default: 'OMEGA'
}
