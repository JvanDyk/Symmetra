// @symmetra/fs
// GPU-accelerated file I/O.
// Large files are memory-mapped on CPU then transferred to VRAM via pinned memory
// (DMA — GPU's memory controller copies at full PCIe bandwidth, no CPU involvement).
// Small files (<64 KB) are read synchronously; large files stream in chunks.

export interface ReadOptions {
  encoding?: 'binary' | 'utf8'    // binary = GpuBuffer, utf8 = string (CPU)
  offset?:   number                // byte offset into file
  length?:   number                // bytes to read (default: entire file)
}

export interface WriteOptions {
  append?: boolean                 // append to existing file
  mode?:   number                  // Unix file permissions (default: 0o644)
  flush?:  boolean                 // fsync after write
}

export interface FileStats {
  size:     number                 // bytes
  created:  Date
  modified: Date
  isFile:   boolean
  isDir:    boolean
}

import type { GpuBuffer } from '@symmetra/core'

export interface FsApi {
  // Read entire file into VRAM as a GpuBuffer (best for large binary files)
  read(path: string, opts?: ReadOptions & { encoding: 'binary' }): Promise<GpuBuffer<Uint8Array>>

  // Read file as UTF-8 string (CPU-side, for small config/text files)
  read(path: string, opts?: ReadOptions & { encoding: 'utf8' }): Promise<string>
  read(path: string, opts?: ReadOptions): Promise<GpuBuffer<Uint8Array> | string>

  // Write a GpuBuffer from VRAM to disk (async D2H DMA then write)
  write(path: string, data: GpuBuffer | string | Uint8Array, opts?: WriteOptions): Promise<void>

  // Append bytes to a file
  append(path: string, data: string | Uint8Array): Promise<void>

  // File metadata (no GPU involved — pure OS call)
  stat(path: string): Promise<FileStats>
  exists(path: string): Promise<boolean>

  // Directory operations
  mkdir(path: string, opts?: { recursive?: boolean }): Promise<void>
  readdir(path: string): Promise<string[]>
  unlink(path: string): Promise<void>
  rename(from: string, to: string): Promise<void>

  // Watch a file or directory for changes
  watch(path: string, handler: (event: 'change' | 'rename', filename: string) => void): Watcher
}

export interface Watcher {
  close(): void
}

// JSON helpers — read/write JSON files with type safety
export interface JsonFs {
  read<T = unknown>(path: string): Promise<T>
  write<T = unknown>(path: string, data: T, opts?: { pretty?: boolean }): Promise<void>
}

export declare const fs: FsApi
export declare const jsonFs: JsonFs
