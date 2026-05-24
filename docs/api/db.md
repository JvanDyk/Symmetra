# `@symmetra/db` — Database Reference

PostgreSQL, SQLite, and Redis adapters for Symmetra. All queries run on the CPU thread pool (database drivers are CPU-native). Results are pinned to VRAM before being returned to your GPU handler.

## Installation

```typescript
import { postgres, sqlite, redis } from '@symmetra/db';
```

---

## PostgreSQL

### `postgres(connectionString: string): PgClient`

Create a PostgreSQL connection pool.

```typescript
const pg = postgres(process.env.DATABASE_URL!);
```

The connection pool size defaults to `Math.min(streams, 20)`, matching the server's stream count so every concurrent request can hold a connection.

#### `pg.query<T>(sql: string, params?: unknown[]): Promise<T[]>`

Run a parameterised query. Returns an array of typed row objects.

```typescript
const users = await pg.query<{ id: number; name: string }>(
  'SELECT id, name FROM users WHERE active = $1',
  [true],
);
ctx.json(users);
```

#### `pg.queryOne<T>(sql: string, params?: unknown[]): Promise<T | null>`

Like `query`, but returns the first row or `null`.

```typescript
const user = await pg.queryOne<User>(
  'SELECT * FROM users WHERE id = $1',
  [ctx.params.id],
);
if (!user) {
  ctx.status(404).json({ error: 'not found' });
  return;
}
ctx.json(user);
```

#### `pg.execute(sql: string, params?: unknown[]): Promise<{ rowCount: number }>`

Run a DML statement (`INSERT`, `UPDATE`, `DELETE`). Returns the number of affected rows.

```typescript
const result = await pg.execute(
  'UPDATE users SET name = $1 WHERE id = $2',
  [name, id],
);
ctx.json({ updated: result.rowCount });
```

#### `pg.transaction<T>(fn: (tx: PgTx) => Promise<T>): Promise<T>`

Run a function inside a transaction. Automatically commits on success, rolls back on throw.

```typescript
const result = await pg.transaction(async (tx) => {
  const user = await tx.queryOne<User>('SELECT * FROM users WHERE id = $1 FOR UPDATE', [id]);
  if (!user) throw new Error('not found');
  await tx.execute('UPDATE wallets SET balance = balance - $1 WHERE user_id = $2', [amount, id]);
  await tx.execute('INSERT INTO ledger (user_id, amount) VALUES ($1, $2)', [id, -amount]);
  return user;
});
ctx.json({ ok: true });
```

#### `pg.close(): Promise<void>`

Drain the connection pool and close all connections.

---

## SQLite

### `sqlite(path: string): SqliteDb`

Open (or create) a SQLite database file.

```typescript
const db = sqlite('./data.db');
```

#### `db.query<T>(sql: string, params?: unknown[]): T[]`

Synchronous query — SQLite is embedded, no async needed.

```typescript
const rows = db.query<{ id: number; value: string }>(
  'SELECT * FROM items WHERE score > ?',
  [100],
);
ctx.json(rows);
```

#### `db.execute(sql: string, params?: unknown[]): { changes: number; lastInsertId: number }`

Run a DML statement.

```typescript
const { lastInsertId } = db.execute(
  'INSERT INTO items (name, score) VALUES (?, ?)',
  [name, score],
);
ctx.json({ id: lastInsertId });
```

#### `db.transaction(fn: (tx: SqliteTx) => void): void`

Synchronous transaction.

```typescript
db.transaction((tx) => {
  tx.execute('INSERT INTO a (x) VALUES (?)', [1]);
  tx.execute('INSERT INTO b (x) VALUES (?)', [2]);
});
```

#### `db.close(): void`

Close the database file handle.

---

## Redis

### `redis(url?: string): RedisClient`

Connect to Redis. Defaults to `redis://127.0.0.1:6379`.

```typescript
const r = redis(process.env.REDIS_URL);
```

#### `r.get(key: string): Promise<string | null>`

```typescript
const val = await r.get('counter');
```

#### `r.set(key: string, value: string, options?: RedisSetOptions): Promise<void>`

```typescript
await r.set('session:abc', JSON.stringify(session), { ex: 3600 }); // 1h TTL
```

#### `RedisSetOptions`

| Field | Type | Description |
|-------|------|-------------|
| `ex` | `number` | Expiry in seconds |
| `px` | `number` | Expiry in milliseconds |
| `nx` | `boolean` | Only set if not exists |
| `xx` | `boolean` | Only set if exists |

#### `r.del(...keys: string[]): Promise<number>`

Delete one or more keys. Returns number of keys deleted.

#### `r.incr(key: string): Promise<number>`

Atomically increment a counter.

```typescript
const count = await r.incr(`views:${articleId}`);
```

#### `r.expire(key: string, seconds: number): Promise<boolean>`

Set a key's TTL.

#### `r.ttl(key: string): Promise<number>`

Return the remaining TTL in seconds, or `-1` if no TTL, `-2` if key doesn't exist.

#### `r.hset(key: string, field: string, value: string): Promise<void>`
#### `r.hget(key: string, field: string): Promise<string | null>`
#### `r.hgetall(key: string): Promise<Record<string, string>>`
#### `r.hdel(key: string, ...fields: string[]): Promise<number>`

Hash operations.

#### `r.lpush(key: string, ...values: string[]): Promise<number>`
#### `r.rpush(key: string, ...values: string[]): Promise<number>`
#### `r.lrange(key: string, start: number, stop: number): Promise<string[]>`
#### `r.llen(key: string): Promise<number>`

List operations.

#### `r.sadd(key: string, ...members: string[]): Promise<number>`
#### `r.smembers(key: string): Promise<string[]>`
#### `r.sismember(key: string, member: string): Promise<boolean>`

Set operations.

#### `r.zadd(key: string, score: number, member: string): Promise<number>`
#### `r.zrange(key: string, start: number, stop: number): Promise<string[]>`
#### `r.zscore(key: string, member: string): Promise<number | null>`

Sorted set operations.

#### `r.publish(channel: string, message: string): Promise<number>`
#### `r.subscribe(channel: string, fn: (message: string) => void): Promise<void>`

Pub/sub.

#### `r.close(): Promise<void>`

Close the Redis connection.

---

## Complete example

```typescript
import { serve, Router } from '@symmetra/http';
import { postgres } from '@symmetra/db';

interface Product {
  id: number;
  name: string;
  price: number;
  stock: number;
}

const app = new Router();
const pg  = postgres(process.env.DATABASE_URL!);

app.get('/products', async (ctx) => {
  const rows = await pg.query<Product>('SELECT * FROM products ORDER BY name');
  ctx.json(rows);
});

app.get('/products/:id', async (ctx) => {
  const product = await pg.queryOne<Product>(
    'SELECT * FROM products WHERE id = $1',
    [ctx.params.id],
  );
  if (!product) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json(product);
});

app.post('/products', async (ctx) => {
  const { name, price } = await ctx.json<{ name: string; price: number }>();
  const row = await pg.queryOne<{ id: number }>(
    'INSERT INTO products (name, price, stock) VALUES ($1, $2, 0) RETURNING id',
    [name, price],
  );
  ctx.status(201).json({ id: row!.id });
});

app.post('/products/:id/purchase', async (ctx) => {
  const { quantity } = await ctx.json<{ quantity: number }>();
  await pg.transaction(async (tx) => {
    const p = await tx.queryOne<Product>(
      'SELECT * FROM products WHERE id = $1 FOR UPDATE',
      [ctx.params.id],
    );
    if (!p) throw Object.assign(new Error('not found'), { status: 404 });
    if (p.stock < quantity) throw Object.assign(new Error('insufficient stock'), { status: 409 });
    await tx.execute(
      'UPDATE products SET stock = stock - $1 WHERE id = $2',
      [quantity, p.id],
    );
  });
  ctx.json({ ok: true });
});

serve(app, { port: 8080 });
```
