// Node.js fs module stubs (file I/O not suitable for GPU runtime)

export const constants = { F_OK: 0, R_OK: 4, W_OK: 2, X_OK: 1 };

export const readFile = (path: string, callback?: (err: Error | null, data?: any) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'), null);
};

export const writeFile = (path: string, data: any, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const appendFile = (path: string, data: any, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const readdir = (path: string, callback?: (err: Error | null, files?: string[]) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'), []);
};

export const mkdir = (path: string, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const rmdir = (path: string, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const unlink = (path: string, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const stat = (path: string, callback?: (err: Error | null, stats?: any) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'), null);
};

export const exists = (path: string, callback?: (exists: boolean) => void) => {
  if (callback) callback(false);
};

export const access = (path: string, mode: number, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const copyFile = (src: string, dest: string, callback?: (err: Error | null) => void) => {
  if (callback) callback(new Error('File I/O not available on GPU'));
};

export const promises = {
  readFile: () => Promise.reject(new Error('File I/O not available on GPU')),
  writeFile: () => Promise.reject(new Error('File I/O not available on GPU')),
  mkdir: () => Promise.reject(new Error('File I/O not available on GPU')),
  readdir: () => Promise.reject(new Error('File I/O not available on GPU')),
  stat: () => Promise.reject(new Error('File I/O not available on GPU'))
};

// Sync stubs
export const readFileSync = (path: string) => Buffer.alloc(0);
export const writeFileSync = (path: string, data: any) => {};
export const readdirSync = (path: string) => [];
export const mkdirSync = (path: string) => '';
export const statSync = (path: string) => ({});
export const existsSync = (path: string) => false;

export default { constants, readFile, writeFile, appendFile, readdir, mkdir, rmdir, unlink, stat, exists, access, copyFile, promises, readFileSync, writeFileSync, readdirSync, mkdirSync, statSync, existsSync };
