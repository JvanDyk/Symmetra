// @symmetra/env — typed environment variable loader type definitions

// ── Schema types ──────────────────────────────────────────────────────────────

export type EnvFieldType = 'string' | 'number' | 'boolean' | 'url' | 'email' | 'port';

export interface EnvField {
  type: EnvFieldType;
  required?: boolean;
  default?: string | number | boolean;
  description?: string;
  min?: number;
  max?: number;
  pattern?: RegExp;
  choices?: string[];
}

export type EnvSchema = Record<string, EnvField>;

type InferField<F extends EnvField> =
  F['type'] extends 'number' | 'port' ? number :
  F['type'] extends 'boolean'         ? boolean :
  string;

type InferRequired<F extends EnvField, T> =
  F['required'] extends true ? T :
  F['default'] extends undefined ? T | undefined :
  T;

export type EnvValues<S extends EnvSchema> = {
  [K in keyof S]: InferRequired<S[K], InferField<S[K]>>;
};

// ── Env API ───────────────────────────────────────────────────────────────────

export interface EnvLoadOptions {
  path?: string;
  override?: boolean;
  validate?: boolean;
}

export interface EnvApi {
  load<S extends EnvSchema>(
    schema: S,
    options?: EnvLoadOptions
  ): EnvValues<S>;

  get(key: string): string | undefined;
  get(key: string, defaultValue: string): string;

  getNumber(key: string, defaultValue?: number): number | undefined;
  getBoolean(key: string, defaultValue?: boolean): boolean | undefined;

  require(key: string): string;

  all(): Record<string, string>;
}

// ── Config API ────────────────────────────────────────────────────────────────

export interface ConfigApi {
  loadJson<T>(path: string): Promise<T>;
  loadToml<T>(path: string): Promise<T>;
  watch<T>(path: string, callback: (config: T) => void): () => void;
}

// ── Exports ───────────────────────────────────────────────────────────────────

export declare const env: EnvApi;
export declare const config: ConfigApi;
