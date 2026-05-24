// @symmetra/crypto — GPU-native cryptography type definitions

import type { GpuBuffer } from '@symmetra/core';

// ── Hashing ──────────────────────────────────────────────────────────────────

export type HashAlgorithm = 'sha256' | 'sha512' | 'blake3';

export interface HashApi {
  hash(algorithm: HashAlgorithm, data: string | Uint8Array | GpuBuffer<Uint8Array>): Promise<string>;

  hmac(
    algorithm: HashAlgorithm,
    key: string | Uint8Array,
    data: string | Uint8Array
  ): Promise<string>;

  batchHash(
    algorithm: HashAlgorithm,
    items: GpuBuffer<Uint8Array>[]
  ): Promise<GpuBuffer<Uint8Array>>;

  compare(a: string | Uint8Array, b: string | Uint8Array): boolean;
}

// ── Symmetric encryption ─────────────────────────────────────────────────────

export type CipherAlgorithm = 'aes-256-gcm' | 'chacha20-poly1305';

export interface EncryptResult {
  ciphertext: Uint8Array;
  iv: Uint8Array;
  tag: Uint8Array;
}

export interface CipherApi {
  encrypt(
    algorithm: CipherAlgorithm,
    key: string | Uint8Array,
    plaintext: string | Uint8Array | GpuBuffer<Uint8Array>
  ): Promise<EncryptResult>;

  decrypt(
    algorithm: CipherAlgorithm,
    key: string | Uint8Array,
    ciphertext: Uint8Array,
    iv: Uint8Array,
    tag: Uint8Array
  ): Promise<Uint8Array>;

  encryptString(
    algorithm: CipherAlgorithm,
    key: string | Uint8Array,
    plaintext: string
  ): Promise<string>;

  decryptString(
    algorithm: CipherAlgorithm,
    key: string | Uint8Array,
    encoded: string
  ): Promise<string>;
}

// ── Key derivation ────────────────────────────────────────────────────────────

export interface Pbkdf2Options {
  iterations?: number;
  keyLength?: number;
  algorithm?: 'sha256' | 'sha512';
}

export interface Argon2Options {
  memoryCost?: number;
  timeCost?: number;
  parallelism?: number;
  keyLength?: number;
}

export interface HkdfOptions {
  algorithm?: 'sha256' | 'sha512';
  info?: string | Uint8Array;
  keyLength?: number;
}

export interface KdfApi {
  pbkdf2(password: string, salt: string | Uint8Array, options?: Pbkdf2Options): Promise<string>;
  argon2id(password: string, salt?: string, options?: Argon2Options): Promise<string>;
  hkdf(
    inputKey: string | Uint8Array,
    salt: string | Uint8Array,
    options?: HkdfOptions
  ): Promise<Uint8Array>;
}

// ── Secure random ─────────────────────────────────────────────────────────────

export interface RandomApi {
  bytes(length: number): Uint8Array;
  hex(byteLength?: number): string;
  uuid(): string;
  int(min?: number, max?: number): number;
}

// ── JWT ───────────────────────────────────────────────────────────────────────

export type JwtAlgorithm = 'HS256' | 'HS512' | 'RS256' | 'ES256';

export interface JwtOptions {
  algorithm?: JwtAlgorithm;
  expiresIn?: number;
  issuer?: string;
  audience?: string;
}

export interface JwtPayload {
  sub?: string;
  iss?: string;
  aud?: string | string[];
  exp?: number;
  iat?: number;
  [claim: string]: unknown;
}

export interface JwtApi {
  sign(payload: JwtPayload, secret: string, options?: JwtOptions): Promise<string>;
  verify<T extends JwtPayload = JwtPayload>(token: string, secret: string): Promise<T>;
  decode<T extends JwtPayload = JwtPayload>(token: string): T | null;
}

// ── Password ──────────────────────────────────────────────────────────────────

export interface PasswordApi {
  hash(password: string): Promise<string>;
  verify(password: string, hash: string): Promise<boolean>;
}

// ── Exports ───────────────────────────────────────────────────────────────────

export declare const hash: HashApi;
export declare const cipher: CipherApi;
export declare const kdf: KdfApi;
export declare const random: RandomApi;
export declare const jwt: JwtApi;
export declare const password: PasswordApi;
