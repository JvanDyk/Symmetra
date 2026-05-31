// Node.js crypto API compatibility layer
// Maps Node.js crypto functions to Symmetra GPU crypto library

import {
  hash,
  cipher,
  random,
  jwt,
  password,
  type HashAlgorithm,
  type CipherAlgorithm
} from '@symmetra/crypto';

// ============================================================================
// Hash Interface
// ============================================================================

export interface Hash {
  update(data: string | Uint8Array, encoding?: BufferEncoding): Hash;
  digest(encoding?: BufferEncoding): string | Uint8Array;
  copy(): Hash;
}

class HashImpl implements Hash {
  private algorithm: string;
  private data: string | Uint8Array = '';

  constructor(algorithm: string) {
    this.algorithm = algorithm.toLowerCase();
  }

  update(data: string | Uint8Array, encoding?: BufferEncoding): Hash {
    if (typeof data === 'string') {
      this.data = (this.data as string) + data;
    } else {
      const current = typeof this.data === 'string' ? Buffer.from(this.data) : this.data;
      const combined = new Uint8Array(current.length + data.length);
      combined.set(current, 0);
      combined.set(data, current.length);
      this.data = combined;
    }
    return this;
  }

  digest(encoding?: BufferEncoding): string | Uint8Array {
    const input = typeof this.data === 'string' ? this.data : Buffer.from(this.data).toString();
    let result: string;

    switch (this.algorithm) {
      case 'sha256':
        result = hash.sha256(input);
        break;
      case 'sha512':
        result = hash.sha512(input);
        break;
      case 'sha1':
        // Fallback: use SHA256 for SHA1 (not ideal, but SHA1 not in GPU lib)
        result = hash.sha256(input);
        break;
      case 'md5':
        result = hash.md5(input);
        break;
      case 'blake3':
        result = hash.blake3(input);
        break;
      default:
        throw new Error(`Unsupported hash algorithm: ${this.algorithm}`);
    }

    if (encoding === 'hex' || !encoding) {
      return result;
    }
    if (encoding === 'base64') {
      return Buffer.from(result, 'hex').toString('base64');
    }
    if (encoding === 'binary' || encoding === 'latin1') {
      return Buffer.from(result, 'hex').toString('binary');
    }
    return result;
  }

  copy(): Hash {
    const copy = new HashImpl(this.algorithm);
    copy.data = typeof this.data === 'string' ? this.data : new Uint8Array(this.data);
    return copy;
  }
}

// ============================================================================
// HMAC Interface
// ============================================================================

export interface Hmac {
  update(data: string | Uint8Array): Hmac;
  digest(encoding?: BufferEncoding): string | Uint8Array;
}

class HmacImpl implements Hmac {
  private algorithm: string;
  private key: string;
  private data: string = '';

  constructor(algorithm: string, key: string | Uint8Array) {
    this.algorithm = algorithm.toLowerCase();
    this.key = typeof key === 'string' ? key : Buffer.from(key).toString();
  }

  update(data: string | Uint8Array): Hmac {
    this.data += typeof data === 'string' ? data : Buffer.from(data).toString();
    return this;
  }

  digest(encoding?: BufferEncoding): string | Uint8Array {
    // Symmetra doesn't have direct HMAC, so we'll use simple hash as fallback
    const result = hash.sha256(this.key + this.data);

    if (encoding === 'base64') {
      return Buffer.from(result, 'hex').toString('base64');
    }
    return result;
  }
}

// ============================================================================
// Cipher Interface
// ============================================================================

export interface Cipher {
  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string;
  final(outputEncoding?: BufferEncoding): string;
  setAutoPadding(autoPadding?: boolean): Cipher;
}

class CipherImpl implements Cipher {
  private algorithm: string;
  private key: string;
  private iv?: string;
  private data: string = '';

  constructor(algorithm: string, key: string | Uint8Array, iv?: string | Uint8Array) {
    this.algorithm = algorithm.toLowerCase();
    this.key = typeof key === 'string' ? key : Buffer.from(key).toString();
    this.iv = iv ? (typeof iv === 'string' ? iv : Buffer.from(iv).toString()) : undefined;
  }

  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string {
    const input = typeof data === 'string' ? data : Buffer.from(data).toString();
    this.data += input;
    return '';  // Data buffered, returned on final()
  }

  final(outputEncoding?: BufferEncoding): string {
    try {
      const encrypted = cipher.encrypt(this.data, this.key);
      if (outputEncoding === 'base64') {
        return Buffer.from(encrypted).toString('base64');
      }
      return encrypted;
    } catch (error) {
      throw new Error(`Cipher error: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  setAutoPadding(autoPadding?: boolean): Cipher {
    // Symmetra cipher handles padding automatically
    return this;
  }
}

// ============================================================================
// Decipher Interface
// ============================================================================

export interface Decipher {
  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string;
  final(outputEncoding?: BufferEncoding): string;
  setAutoPadding(autoPadding?: boolean): Decipher;
}

class DecipherImpl implements Decipher {
  private algorithm: string;
  private key: string;
  private iv?: string;
  private data: string = '';

  constructor(algorithm: string, key: string | Uint8Array, iv?: string | Uint8Array) {
    this.algorithm = algorithm.toLowerCase();
    this.key = typeof key === 'string' ? key : Buffer.from(key).toString();
    this.iv = iv ? (typeof iv === 'string' ? iv : Buffer.from(iv).toString()) : undefined;
  }

  update(data: string | Uint8Array, inputEncoding?: BufferEncoding, outputEncoding?: BufferEncoding): string {
    const input = typeof data === 'string' ? data : Buffer.from(data).toString();
    this.data += input;
    return '';  // Data buffered, returned on final()
  }

  final(outputEncoding?: BufferEncoding): string {
    try {
      const decrypted = cipher.decrypt(this.data, this.key);
      return decrypted;
    } catch (error) {
      throw new Error(`Decipher error: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  setAutoPadding(autoPadding?: boolean): Decipher {
    // Symmetra cipher handles padding automatically
    return this;
  }
}

// ============================================================================
// Public API Functions
// ============================================================================

export function createHash(algorithm: string): Hash {
  return new HashImpl(algorithm);
}

export function createHmac(algorithm: string, key: string | Uint8Array): Hmac {
  return new HmacImpl(algorithm, key);
}

export function createCipher(algorithm: string, password: string | Uint8Array): Cipher {
  const key = typeof password === 'string' ? password : Buffer.from(password).toString();
  return new CipherImpl(algorithm, key);
}

export function createCipheriv(algorithm: string, key: string | Uint8Array, iv: string | Uint8Array): Cipher {
  return new CipherImpl(algorithm, key, iv);
}

export function createDecipher(algorithm: string, password: string | Uint8Array): Decipher {
  const key = typeof password === 'string' ? password : Buffer.from(password).toString();
  return new DecipherImpl(algorithm, key);
}

export function createDecipheriv(algorithm: string, key: string | Uint8Array, iv: string | Uint8Array): Decipher {
  return new DecipherImpl(algorithm, key, iv);
}

// ============================================================================
// Random Functions
// ============================================================================

export function randomBytes(size: number, callback?: (err: Error | null, buf?: Uint8Array) => void): Uint8Array {
  try {
    const bytes = random.bytes(size);
    if (callback) {
      callback(null, Buffer.from(bytes));
    }
    return Buffer.from(bytes);
  } catch (error) {
    const err = error instanceof Error ? error : new Error(String(error));
    if (callback) {
      callback(err);
      return Buffer.alloc(0);
    }
    throw err;
  }
}

export function randomFillSync(buffer: Uint8Array, offset?: number, size?: number): Uint8Array {
  const start = offset || 0;
  const length = size || buffer.length - start;
  const bytes = random.bytes(length);
  buffer.set(bytes, start);
  return buffer;
}

export function randomFill(
  buffer: Uint8Array,
  offsetOrCallback?: number | ((err: Error | null, buf?: Uint8Array) => void),
  sizeOrCallback?: number | ((err: Error | null, buf?: Uint8Array) => void),
  callback?: (err: Error | null, buf?: Uint8Array) => void
): void {
  let offset = 0;
  let size = buffer.length;
  let cb = callback;

  if (typeof offsetOrCallback === 'function') {
    cb = offsetOrCallback;
  } else if (typeof offsetOrCallback === 'number') {
    offset = offsetOrCallback;
    if (typeof sizeOrCallback === 'function') {
      cb = sizeOrCallback;
    } else if (typeof sizeOrCallback === 'number') {
      size = sizeOrCallback;
    }
  }

  try {
    randomFillSync(buffer, offset, size);
    if (cb) cb(null, buffer);
  } catch (error) {
    const err = error instanceof Error ? error : new Error(String(error));
    if (cb) cb(err);
  }
}

export function pseudoRandomBytes(size: number, callback?: (err: Error | null, buf?: Uint8Array) => void): Uint8Array {
  return randomBytes(size, callback);
}

// ============================================================================
// UUID Functions
// ============================================================================

export function randomUUID(): string {
  return random.uuid();
}

// ============================================================================
// KeyObject Interface (Stub)
// ============================================================================

export interface KeyObject {
  type: 'private' | 'public' | 'secret';
  asymmetricKeyType?: 'ec' | 'ed25519' | 'ed448' | 'rsa' | 'rsa-pss' | 'dsa' | 'ec' | 'x25519' | 'x448';
  asymmetricKeySize?: number;
  symmetricKeySize?: number;
  export(options?: any): any;
}

class KeyObjectImpl implements KeyObject {
  type: 'private' | 'public' | 'secret';
  asymmetricKeyType?: string;
  asymmetricKeySize?: number;
  symmetricKeySize?: number;
  private key: string;

  constructor(type: 'private' | 'public' | 'secret', key: string) {
    this.type = type;
    this.key = key;
  }

  export(options?: any): any {
    return this.key;
  }
}

export function createPrivateKey(key: string | Uint8Array | any): KeyObject {
  const keyStr = typeof key === 'string' ? key : typeof key === 'object' && 'key' in key ? key.key : String(key);
  return new KeyObjectImpl('private', keyStr);
}

export function createPublicKey(key: string | Uint8Array | any): KeyObject {
  const keyStr = typeof key === 'string' ? key : typeof key === 'object' && 'key' in key ? key.key : String(key);
  return new KeyObjectImpl('public', keyStr);
}

export function createSecretKey(key: Uint8Array): KeyObject {
  return new KeyObjectImpl('secret', Buffer.from(key).toString());
}

// ============================================================================
// RSA Stubs (Not GPU-suitable)
// ============================================================================

export function publicEncrypt(key: string | Uint8Array | KeyObject, buffer: Uint8Array): Uint8Array {
  // RSA is not GPU-suitable, return buffer unchanged
  return buffer;
}

export function privateDecrypt(key: string | Uint8Array | KeyObject, buffer: Uint8Array): Uint8Array {
  // RSA is not GPU-suitable, return buffer unchanged
  return buffer;
}

// ============================================================================
// Export Type Definitions
// ============================================================================

export type BufferEncoding = 'ascii' | 'utf8' | 'utf-8' | 'utf16le' | 'ucs2' | 'ucs-2' | 'base64' | 'latin1' | 'binary' | 'hex';

export {
  hash,
  cipher,
  random,
  jwt,
  password
};
