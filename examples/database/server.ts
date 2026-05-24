/**
 * Symmetra Database Example
 * Demonstrates: PostgreSQL queries, transactions, SQLite, Redis caching.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { postgres, sqlite, redis } from '@symmetra/db';
import { KvStore } from '@symmetra/kv';

const app = new Router();

// ── PostgreSQL ────────────────────────────────────────────────────────────────

const pg = postgres(process.env.DATABASE_URL ?? 'postgresql://localhost/demo');

interface User { id: number; name: string; email: string; }

app.get('/users', async (ctx) => {
  const users = await pg.query<User>('SELECT id, name, email FROM users ORDER BY name');
  ctx.json(users);
});

app.get('/users/:id', async (ctx) => {
  const user = await pg.queryOne<User>(
    'SELECT id, name, email FROM users WHERE id = $1',
    [ctx.params.id],
  );
  if (!user) { ctx.status(404).json({ error: 'not found' }); return; }
  ctx.json(user);
});

app.post('/users', async (ctx) => {
  const { name, email } = await ctx.json<{ name: string; email: string }>();
  const row = await pg.queryOne<{ id: number }>(
    'INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id',
    [name, email],
  );
  ctx.status(201).json({ id: row!.id, name, email });
});

// ── Transactions ──────────────────────────────────────────────────────────────

app.post('/transfer', async (ctx) => {
  const { from, to, amount } = await ctx.json<{ from: number; to: number; amount: number }>();

  await pg.transaction(async (tx) => {
    const sender = await tx.queryOne<{ balance: number }>(
      'SELECT balance FROM wallets WHERE user_id = $1 FOR UPDATE', [from],
    );
    if (!sender || sender.balance < amount) {
      throw Object.assign(new Error('insufficient funds'), { status: 400 });
    }
    await tx.execute('UPDATE wallets SET balance = balance - $1 WHERE user_id = $2', [amount, from]);
    await tx.execute('UPDATE wallets SET balance = balance + $1 WHERE user_id = $2', [amount, to]);
    await tx.execute(
      'INSERT INTO ledger (from_id, to_id, amount) VALUES ($1, $2, $3)',
      [from, to, amount],
    );
  });

  ctx.json({ ok: true, transferred: amount });
});

// ── SQLite (embedded, no external process) ────────────────────────────────────

const db = sqlite('./local.db');

// Create table on startup
db.execute(`
  CREATE TABLE IF NOT EXISTS notes (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    body  TEXT NOT NULL
  )
`);

interface Note { id: number; title: string; body: string; }

app.get('/notes', (ctx) => {
  const notes = db.query<Note>('SELECT * FROM notes ORDER BY id DESC');
  ctx.json(notes);
});

app.post('/notes', async (ctx) => {
  const { title, body } = await ctx.json<{ title: string; body: string }>();
  const { lastInsertId } = db.execute(
    'INSERT INTO notes (title, body) VALUES (?, ?)', [title, body],
  );
  ctx.status(201).json({ id: lastInsertId, title, body });
});

app.delete('/notes/:id', (ctx) => {
  db.execute('DELETE FROM notes WHERE id = ?', [ctx.params.id]);
  ctx.status(204).text('');
});

// ── Redis + VRAM cache (L1 = VRAM, L2 = Redis) ────────────────────────────────

const r   = redis(process.env.REDIS_URL ?? 'redis://localhost:6379');
const kv  = new KvStore({ namespace: 'products', maxMb: 64 });

app.get('/products/:id', async (ctx) => {
  const id = ctx.params.id;

  // L1: check VRAM
  const cached = await kv.get<{ id: string; name: string; price: number }>(id);
  if (cached) { ctx.json({ ...cached, from: 'vram' }); return; }

  // L2: check Redis
  const rval = await r.get(`product:${id}`);
  if (rval) {
    const product = JSON.parse(rval);
    await kv.set(id, product, { ttlMs: 5_000 });   // re-warm VRAM cache
    ctx.json({ ...product, from: 'redis' });
    return;
  }

  // Miss: fetch from DB and populate caches
  const product = await pg.queryOne<{ id: string; name: string; price: number }>(
    'SELECT id::text, name, price FROM products WHERE id = $1', [id],
  );
  if (!product) { ctx.status(404).json({ error: 'not found' }); return; }

  await r.set(`product:${id}`, JSON.stringify(product), { ex: 300 });  // 5 min Redis TTL
  await kv.set(id, product, { ttlMs: 5_000 });                          // 5 s VRAM TTL

  ctx.json({ ...product, from: 'db' });
});

// ── Counters (Redis INCR) ─────────────────────────────────────────────────────

app.post('/counter/:key/incr', async (ctx) => {
  const count = await r.incr(`counter:${ctx.params.key}`);
  ctx.json({ key: ctx.params.key, count });
});

app.get('/counter/:key', async (ctx) => {
  const val = await r.get(`counter:${ctx.params.key}`);
  ctx.json({ key: ctx.params.key, count: val !== null ? parseInt(val, 10) : 0 });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
