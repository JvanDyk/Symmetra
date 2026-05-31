// Node.js util module compatibility layer for Symmetra

export interface InspectOptions {
  showHidden?: boolean;
  depth?: number | null;
  colors?: boolean;
  compact?: boolean | number;
  breakLength?: number;
  maxStringLength?: number;
  maxArrayLength?: number;
  sorted?: boolean;
  getters?: boolean;
  numericSeparator?: boolean;
}

// Simple object type checking
export function isArray(object: any): object is any[] {
  return Array.isArray(object);
}

export function isRegExp(object: any): object is RegExp {
  return object instanceof RegExp;
}

export function isDate(object: any): object is Date {
  return object instanceof Date;
}

export function isError(object: any): object is Error {
  return object instanceof Error;
}

export function isBoolean(object: any): object is boolean {
  return typeof object === 'boolean';
}

export function isNull(object: any): object is null {
  return object === null;
}

export function isNullOrUndefined(object: any): object is null | undefined {
  return object === null || object === undefined;
}

export function isNumber(object: any): object is number {
  return typeof object === 'number';
}

export function isString(object: any): object is string {
  return typeof object === 'string';
}

export function isSymbol(object: any): object is symbol {
  return typeof object === 'symbol';
}

export function isUndefined(object: any): object is undefined {
  return object === undefined;
}

export function isObject(object: any): object is object {
  return object !== null && typeof object === 'object';
}

export function isFunction(object: any): object is Function {
  return typeof object === 'function';
}

// String formatting
export function format(f: any, ...args: any[]): string {
  let i = 0;
  let str = String(f).replace(/%[sdiqO%]/g, (x) => {
    if (x === '%%') return '%';
    if (i >= args.length) return x;
    switch (x) {
      case '%s': return String(args[i++]);
      case '%d': return Number(args[i++]);
      case '%i': return Math.floor(Number(args[i++]));
      case '%q': return JSON.stringify(args[i++]);
      case '%O': return JSON.stringify(args[i++], null, 2);
      default: return x;
    }
  });

  for (; i < args.length; i++) {
    str += ' ' + inspect(args[i]);
  }

  return str;
}

// Simple inspect implementation
export function inspect(value: any, options?: InspectOptions | boolean | number): string {
  if (value === null) return 'null';
  if (value === undefined) return 'undefined';
  if (typeof value === 'string') return `'${value}'`;
  if (typeof value === 'number' || typeof value === 'boolean') return String(value);
  if (value instanceof RegExp) return value.toString();
  if (value instanceof Date) return value.toISOString();
  if (Array.isArray(value)) {
    return '[' + value.map(v => inspect(v)).join(', ') + ']';
  }
  if (typeof value === 'object') {
    const keys = Object.keys(value);
    if (keys.length === 0) return '{}';
    return '{' + keys.map(k => `${k}: ${inspect(value[k])}`).join(', ') + '}';
  }
  if (typeof value === 'function') {
    return `[Function: ${value.name || 'anonymous'}]`;
  }
  return String(value);
}

// Deprecation warnings
const deprecatedWarnings = new Set<string>();

export function deprecate(fn: Function, message: string, code?: string): Function {
  return function (this: any, ...args: any[]) {
    const key = `${fn.name}:${code || message}`;
    if (!deprecatedWarnings.has(key)) {
      console.warn(`DeprecationWarning: ${message}` + (code ? ` [${code}]` : ''));
      deprecatedWarnings.add(key);
    }
    return fn.apply(this, args);
  };
}

// Text encoding/decoding
export function TextEncoder() {
  return {
    encode(text: string) {
      const buffer = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) {
        buffer[i] = text.charCodeAt(i);
      }
      return buffer;
    }
  };
}

export function TextDecoder(encoding: string = 'utf-8') {
  return {
    decode(buffer: Uint8Array) {
      let result = '';
      for (let i = 0; i < buffer.length; i++) {
        result += String.fromCharCode(buffer[i]);
      }
      return result;
    }
  };
}

// Promisify (basic stub)
export function promisify(fn: Function): (...args: any[]) => Promise<any> {
  return function (this: any, ...args: any[]) {
    return new Promise((resolve, reject) => {
      fn.apply(this, [...args, (err: any, result: any) => {
        if (err) reject(err);
        else resolve(result);
      }]);
    });
  };
}

// Callable promisify
promisify.custom = Symbol('util.promisify.custom');

// Inherits (old-style prototype inheritance)
export function inherits(constructor: Function, superConstructor: Function): void {
  constructor.prototype = Object.create(superConstructor.prototype);
  constructor.prototype.constructor = constructor;
}

// Types
export type TextEncoder = ReturnType<typeof TextEncoder>;
export type TextDecoder = ReturnType<typeof TextDecoder>;
