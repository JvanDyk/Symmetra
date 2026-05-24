# `@symmetra/kv` — KvStore Reference

VRAM key-value store. All data lives in GPU memory — no DRAM, no Redis, no network round-trips. Survives across requests for the lifetime of the server process.

## Installation

```typescript
import { KvStore } from '@symmetra/kv';
```

---

## `new KvStore(options?)`

Create a KV store. You can create multiple stores with independent namespaces.

```typescript
const kv = new KvStore({ maxMb: 512 });
```

### `KvStoreOptions`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `maxMb` | `number` | `256` | Maximum VRAM allocation for this store in megabytes |
| `namespace` | `string` | `'default'` | Logical namespace — keys are isolated per namespace |

```typescript
const cache   = new KvStore({ namespace: 'cache',    maxMb: 512 });
const session = new KvStore({ namespace: 'sessions', maxMb: 128 });
```

---

## Methods

### `kv.get<T>(key: string): Promise<T | null>`

Retrieve a value by key. Returns `null` if the key does not exist or has expired.

```typescript
const user = await kv.get<User>('user:42');
if (!user) {
  ctx.status(404).json({ error: 'not found' });
  return;
}
ctx.json(user);
```

### `kv.set(key: string, value: unknown, options?: SetOptions): Promise<void>`

Store a value. Values are serialised to JSON before being written to VRAM.

```typescript
await kv.set('user:42', { name: 'Alice', role: 'admin' });
await kv.set('token:abc', session, { ttlMs: 3_600_000 });  // 1 hour TTL
```

#### `SetOptions`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ttlMs` | `number` | `0` (no expiry) | Time-to-live in milliseconds |
| `ifNotExists` | `boolean` | `false` | Only write if the key does not already exist |

### `kv.del(key: string): Promise<void>`

Delete a key. No-op if the key does not exist.

```typescript
await kv.del('session:xyz');
```

### `kv.has(key: string): Promise<boolean>`

Check whether a key exists (without fetching its value).

```typescript
if (await kv.has('lock:resource')) {
  ctx.status(409).json({ error: 'locked' });
  return;
}
```

### `kv.keys(prefix?: string): Promise<string[]>`

List all keys, optionally filtered by prefix.

```typescript
const all     = await kv.keys();           // all keys
const users   = await kv.keys('user:');    // keys starting with "user:"
```

### `kv.clear(): Promise<void>`

Delete all keys in this store's namespace.

```typescript
await kv.clear();
```

### `kv.stats(): Promise<KvStats>`

Return memory usage and hit statistics.

```typescript
const stats = await kv.stats();
ctx.json(stats);
```

#### `KvStats`

| Field | Type | Description |
|-------|------|-------------|
| `keys` | `number` | Number of live keys |
| `bytesUsed` | `number` | VRAM bytes consumed by stored values |
| `bytesCapacity` | `number` | Total VRAM capacity for this store |
| `hits` | `number` | Total successful `get()` calls since startup |
| `misses` | `number` | Total `get()` calls that returned `null` |
| `evictions` | `number` | Number of entries evicted due to TTL expiry or capacity pressure |

---

## Complete example

```typescript
import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';

interface Item {
  name: string;
  value: number;
}

const app = new Router();
const kv  = new KvStore({ namespace: 'items', maxMb: 256 });

app.get('/item/:id', async (ctx) => {
  const item = await kv.get<Item>(ctx.params.id);
  if (!item) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json(item);
});

app.put('/item/:id', async (ctx) => {
  const body = await ctx.json<Item>();
  await kv.set(ctx.params.id, body, { ttlMs: 60_000 });
  ctx.json({ ok: true });
});

app.delete('/item/:id', async (ctx) => {
  await kv.del(ctx.params.id);
  ctx.status(204).text('');
});

app.get('/_kv/stats', async (ctx) => {
  ctx.json(await kv.stats());
});

serve(app, { port: 8080 });
```
