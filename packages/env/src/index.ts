// @symmetra/env
// Type-safe environment variables and config files.
// Parsed once at startup, validated against a schema, then frozen.
// Accessing env vars inside handlers is free — no process.env lookup.

// ── Schema definition ─────────────────────────────────────────────────────────

type EnvType = 'string' | 'number' | 'boolean' | 'url' | 'port' | 'host'

export interface EnvField<T = string> {
  type?:     EnvType
  default?:  T
  required?: boolean          // default: true if no default
  secret?:   boolean          // masked in logs, never serialised
  min?:      number           // for type: 'number' | 'port'
  max?:      number
  pattern?:  RegExp           // for type: 'string' | 'url'
  choices?:  T[]              // allowed values
}

// Helper to define env schema with full type inference
export type EnvSchema = Record<string, EnvField<any>>

export type EnvValues<S extends EnvSchema> = {
  readonly [K in keyof S]:
    S[K]['type'] extends 'number' | 'port' ? number :
    S[K]['type'] extends 'boolean'         ? boolean :
    string
}

// ── Env loader ────────────────────────────────────────────────────────────────

export interface EnvOptions {
  files?: string[]           // .env files to load, in order (default: ['.env'])
  override?: boolean         // file values override process.env (default: false)
  strict?: boolean           // throw on unknown variables (default: false)
}

export interface EnvApi {
  // Load and validate env vars against a schema
  load<S extends EnvSchema>(schema: S, opts?: EnvOptions): EnvValues<S>

  // Get a single variable (no schema)
  get(key: string): string | undefined
  require(key: string): string              // throws if missing
  number(key: string, fallback?: number): number
  bool(key: string, fallback?: boolean): boolean

  // Load a .env file manually
  loadFile(path: string): Record<string, string>
}

// ── Config file loading ───────────────────────────────────────────────────────

export interface ConfigApi {
  // Load JSON config file with type safety
  json<T = Record<string, unknown>>(path: string): T

  // Load TOML config file (no external lib — built-in TOML parser)
  toml<T = Record<string, unknown>>(path: string): T

  // Merge multiple config sources (later sources override earlier)
  merge<T>(...sources: Partial<T>[]): T
}

// ── Usage example (in TypeScript) ─────────────────────────────────────────────
//
// import { env } from '@symmetra/env';
//
// const config = env.load({
//   PORT:        { type: 'port',   default: 8080 },
//   DATABASE_URL: { type: 'url',   required: true },
//   SECRET_KEY:  { type: 'string', required: true, secret: true },
//   DEBUG:       { type: 'boolean', default: false },
//   MAX_CONN:    { type: 'number',  default: 32, min: 1, max: 256 },
// });
//
// // config.PORT        → number
// // config.DATABASE_URL → string
// // config.DEBUG       → boolean

export declare const env:    EnvApi
export declare const config: ConfigApi
