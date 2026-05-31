// Local cache manager for SPM

export interface CacheEntry {
  key: string;
  data: any;
  timestamp: number;
  ttl?: number;  // Time to live in milliseconds
  integrity?: string;
}

/**
 * In-memory cache for SPM operations
 * In production, this would also persist to disk in .spm/cache/
 */
export class SpmCache {
  private cache: Map<string, CacheEntry> = new Map();
  private cacheDir: string;

  constructor(cacheDir: string = '.spm/cache') {
    this.cacheDir = cacheDir;
  }

  /**
   * Set a value in cache
   */
  set(key: string, data: any, ttl?: number, integrity?: string): void {
    this.cache.set(key, {
      key,
      data,
      timestamp: Date.now(),
      ttl,
      integrity
    });
  }

  /**
   * Get a value from cache
   */
  get(key: string): any | undefined {
    const entry = this.cache.get(key);
    if (!entry) return undefined;

    // Check if expired
    if (entry.ttl && Date.now() - entry.timestamp > entry.ttl) {
      this.cache.delete(key);
      return undefined;
    }

    return entry.data;
  }

  /**
   * Check if key exists in cache
   */
  has(key: string): boolean {
    const entry = this.cache.get(key);
    if (!entry) return false;

    // Check if expired
    if (entry.ttl && Date.now() - entry.timestamp > entry.ttl) {
      this.cache.delete(key);
      return false;
    }

    return true;
  }

  /**
   * Delete a cache entry
   */
  delete(key: string): boolean {
    return this.cache.delete(key);
  }

  /**
   * Clear entire cache
   */
  clear(): void {
    this.cache.clear();
  }

  /**
   * Get cache statistics
   */
  stats(): {
    size: number;
    keys: string[];
    totalBytes: number;
  } {
    let totalBytes = 0;
    const keys: string[] = [];

    for (const entry of this.cache.values()) {
      keys.push(entry.key);
      if (typeof entry.data === 'string') {
        totalBytes += entry.data.length;
      } else if (entry.data instanceof Uint8Array) {
        totalBytes += entry.data.byteLength;
      } else {
        totalBytes += JSON.stringify(entry.data).length;
      }
    }

    return {
      size: this.cache.size,
      keys,
      totalBytes
    };
  }

  /**
   * Cache key generators
   */
  static keys = {
    packageMetadata: (name: string) => `pkg:metadata:${name}`,
    packageVersion: (name: string, version: string) => `pkg:version:${name}@${version}`,
    packageTarball: (name: string, version: string) => `pkg:tarball:${name}@${version}`,
    transformed: (name: string, version: string) => `pkg:transformed:${name}@${version}`,
    lockfile: (hash?: string) => `lockfile:${hash || 'default'}`,
    compileInfo: (name: string, version: string) => `compile:${name}@${version}`
  };
}

/**
 * Cache with disk persistence (simulated)
 */
export class PersistentCache extends SpmCache {
  /**
   * Write cache to disk (simulated - would use fs in real implementation)
   */
  async save(): Promise<void> {
    // In production: write .spm/cache/index.json with cache entries
    // For now, just log that it would save
  }

  /**
   * Load cache from disk (simulated)
   */
  async load(): Promise<void> {
    // In production: read .spm/cache/index.json and restore entries
    // For now, just log that it would load
  }

  /**
   * Clean old entries (older than TTL)
   */
  prune(): number {
    const before = this.cache.size;
    const now = Date.now();

    for (const [key, entry] of this.cache.entries()) {
      if (entry.ttl && now - entry.timestamp > entry.ttl) {
        this.cache.delete(key);
      }
    }

    return before - this.cache.size;
  }

  /**
   * Clean entire cache directory
   */
  async cleanAll(): Promise<void> {
    this.clear();
    // In production: rm -rf .spm/cache/
  }
}

/**
 * Create a cache instance
 */
export function createCache(persistent: boolean = true, cacheDir?: string): SpmCache {
  if (persistent) {
    return new PersistentCache(cacheDir);
  }
  return new SpmCache(cacheDir);
}

export default { SpmCache, PersistentCache, createCache };
