// Node.js buffer module compatibility layer

export type BufferEncoding = 'ascii' | 'utf8' | 'utf-8' | 'utf16le' | 'ucs2' | 'ucs-2' | 'base64' | 'latin1' | 'binary' | 'hex';

// Buffer is just Uint8Array with extra methods
export class Buffer extends Uint8Array {
  constructor(size: number | Uint8Array | string, encoding?: BufferEncoding);
  constructor(arrayBuffer: ArrayBuffer, byteOffset?: number, length?: number);
  constructor(arg: any, encodingOrByteOffset?: any, length?: any) {
    if (typeof arg === 'number') {
      super(arg);
    } else if (arg instanceof Uint8Array) {
      super(arg);
    } else if (typeof arg === 'string') {
      const encoded = encodeString(arg, encodingOrByteOffset);
      super(encoded);
    } else if (arg instanceof ArrayBuffer) {
      super(arg, encodingOrByteOffset, length);
    } else {
      super(arg);
    }
  }

  static alloc(size: number, fill?: number | string | Buffer, encoding?: BufferEncoding): Buffer {
    const buf = new Buffer(size);
    if (fill !== undefined) {
      buf.fill(fill, 0, size, encoding);
    }
    return buf;
  }

  static allocUnsafe(size: number): Buffer {
    return new Buffer(size);
  }

  static from(value: Uint8Array | string | ArrayBuffer | any, encodingOrOffset?: any, length?: any): Buffer {
    if (value instanceof Uint8Array) {
      return new Buffer(value);
    }
    if (typeof value === 'string') {
      return new Buffer(value, encodingOrOffset);
    }
    if (value instanceof ArrayBuffer) {
      return new Buffer(value, encodingOrOffset, length);
    }
    return new Buffer(String(value), encodingOrOffset);
  }

  static isBuffer(obj: any): obj is Buffer {
    return obj instanceof Buffer;
  }

  static isEncoding(encoding: string): boolean {
    return [
      'ascii', 'utf8', 'utf-8', 'utf16le', 'ucs2', 'ucs-2',
      'base64', 'latin1', 'binary', 'hex'
    ].includes(encoding.toLowerCase());
  }

  static byteLength(string: string, encoding: BufferEncoding = 'utf8'): number {
    const encoded = encodeString(string, encoding);
    return encoded.length;
  }

  static concat(list: Uint8Array[], totalLength?: number): Buffer {
    let len = totalLength || 0;
    if (len === 0) {
      for (const buf of list) {
        len += buf.length;
      }
    }

    const result = new Buffer(len);
    let pos = 0;
    for (const buf of list) {
      result.set(buf, pos);
      pos += buf.length;
    }
    return result;
  }

  static compare(buf1: Uint8Array, buf2: Uint8Array): number {
    for (let i = 0; i < Math.min(buf1.length, buf2.length); i++) {
      if (buf1[i] < buf2[i]) return -1;
      if (buf1[i] > buf2[i]) return 1;
    }
    return buf1.length - buf2.length;
  }

  // Instance methods
  toString(encoding: BufferEncoding = 'utf8', start: number = 0, end: number = this.length): string {
    const slice = this.slice(start, end);
    return decodeString(slice, encoding);
  }

  toJSON(): { type: string; data: number[] } {
    return {
      type: 'Buffer',
      data: Array.from(this)
    };
  }

  fill(value: number | string | Buffer, offset: number = 0, end: number = this.length, encoding?: BufferEncoding): this {
    if (typeof value === 'number') {
      for (let i = offset; i < end; i++) {
        this[i] = value & 0xFF;
      }
    } else if (typeof value === 'string') {
      const encoded = encodeString(value, encoding);
      for (let i = offset, j = 0; i < end; i++, j = (j + 1) % encoded.length) {
        this[i] = encoded[j];
      }
    } else if (value instanceof Uint8Array) {
      for (let i = offset, j = 0; i < end; i++, j = (j + 1) % value.length) {
        this[i] = value[j];
      }
    }
    return this;
  }

  copy(target: Uint8Array, targetStart: number = 0, start: number = 0, end: number = this.length): number {
    const length = Math.min(end - start, target.length - targetStart);
    target.set(this.slice(start, start + length), targetStart);
    return length;
  }

  equals(otherBuffer: Uint8Array): boolean {
    if (this.length !== otherBuffer.length) return false;
    for (let i = 0; i < this.length; i++) {
      if (this[i] !== otherBuffer[i]) return false;
    }
    return true;
  }

  compare(otherBuffer: Uint8Array, otherStart?: number, otherEnd?: number, thisStart?: number, thisEnd?: number): number {
    const bs = thisStart || 0;
    const be = thisEnd || this.length;
    const os = otherStart || 0;
    const oe = otherEnd || otherBuffer.length;

    for (let i = 0; i < Math.min(be - bs, oe - os); i++) {
      if (this[bs + i] < otherBuffer[os + i]) return -1;
      if (this[bs + i] > otherBuffer[os + i]) return 1;
    }

    return (be - bs) - (oe - os);
  }

  indexOf(value: number | string | Buffer, byteOffset?: number, encoding?: BufferEncoding): number {
    if (typeof value === 'number') {
      for (let i = byteOffset || 0; i < this.length; i++) {
        if (this[i] === value) return i;
      }
      return -1;
    }
    if (typeof value === 'string') {
      const encoded = encodeString(value, encoding);
      return this.indexOf(encoded);
    }
    // Find subarray
    for (let i = byteOffset || 0; i <= this.length - value.length; i++) {
      let match = true;
      for (let j = 0; j < value.length; j++) {
        if (this[i + j] !== value[j]) {
          match = false;
          break;
        }
      }
      if (match) return i;
    }
    return -1;
  }

  lastIndexOf(value: number | string | Buffer, byteOffset?: number, encoding?: BufferEncoding): number {
    if (typeof value === 'number') {
      for (let i = (byteOffset || this.length) - 1; i >= 0; i--) {
        if (this[i] === value) return i;
      }
      return -1;
    }
    if (typeof value === 'string') {
      const encoded = encodeString(value, encoding);
      return this.lastIndexOf(encoded);
    }
    // Find subarray from end
    for (let i = Math.min(byteOffset || this.length, this.length - value.length); i >= 0; i--) {
      let match = true;
      for (let j = 0; j < value.length; j++) {
        if (this[i + j] !== value[j]) {
          match = false;
          break;
        }
      }
      if (match) return i;
    }
    return -1;
  }

  readUInt8(offset: number = 0): number {
    return this[offset] & 0xFF;
  }

  writeUInt8(value: number, offset: number = 0): number {
    this[offset] = value & 0xFF;
    return offset + 1;
  }

  slice(start: number = 0, end: number = this.length): Buffer {
    return Buffer.from(super.slice(start, end));
  }
}

// Helper functions
function encodeString(str: string, encoding: BufferEncoding = 'utf8'): Uint8Array {
  encoding = (encoding || 'utf8').toLowerCase();

  switch (encoding) {
    case 'utf8':
    case 'utf-8': {
      const buf = [];
      for (let i = 0; i < str.length; i++) {
        const code = str.charCodeAt(i);
        if (code < 0x80) {
          buf.push(code);
        } else if (code < 0x800) {
          buf.push(0xC0 | (code >> 6));
          buf.push(0x80 | (code & 0x3F));
        } else {
          buf.push(0xE0 | (code >> 12));
          buf.push(0x80 | ((code >> 6) & 0x3F));
          buf.push(0x80 | (code & 0x3F));
        }
      }
      return new Uint8Array(buf);
    }
    case 'hex': {
      const buf = [];
      for (let i = 0; i < str.length; i += 2) {
        buf.push(parseInt(str.substr(i, 2), 16));
      }
      return new Uint8Array(buf);
    }
    case 'base64': {
      const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
      const buf = [];
      for (let i = 0; i < str.length; i += 4) {
        const a = chars.indexOf(str[i] || 'A');
        const b = chars.indexOf(str[i + 1] || 'A');
        const c = chars.indexOf(str[i + 2] || 'A');
        const d = chars.indexOf(str[i + 3] || 'A');
        buf.push((a << 2) | (b >> 4));
        if (str[i + 2] !== '=') buf.push(((b & 0xF) << 4) | (c >> 2));
        if (str[i + 3] !== '=') buf.push(((c & 0x3) << 6) | d);
      }
      return new Uint8Array(buf);
    }
    case 'ascii':
    case 'latin1':
    case 'binary': {
      const buf = [];
      for (let i = 0; i < str.length; i++) {
        buf.push(str.charCodeAt(i) & 0xFF);
      }
      return new Uint8Array(buf);
    }
    default: {
      return new Uint8Array(str.length);
    }
  }
}

function decodeString(buf: Uint8Array, encoding: BufferEncoding = 'utf8'): string {
  encoding = (encoding || 'utf8').toLowerCase();

  switch (encoding) {
    case 'utf8':
    case 'utf-8': {
      let result = '';
      let i = 0;
      while (i < buf.length) {
        const byte1 = buf[i++];
        if ((byte1 & 0x80) === 0) {
          result += String.fromCharCode(byte1);
        } else if ((byte1 & 0xE0) === 0xC0) {
          const byte2 = buf[i++];
          result += String.fromCharCode(((byte1 & 0x1F) << 6) | (byte2 & 0x3F));
        } else if ((byte1 & 0xF0) === 0xE0) {
          const byte2 = buf[i++];
          const byte3 = buf[i++];
          result += String.fromCharCode(((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F));
        }
      }
      return result;
    }
    case 'hex': {
      let result = '';
      for (const byte of buf) {
        result += byte.toString(16).padStart(2, '0');
      }
      return result;
    }
    case 'base64': {
      const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
      let result = '';
      for (let i = 0; i < buf.length; i += 3) {
        const a = buf[i] >> 2;
        const b = ((buf[i] & 0x03) << 4) | (buf[i + 1] >> 4 || 0);
        const c = ((buf[i + 1] & 0x0F) << 2) | (buf[i + 2] >> 6 || 0);
        const d = buf[i + 2] & 0x3F || 0;
        result += chars[a] + chars[b] + (i + 1 < buf.length ? chars[c] : '=') + (i + 2 < buf.length ? chars[d] : '=');
      }
      return result;
    }
    case 'ascii':
    case 'latin1':
    case 'binary': {
      let result = '';
      for (const byte of buf) {
        result += String.fromCharCode(byte);
      }
      return result;
    }
    default: {
      return buf.toString();
    }
  }
}

// Re-exports
export function isBuffer(obj: any): obj is Buffer {
  return Buffer.isBuffer(obj);
}

export function isEncoding(encoding: string): boolean {
  return Buffer.isEncoding(encoding);
}

export function byteLength(str: string, encoding: BufferEncoding = 'utf8'): number {
  return Buffer.byteLength(str, encoding);
}

export function concat(list: Uint8Array[], totalLength?: number): Buffer {
  return Buffer.concat(list, totalLength);
}

export function compare(buf1: Uint8Array, buf2: Uint8Array): number {
  return Buffer.compare(buf1, buf2);
}

export const MAX_LENGTH = 0x7FFFFFFF; // 2GB max buffer length
export const MAX_STRING_LENGTH = 0x3FFFFFFF; // Max string length
