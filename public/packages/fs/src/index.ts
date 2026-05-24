// @symmetra/fs — GPU-native filesystem type definitions

import type { GpuBuffer } from '@symmetra/core';

export interface FileStat {
  size: number;
  isFile: boolean;
  isDirectory: boolean;
  isSymlink: boolean;
  modifiedAt: Date;
  createdAt: Date;
}

export interface WatchEvent {
  type: 'create' | 'modify' | 'delete' | 'rename';
  path: string;
  previousPath?: string;
}

export interface WatchOptions {
  recursive?: boolean;
  signal?: AbortSignal;
}

export interface ReadOptions {
  encoding?: 'utf8' | 'binary';
  offset?: number;
  length?: number;
}

export interface WriteOptions {
  mode?: number;
  flag?: 'w' | 'wx' | 'a' | 'ax';
}

export interface FsApi {
  read(path: string, options: { encoding: 'utf8' } & ReadOptions): Promise<string>;
  read(path: string, options?: { encoding?: 'binary' } & ReadOptions): Promise<GpuBuffer<Uint8Array>>;
  read(path: string, options?: ReadOptions): Promise<string | GpuBuffer<Uint8Array>>;

  write(path: string, data: string | Uint8Array | GpuBuffer<Uint8Array>, options?: WriteOptions): Promise<void>;

  append(path: string, data: string | Uint8Array): Promise<void>;

  stat(path: string): Promise<FileStat>;

  exists(path: string): Promise<boolean>;

  delete(path: string): Promise<void>;

  rename(from: string, to: string): Promise<void>;

  mkdir(path: string, options?: { recursive?: boolean; mode?: number }): Promise<void>;

  rmdir(path: string, options?: { recursive?: boolean }): Promise<void>;

  readdir(path: string): Promise<{ name: string; type: 'file' | 'dir' | 'symlink' }[]>;

  copy(from: string, to: string): Promise<void>;

  watch(path: string, options?: WatchOptions): AsyncIterable<WatchEvent>;
}

export interface JsonFsApi {
  read<T>(path: string): Promise<T>;
  write<T>(path: string, value: T, options?: { pretty?: boolean }): Promise<void>;
}

export declare const fs: FsApi;
export declare const jsonFs: JsonFsApi;
