# `@symmetra/fs` — Filesystem API Reference

Read and write files from inside GPU handlers. File I/O is always async — the host CPU performs the actual syscall while the GPU continues processing other requests.

## Installation

```typescript
import { readFile, writeFile, appendFile, stat, exists, readDir } from '@symmetra/fs';
```

---

## Reading files

### `readFile(path: string, encoding?: 'utf8'): Promise<string>`
### `readFile(path: string, encoding: 'binary'): Promise<Uint8Array>`

Read an entire file into VRAM.

```typescript
// Read as UTF-8 string
const text = await readFile('./config.json', 'utf8');
const config = JSON.parse(text);

// Read as binary
const img = await readFile('./logo.png', 'binary');
ctx.json({ size: img.byteLength });
```

**Throws** `FsError` if the file does not exist or is not readable.

---

## Writing files

### `writeFile(path: string, data: string | Uint8Array): Promise<void>`

Write data to a file, creating it if necessary. **Overwrites** existing content.

```typescript
await writeFile('./output.json', JSON.stringify({ ok: true }));

const encoder = new TextEncoder();
await writeFile('./data.bin', encoder.encode('hello'));
```

### `appendFile(path: string, data: string): Promise<void>`

Append to an existing file, or create it if it does not exist.

```typescript
await appendFile('./server.log', `${Date.now()} GET /ping\n`);
```

---

## File metadata

### `stat(path: string): Promise<FileStat>`

Return metadata for a file or directory.

```typescript
const info = await stat('./data.csv');
ctx.json({
  size:  info.size,
  isDir: info.isDirectory,
  mtime: info.mtime,
});
```

#### `FileStat`

| Field | Type | Description |
|-------|------|-------------|
| `size` | `number` | File size in bytes |
| `isFile` | `boolean` | True if this is a regular file |
| `isDirectory` | `boolean` | True if this is a directory |
| `mtime` | `number` | Last-modified timestamp (milliseconds since epoch) |
| `ctime` | `number` | Creation timestamp (milliseconds since epoch) |

### `exists(path: string): Promise<boolean>`

Return `true` if the path exists (file or directory).

```typescript
if (!await exists('./config.json')) {
  ctx.status(500).json({ error: 'config missing' });
  return;
}
```

---

## Directories

### `readDir(path: string): Promise<DirEntry[]>`

List the contents of a directory.

```typescript
const entries = await readDir('./uploads');
const files   = entries.filter(e => e.isFile);
ctx.json(files.map(e => ({ name: e.name, size: e.size })));
```

#### `DirEntry`

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | File or directory name (not full path) |
| `isFile` | `boolean` | True for regular files |
| `isDirectory` | `boolean` | True for directories |
| `size` | `number` | File size (0 for directories) |

### `mkdir(path: string, options?: { recursive?: boolean }): Promise<void>`

Create a directory.

```typescript
await mkdir('./uploads', { recursive: true });
```

### `remove(path: string): Promise<void>`

Delete a file or empty directory.

```typescript
await remove('./tmp/scratch.dat');
```

---

## Error handling

All functions throw `FsError` on failure:

```typescript
import { FsError } from '@symmetra/fs';

try {
  const data = await readFile('./missing.txt', 'utf8');
} catch (e) {
  if (e instanceof FsError) {
    ctx.status(404).json({ error: e.message, code: e.code });
  }
}
```

#### `FsError`

| Property | Type | Description |
|----------|------|-------------|
| `message` | `string` | Human-readable error |
| `code` | `string` | OS error code (`ENOENT`, `EACCES`, `EISDIR`, …) |
| `path` | `string` | The path that caused the error |

---

## Complete example

```typescript
import { serve, Router } from '@symmetra/http';
import { readFile, writeFile, exists, stat } from '@symmetra/fs';

const app = new Router();

app.get('/file/:name', async (ctx) => {
  const path = `./data/${ctx.params.name}`;
  if (!await exists(path)) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  const info = await stat(path);
  const text = await readFile(path, 'utf8');
  ctx.json({ name: ctx.params.name, size: info.size, content: text });
});

app.put('/file/:name', async (ctx) => {
  const text = await ctx.text();
  await writeFile(`./data/${ctx.params.name}`, text);
  ctx.status(201).json({ ok: true });
});

serve(app, { port: 8080 });
```
