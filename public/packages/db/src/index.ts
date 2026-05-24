// @symmetra/db — GPU-native database client type definitions

import type { GpuBuffer } from '@symmetra/core';

// ── PostgreSQL ───────────────────────────────────────────────────────────────

export interface PgConfig {
  host: string;
  port?: number;
  database: string;
  user: string;
  password: string;
  ssl?: boolean;
  maxConnections?: number;
  idleTimeoutMs?: number;
}

export interface PgRow {
  [column: string]: string | number | boolean | null | Buffer;
}

export interface PgResult<T = PgRow> {
  rows: T[];
  rowCount: number;
  fields: { name: string; dataTypeId: number }[];
}

export interface PreparedStatement<T = PgRow> {
  execute(...params: unknown[]): Promise<PgResult<T>>;
  executeOne(...params: unknown[]): Promise<T | null>;
  close(): Promise<void>;
}

export interface PgClient {
  query<T = PgRow>(sql: string, params?: unknown[]): Promise<PgResult<T>>;
  queryOne<T = PgRow>(sql: string, params?: unknown[]): Promise<T | null>;
  prepare<T = PgRow>(name: string, sql: string): Promise<PreparedStatement<T>>;
  transaction<T>(fn: (client: PgTransactionClient) => Promise<T>): Promise<T>;
  close(): Promise<void>;
}

export interface PgTransactionClient {
  query<T = PgRow>(sql: string, params?: unknown[]): Promise<PgResult<T>>;
  queryOne<T = PgRow>(sql: string, params?: unknown[]): Promise<T | null>;
  rollback(): Promise<void>;
}

export declare function postgres(config: PgConfig): PgClient;

// ── SQLite ───────────────────────────────────────────────────────────────────

export interface SqliteOptions {
  path: string;
  readOnly?: boolean;
  walMode?: boolean;
}

export interface SqliteResult<T = PgRow> {
  rows: T[];
  rowCount: number;
  lastInsertRowid?: number;
  changes?: number;
}

export interface SqliteClient {
  query<T = PgRow>(sql: string, params?: unknown[]): Promise<SqliteResult<T>>;
  queryOne<T = PgRow>(sql: string, params?: unknown[]): Promise<T | null>;
  execute(sql: string, params?: unknown[]): Promise<SqliteResult>;
  tableToGpu<T extends Float32Array | Int32Array>(
    sql: string,
    params?: unknown[]
  ): Promise<GpuBuffer<T>>;
  close(): Promise<void>;
}

export declare function sqlite(options: SqliteOptions): SqliteClient;

// ── Redis ────────────────────────────────────────────────────────────────────

export interface RedisConfig {
  host?: string;
  port?: number;
  password?: string;
  db?: number;
  tls?: boolean;
  connectTimeoutMs?: number;
}

export interface RedisPipeline {
  get(key: string): this;
  set(key: string, value: string | number, options?: RedisSetOptions): this;
  del(...keys: string[]): this;
  expire(key: string, seconds: number): this;
  incr(key: string): this;
  hset(key: string, field: string, value: string): this;
  hget(key: string, field: string): this;
  exec(): Promise<(string | number | null)[]>;
}

export interface RedisSetOptions {
  ex?: number;
  px?: number;
  nx?: boolean;
  xx?: boolean;
}

export interface RedisClient {
  get(key: string): Promise<string | null>;
  set(key: string, value: string | number, options?: RedisSetOptions): Promise<'OK' | null>;
  del(...keys: string[]): Promise<number>;
  exists(...keys: string[]): Promise<number>;
  expire(key: string, seconds: number): Promise<boolean>;
  ttl(key: string): Promise<number>;
  incr(key: string): Promise<number>;
  incrBy(key: string, by: number): Promise<number>;
  hset(key: string, field: string, value: string): Promise<number>;
  hget(key: string, field: string): Promise<string | null>;
  hgetall(key: string): Promise<Record<string, string> | null>;
  sadd(key: string, ...members: string[]): Promise<number>;
  smembers(key: string): Promise<string[]>;
  sismember(key: string, member: string): Promise<boolean>;
  lpush(key: string, ...values: string[]): Promise<number>;
  rpush(key: string, ...values: string[]): Promise<number>;
  lrange(key: string, start: number, stop: number): Promise<string[]>;
  pipeline(): RedisPipeline;
  close(): Promise<void>;
}

export declare function redis(config?: RedisConfig): RedisClient;
