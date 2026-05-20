// @symmetra/kv
// VRAM-resident key-value store. No DRAM round-trip. No Redis.
// All data lives in GPU VRAM for the lifetime of the process.

export type { KvApi } from '@symmetra/core'

// KvStore can be instantiated for a named namespace.
// Each namespace gets its own VRAM segment.
export class KvStore {
  constructor(opts?: {
    namespace?: string   // Default: 'default'
    maxMb?:     number   // VRAM budget. Default: 256
    eviction?:  'lru' | 'lfu' | 'ttl'  // Default: 'lru'
  }) {}

  get<T = unknown>(key: string): Promise<T | null> {
    throw new Error('Use within symmetra runtime')
  }
  set<T = unknown>(key: string, value: T, opts?: { ttlMs?: number }): Promise<void> {
    throw new Error('Use within symmetra runtime')
  }
  del(key: string): Promise<void> {
    throw new Error('Use within symmetra runtime')
  }
  has(key: string): Promise<boolean> {
    throw new Error('Use within symmetra runtime')
  }

  // Return VRAM usage stats
  stats(): Promise<{
    keys:     number
    usedMb:   number
    maxMb:    number
    hitRate:  number
  }> {
    throw new Error('Use within symmetra runtime')
  }
}
