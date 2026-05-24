// @symmetra/db
// Raw database protocol client.
// CPU handles the TCP socket. Query results are transferred to VRAM for GPU processing.
// No ORM. No query builder. Raw SQL → GPU buffer.
//
// Supported protocols:
//   PostgreSQL  wire protocol v3 (RFC: https://www.postgresql.org/docs/current/protocol.html)
//   SQLite      via memory-mapped file + WAL mode
//   Redis       RESP3 protocol (raw TCP)

import type { GpuBuffer } from '@symmetra/core'

// ── Connection config ──────────────────────────────────────────────────────────

export interface PgConfig {
  host:       string
  port?:      number          // default 5432
  database:   string
  user:       string
  password:   string
  ssl?:       boolean         // TLS (recommended in production)
  poolSize?:  number          // connection pool (default: 8)
  timeoutMs?: number
}

export interface SqliteConfig {
  path:    string             // ':memory:' for in-memory
  walMode?: boolean           // WAL mode for concurrent reads (default: true)
  cacheKb?: number            // SQLite page cache in KB (default: 4096)
}

export interface RedisConfig {
  host:      string
  port?:     number           // default 6379
  password?: string
  db?:       number           // database index (default: 0)
  poolSize?: number
}

// ── Row types ─────────────────────────────────────────────────────────────────

export type Row = Record<string, unknown>

export interface QueryResult<T extends Row = Row> {
  rows:         T[]
  rowCount:     number
  fields:       FieldDesc[]
  // Transfer result rows to VRAM as a typed array (for GPU processing)
  toGpu<B extends ArrayBuffer = Float32Array>(): Promise<GpuBuffer<any>>
}

export interface FieldDesc {
  name:     string
  dataType: number            // PostgreSQL OID or SQLite type affinity
}

// ── PostgreSQL client ─────────────────────────────────────────────────────────

export interface PgClient {
  // Execute a query. Parameterized — safe against SQL injection.
  // $1, $2, ... placeholders.
  query<T extends Row = Row>(sql: string, params?: unknown[]): Promise<QueryResult<T>>

  // Execute without returning rows (INSERT, UPDATE, DELETE)
  exec(sql: string, params?: unknown[]): Promise<{ rowsAffected: number }>

  // Transaction block — auto-rollback on throw
  transaction<T>(fn: (tx: PgTx) => Promise<T>): Promise<T>

  // Prepared statement (compiled once, executed many times)
  prepare(name: string, sql: string): PreparedStatement

  // Copy large datasets to VRAM in streaming chunks
  stream<T extends Row = Row>(
    sql: string,
    params?: unknown[],
    chunkSize?: number
  ): AsyncIterable<T[]>

  // Low-level: send raw protocol bytes (for advanced use)
  raw(msg: Uint8Array): Promise<Uint8Array>

  close(): Promise<void>
}

export interface PgTx {
  query<T extends Row = Row>(sql: string, params?: unknown[]): Promise<QueryResult<T>>
  exec(sql: string, params?: unknown[]): Promise<{ rowsAffected: number }>
  savepoint(name: string): Promise<void>
  rollbackTo(name: string): Promise<void>
}

export interface PreparedStatement {
  execute<T extends Row = Row>(params?: unknown[]): Promise<QueryResult<T>>
  close(): Promise<void>
}

// ── SQLite client ─────────────────────────────────────────────────────────────

export interface SqliteClient {
  query<T extends Row = Row>(sql: string, params?: unknown[]): Promise<QueryResult<T>>
  exec(sql: string, params?: unknown[]): Promise<{ rowsAffected: number; lastInsertId: number }>
  transaction<T>(fn: (tx: SqliteTx) => Promise<T>): Promise<T>
  // Dump entire table to VRAM as a typed array for GPU batch processing
  tableToGpu(table: string, column: string): Promise<GpuBuffer<Float32Array>>
  close(): Promise<void>
}

export interface SqliteTx {
  query<T extends Row = Row>(sql: string, params?: unknown[]): Promise<QueryResult<T>>
  exec(sql: string, params?: unknown[]): Promise<{ rowsAffected: number }>
}

// ── Redis client ──────────────────────────────────────────────────────────────

export interface RedisClient {
  get(key: string): Promise<string | null>
  set(key: string, value: string, opts?: { ex?: number; nx?: boolean }): Promise<boolean>
  del(...keys: string[]): Promise<number>
  exists(...keys: string[]): Promise<number>
  incr(key: string): Promise<number>
  hget(key: string, field: string): Promise<string | null>
  hset(key: string, fields: Record<string, string>): Promise<number>
  hgetall(key: string): Promise<Record<string, string> | null>
  lpush(key: string, ...values: string[]): Promise<number>
  rpop(key: string): Promise<string | null>
  publish(channel: string, message: string): Promise<number>
  subscribe(channel: string, handler: (message: string) => void): Promise<() => void>
  pipeline(): RedisPipeline
  close(): Promise<void>
}

export interface RedisPipeline {
  get(key: string): this
  set(key: string, value: string): this
  del(key: string): this
  exec(): Promise<unknown[]>
}

// ── Factory functions ──────────────────────────────────────────────────────────

export declare function postgres(config: PgConfig): Promise<PgClient>
export declare function sqlite(config: SqliteConfig): Promise<SqliteClient>
export declare function redis(config: RedisConfig): Promise<RedisClient>
