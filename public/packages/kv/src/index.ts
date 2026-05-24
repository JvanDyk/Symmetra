// @symmetra/kv
// VRAM key-value store for Symmetra.
// Data lives in GPU memory — no Redis, no DRAM round-trip, no serialization.
// All operations compile to GPU device calls.

import type { KvApi } from '@symmetra/core'

export type { KvApi } from '@symmetra/core'

// ── KvStore ────────────────────────────────────────────────────────────────────

export interface KvStoreOptions {
  maxMb?:     number   // VRAM budget in megabytes. Default: 256
  namespace?: string   // Key namespace prefix. Default: ''
}

// KvStore is resolved by the Symmetra engine at compile time.
// Instantiate it at module scope — it maps to a VRAM allocation.
export class KvStore implements KvApi {
  constructor(opts?: KvStoreOptions) {}

  get<T = unknown>(key: string): Promise<T | null>                           { return Promise.resolve(null) }
  set<T = unknown>(key: string, value: T, opts?: { ttlMs?: number }): Promise<void> { return Promise.resolve() }
  del(key: string): Promise<void>                                             { return Promise.resolve() }
  has(key: string): Promise<boolean>                                          { return Promise.resolve(false) }

  // Cache statistics (hit rate, evictions, current size)
  stats(): Promise<{ hits: number; misses: number; evictions: number; sizeBytes: number }> {
    return Promise.resolve({ hits: 0, misses: 0, evictions: 0, sizeBytes: 0 })
  }
}
