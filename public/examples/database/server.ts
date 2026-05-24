/**
 * Symmetra — database example
 * Demonstrates PostgreSQL queries, Redis caching, and SQLite.
 *
 * Run: sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';
import { postgres, sqlite, redis } from '@symmetra/db';
import { env } from '@symmetra/env';

// ── Config ────────────────────────────────────────────────────────────────────

const cfg = env.load({
  DATABASE_URL: { type: 'string', required: true },
  REDIS_URL:    { type: 'string', default: 'redis://localhost:6379' },
  PORT:         { type: 'port',   default: 8080 },
} as const);

// ── Clients ───────────────────────────────────────────────────────────────────

const pg = postgres({
  host:     'localhost',
  database: 'myapp',
  user:     env.require('DB_USER'),
  password: env.require('DB_PASS'),
});

const sq = sqlite({ path: './local.db', walMode: true });

const rd = redis({ host: 'localhost', port: 6379 });

const kv = new KvStore({ maxMb: 256 });

// ── App ───────────────────────────────────────────────────────────────────────

const app = new Router();

// List users — with Redis cache
app.get('/users', async (ctx) => {
  const cached = await rd.get('users:all');
  if (cached) {
    ctx.json(JSON.parse(cached));
    return;
  }

  const result = await pg.query<{ id: number; name: string; email: string }>(
    'SELECT id, name, email FROM users ORDER BY id LIMIT 100'
  );

  await rd.set('users:all', JSON.stringify(result.rows), { ex: 60 });
  ctx.json(result.rows);
});

// Get single user — from VRAM cache first, then Postgres
app.get('/users/:id', async (ctx) => {
  const id = parseInt(ctx.params.id, 10);

  const cachedUser = await kv.get<{ id: number; name: string }>(`user:${id}`);
  if (cachedUser) {
    ctx.json(cachedUser);
    return;
  }

  const row = await pg.queryOne<{ id: number; name: string; email: string }>(
    'SELECT id, name, email FROM users WHERE id = $1',
    [id]
  );

  if (!row) {
    ctx.status(404).json({ error: 'user not found' });
    return;
  }

  await kv.set(`user:${id}`, row, { ttlMs: 30_000 });
  ctx.json(row);
});

// Create user — invalidate cache
app.post('/users', async (ctx) => {
  const body = await ctx.json<{ name: string; email: string }>();

  const result = await pg.query<{ id: number }>(
    'INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id',
    [body.name, body.email]
  );

  await rd.del('users:all');

  ctx.status(201).json({ id: result.rows[0].id, ...body });
});

// Batch upsert via prepared statement
const upsertStmt = await pg.prepare(
  'upsert_user',
  `INSERT INTO users (id, name, email)
   VALUES ($1, $2, $3)
   ON CONFLICT (id) DO UPDATE SET name = $2, email = $3`
);

app.put('/users/bulk', async (ctx) => {
  const users = await ctx.json<{ id: number; name: string; email: string }[]>();

  for (const u of users) {
    await upsertStmt.execute(u.id, u.name, u.email);
  }

  await rd.del('users:all');
  ctx.json({ updated: users.length });
});

// SQLite example — local settings store
app.get('/settings', async (ctx) => {
  const rows = await sq.query<{ key: string; value: string }>(
    'SELECT key, value FROM settings'
  );
  const settings = Object.fromEntries(rows.rows.map(r => [r.key, r.value]));
  ctx.json(settings);
});

app.put('/settings/:key', async (ctx) => {
  const { key } = ctx.params;
  const { value } = await ctx.json<{ value: string }>();

  await sq.execute(
    'INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)',
    [key, value]
  );
  ctx.json({ ok: true });
});

// Redis pipeline — atomic multi-key ops
app.post('/events', async (ctx) => {
  const event = await ctx.json<{ type: string; userId: number }>();

  await rd.pipeline()
    .incr(`events:count:${event.type}`)
    .lpush(`events:user:${event.userId}`, JSON.stringify(event))
    .expire(`events:user:${event.userId}`, 86400)
    .exec();

  ctx.json({ queued: true });
});

serve(app, { port: cfg.PORT });
