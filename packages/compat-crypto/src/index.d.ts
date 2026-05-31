// Type definitions for Node.js crypto compatibility layer

export type BufferEncoding = 'ascii' | 'utf8' | 'utf-8' | 'utf16le' | 'ucs2' | 'ucs-2' | 'base64' | 'latin1' | 'binary' | 'hex';

export interface Hash {
  update(data: string | Uint8Array, encoding?: BufferEncoding): Hash;
  digest(encoding?: BufferEncoding): string | Uint8Array;
  copy(): Hash;
}

export function createHash(algorithm: string): Hash;

export interface Hmac {
  update(data: string | Uint8Array): Hmac;
  digest(encoding?: BufferEncoding): string | Uint8Array;
}

export function createHmac(algorithm: string, key: string | Uint8Array): Hmac;

export interface Cipher {
  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string;
  final(outputEncoding?: BufferEncoding): string;
  setAutoPadding(autoPadding?: boolean): Cipher;
}

export function createCipher(algorithm: string, password: string | Uint8Array): Cipher;
export function createCipheriv(algorithm: string, key: string | Uint8Array, iv: string | Uint8Array): Cipher;

export interface Decipher {
  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string;
  final(outputEncoding?: BufferEncoding): string;
  setAutoPadding(autoPadding?: boolean): Decipher;
}

export function createDecipher(algorithm: string, password: string | Uint8Array): Decipher;
export function createDecipheriv(algorithm: string, key: string | Uint8Array, iv: string | Uint8Array): Decipher;

// Random functions
export function randomBytes(size: number): Uint8Array;
export function randomBytes(size: number, callback: (err: Error | null, buf: Uint8Array) => void): Uint8Array;

export function randomFillSync(buffer: Uint8Array, offset?: number, size?: number): Uint8Array;

export function randomFill(
  buffer: Uint8Array,
  callback: (err: Error | null, buf: Uint8Array) => void
): void;
export function randomFill(
  buffer: Uint8Array,
  offset: number,
  callback: (err: Error | null, buf: Uint8Array) => void
): void;
export function randomFill(
  buffer: Uint8Array,
  offset: number,
  size: number,
  callback: (err: Error | null, buf: Uint8Array) => void
): void;

export function pseudoRandomBytes(size: number): Uint8Array;
export function pseudoRandomBytes(size: number, callback: (err: Error | null, buf: Uint8Array) => void): Uint8Array;

export function randomUUID(): string;

// KeyObject
export interface KeyObject {
  type: 'private' | 'public' | 'secret';
  asymmetricKeyType?: 'ec' | 'ed25519' | 'ed448' | 'rsa' | 'rsa-pss' | 'dsa' | 'x25519' | 'x448';
  asymmetricKeySize?: number;
  symmetricKeySize?: number;
  export(options?: any): any;
}

export function createPrivateKey(key: string | Uint8Array | any): KeyObject;
export function createPublicKey(key: string | Uint8Array | any): KeyObject;
export function createSecretKey(key: Uint8Array): KeyObject;

// RSA operations (stubs)
export function publicEncrypt(key: string | Uint8Array | KeyObject, buffer: Uint8Array): Uint8Array;
export function privateDecrypt(key: string | Uint8Array | KeyObject, buffer: Uint8Array): Uint8Array;
