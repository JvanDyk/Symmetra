// @symmetra/crypto
// GPU-accelerated cryptography.
// Hash functions, encryption, signing, and key derivation — all in CUDA.
// CPU is used only for key input and final output transfer.
//
// Hash throughput on RTX 4090:
//   SHA-256:  ~45 GB/s  (vs ~2 GB/s on CPU)
//   BLAKE3:   ~90 GB/s  (GPU parallel merkle tree)
//   AES-256:  ~120 GB/s (AES-NI equivalent in CUDA)

import type { GpuBuffer } from '@symmetra/core'

// ── Hash functions ────────────────────────────────────────────────────────────

export interface HashApi {
  // Single value (string or buffer → hex digest)
  sha256(data: string | Uint8Array | GpuBuffer): Promise<string>
  sha512(data: string | Uint8Array | GpuBuffer): Promise<string>
  blake3(data: string | Uint8Array | GpuBuffer): Promise<string>
  md5(data: string | Uint8Array | GpuBuffer): Promise<string>      // legacy only — not secure

  // Batch hash — single GPU kernel for all inputs
  // Returns an array of hex digests, one per input
  sha256Batch(inputs: string[] | Uint8Array[]): Promise<string[]>
  blake3Batch(inputs: string[] | Uint8Array[]): Promise<string[]>

  // Hash a large GpuBuffer already in VRAM (no H2D copy needed)
  sha256Gpu(buf: GpuBuffer): Promise<string>
  blake3Gpu(buf: GpuBuffer): Promise<string>

  // HMAC
  hmacSha256(key: string | Uint8Array, data: string | Uint8Array): Promise<string>
  hmacSha512(key: string | Uint8Array, data: string | Uint8Array): Promise<string>
}

// ── Symmetric encryption ──────────────────────────────────────────────────────

export interface EncryptResult {
  ciphertext: Uint8Array
  iv:         Uint8Array   // 12 bytes for GCM
  tag:        Uint8Array   // 16 bytes authentication tag
}

export interface CipherApi {
  // AES-256-GCM (authenticated encryption — use this for everything)
  encrypt(
    key: Uint8Array,           // 32 bytes
    plaintext: string | Uint8Array | GpuBuffer,
    aad?: Uint8Array           // additional authenticated data
  ): Promise<EncryptResult>

  decrypt(
    key: Uint8Array,
    result: EncryptResult,
    aad?: Uint8Array
  ): Promise<Uint8Array>

  // ChaCha20-Poly1305 (faster on non-AES-NI hardware — all CUDA)
  chachaEncrypt(key: Uint8Array, plaintext: Uint8Array, nonce?: Uint8Array): Promise<EncryptResult>
  chachaDecrypt(key: Uint8Array, result: EncryptResult): Promise<Uint8Array>

  // Batch encrypt — one kernel call for many plaintexts
  encryptBatch(key: Uint8Array, plaintexts: Uint8Array[]): Promise<EncryptResult[]>
}

// ── Key derivation ────────────────────────────────────────────────────────────

export interface KdfApi {
  // PBKDF2 — password-based key derivation (GPU runs thousands of iterations in parallel)
  pbkdf2(
    password: string | Uint8Array,
    salt: Uint8Array,
    opts?: { iterations?: number; keyLen?: number; hash?: 'SHA-256' | 'SHA-512' }
  ): Promise<Uint8Array>

  // Argon2id — memory-hard KDF (recommended for password hashing)
  argon2(
    password: string | Uint8Array,
    salt: Uint8Array,
    opts?: { memory?: number; iterations?: number; parallelism?: number }
  ): Promise<Uint8Array>

  // HKDF — HMAC-based key derivation (for deriving session keys from a master key)
  hkdf(
    key: Uint8Array,
    info: Uint8Array,
    salt?: Uint8Array,
    len?: number
  ): Promise<Uint8Array>
}

// ── Random number generation ──────────────────────────────────────────────────

export interface RandomApi {
  // Cryptographically secure random bytes (CURAND XORWOW generator)
  bytes(n: number): Uint8Array
  uuid(): string               // RFC 4122 v4 UUID
  hex(n: number): string       // n random hex chars

  // Fill a GpuBuffer with random floats [0, 1) — stays in VRAM
  fillGpu(buf: GpuBuffer<Float32Array>): Promise<void>

  // Gaussian (normal) distribution in VRAM
  gaussianGpu(buf: GpuBuffer<Float32Array>, mean?: number, stddev?: number): Promise<void>
}

// ── JWT / token helpers ───────────────────────────────────────────────────────

export interface JwtApi {
  sign(
    payload: Record<string, unknown>,
    secret: string | Uint8Array,
    opts?: { expiresIn?: number; algorithm?: 'HS256' | 'HS512' }
  ): string

  verify<T = Record<string, unknown>>(
    token: string,
    secret: string | Uint8Array
  ): T                          // throws if invalid or expired

  decode<T = Record<string, unknown>>(token: string): T | null
}

// ── Password hashing ──────────────────────────────────────────────────────────

export interface PasswordApi {
  hash(password: string): Promise<string>      // Argon2id — returns encoded string
  verify(password: string, hash: string): Promise<boolean>
}

// ── Exports ───────────────────────────────────────────────────────────────────

export declare const hash:     HashApi
export declare const cipher:   CipherApi
export declare const kdf:      KdfApi
export declare const random:   RandomApi
export declare const jwt:      JwtApi
export declare const password: PasswordApi
