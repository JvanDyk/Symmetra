// Node.js path module compatibility layer
// Path manipulation for Symmetra GPU runtime

export interface ParsedPath {
  root: string;
  dir: string;
  base: string;
  ext: string;
  name: string;
}

export interface FormatInputPathObject {
  root?: string;
  dir?: string;
  base?: string;
  ext?: string;
  name?: string;
}

const isWindows = false; // Assume POSIX on GPU runtime
const sep = '/';
const delimiter = ':';

export { sep, delimiter };

// ============================================================================
// Path Functions
// ============================================================================

export function normalize(p: string): string {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }

  // Handle empty path
  if (p.length === 0) return '.';

  // Normalize slashes
  const isAbsolute = p[0] === '/';
  const trailingSlash = p.endsWith('/');

  // Split, filter, and rejoin
  const parts = p.split('/').filter((part, i) => {
    // Skip empty parts except for first (which indicates absolute)
    if (part === '' || part === '.') return false;
    if (part === '..') return true;
    return true;
  });

  // Handle parent directory references
  const normalized = [];
  for (const part of parts) {
    if (part === '..') {
      if (normalized.length > 0 && normalized[normalized.length - 1] !== '..') {
        normalized.pop();
      } else if (!isAbsolute) {
        normalized.push('..');
      }
    } else {
      normalized.push(part);
    }
  }

  let result = normalized.join('/');
  if (isAbsolute) result = '/' + result;
  if (trailingSlash && result !== '/') result += '/';
  if (result === '') result = '.';

  return result;
}

export function join(...paths: string[]): string {
  if (paths.length === 0) return '.';
  return normalize(paths.join('/'));
}

export function resolve(...paths: string[]): string {
  let resolvedPath = '';
  let resolvedAbsolute = false;

  for (let i = paths.length - 1; i >= 0; i--) {
    const path = paths[i];
    if (typeof path !== 'string') continue;

    resolvedPath = path + (resolvedPath ? '/' + resolvedPath : '');

    if (path.startsWith('/')) {
      resolvedAbsolute = true;
      break;
    }
  }

  if (!resolvedAbsolute) {
    resolvedPath = normalize(process.cwd ? process.cwd() + '/' + resolvedPath : '/current/' + resolvedPath);
  } else {
    resolvedPath = normalize('/' + resolvedPath);
  }

  return resolvedPath || '/';
}

export function relative(from: string, to: string): string {
  from = resolve(from);
  to = resolve(to);

  if (from === to) return '';

  const fromParts = from.split('/').filter(p => p);
  const toParts = to.split('/').filter(p => p);

  let commonLength = 0;
  for (let i = 0; i < Math.min(fromParts.length, toParts.length); i++) {
    if (fromParts[i] === toParts[i]) {
      commonLength++;
    } else {
      break;
    }
  }

  const ups = Array(fromParts.length - commonLength).fill('..').join('/');
  const downs = toParts.slice(commonLength).join('/');

  return ups ? (downs ? ups + '/' + downs : ups) : (downs || '.');
}

export function dirname(p: string): string {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }

  if (p.length === 0) return '.';

  const lastSlash = p.lastIndexOf('/');
  if (lastSlash === 0) return '/';
  if (lastSlash === -1) return '.';

  return p.slice(0, lastSlash);
}

export function basename(p: string, ext?: string): string {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }

  const lastSlash = p.lastIndexOf('/');
  const start = lastSlash === -1 ? 0 : lastSlash + 1;
  let name = p.slice(start);

  if (ext && name.endsWith(ext)) {
    name = name.slice(0, name.length - ext.length);
  }

  return name;
}

export function extname(p: string): string {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }

  const base = basename(p);
  const dotIdx = base.lastIndexOf('.');

  if (dotIdx < 0 || dotIdx === 0) {
    return '';
  }

  return base.slice(dotIdx);
}

export function parse(p: string): ParsedPath {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }

  const ext = extname(p);
  const root = p.startsWith('/') ? '/' : '';
  const dir = dirname(p);
  const name = basename(p, ext);
  const base = basename(p);

  return { root, dir, base, ext, name };
}

export function format(pathObject: FormatInputPathObject): string {
  if (!pathObject) {
    throw new TypeError('Path object required');
  }

  const { root = '', dir, base, ext, name } = pathObject;

  if (base) {
    return (dir ? dir + '/' : '') + base;
  }

  const filename = name + (ext || '');
  return (dir || root) + (dir && filename ? '/' : '') + filename;
}

export function isAbsolute(p: string): boolean {
  if (typeof p !== 'string') {
    throw new TypeError('Path must be a string');
  }
  return p.length > 0 && p[0] === '/';
}

// ============================================================================
// Re-exports for POSIX
// ============================================================================

export const posix = {
  normalize,
  join,
  resolve,
  relative,
  dirname,
  basename,
  extname,
  parse,
  format,
  isAbsolute,
  sep: '/',
  delimiter: ':'
};

// Default exports
export const win32 = posix; // Symmetra is POSIX-only on GPU
