/**
 * Symmetra — full-stack example
 * A task management API combining: database, caching, auth, file uploads,
 * env config, and outbound HTTP. Shows how all the packages work together.
 *
 * Run: DATABASE_URL=postgres://... JWT_SECRET=... sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { postgres, redis } from '@symmetra/db';
import { fs, jsonFs } from '@symmetra/fs';
import { jwt, password, random, hash } from '@symmetra/crypto';
import { KvStore } from '@symmetra/kv';
import { env } from '@symmetra/env';
import { http } from '@symmetra/net';

// ── Config ────────────────────────────────────────────────────────────────────

const cfg = env.load({
  DATABASE_URL:   { type: 'string',  required: true },
  JWT_SECRET:     { type: 'string',  required: true },
  REDIS_HOST:     { type: 'string',  default: 'localhost' },
  REDIS_PORT:     { type: 'port',    default: 6379 },
  WEBHOOK_URL:    { type: 'string',  default: '' },
  UPLOAD_DIR:     { type: 'string',  default: './uploads' },
  PORT:           { type: 'port',    default: 8080 },
} as const);

// ── Database & cache clients ──────────────────────────────────────────────────

const pg = postgres({
  host:     'localhost',
  database: 'taskapp',
  user:     env.require('DB_USER'),
  password: env.require('DB_PASS'),
});

const rd = redis({ host: cfg.REDIS_HOST, port: cfg.REDIS_PORT });

const kv  = new KvStore({ maxMb: 256 });

await fs.mkdir(cfg.UPLOAD_DIR, { recursive: true });

// ── Types ─────────────────────────────────────────────────────────────────────

interface User    { id: number; email: string; name: string }
interface Task    { id: number; userId: number; title: string; done: boolean; createdAt: string; attachments: string[] }
interface Comment { id: number; taskId: number; userId: number; text: string; createdAt: string }

// ── Auth helpers ──────────────────────────────────────────────────────────────

async function getUser(ctx: any): Promise<User | null> {
  const authHeader = ctx.header('authorization') as string | undefined;
  if (!authHeader?.startsWith('Bearer ')) return null;

  try {
    const payload = await jwt.verify<{ sub: string; email: string; name: string }>(
      authHeader.slice(7), cfg.JWT_SECRET
    );
    return { id: parseInt(payload.sub, 10), email: payload.email, name: payload.name };
  } catch {
    return null;
  }
}

// ── App ───────────────────────────────────────────────────────────────────────

const app = new Router();

// ── Auth endpoints ────────────────────────────────────────────────────────────

app.post('/auth/register', async (ctx) => {
  const { email, name, pass } = await ctx.json<{ email: string; name: string; pass: string }>();

  if (!email || !name || !pass || pass.length < 8)  {
    ctx.status(400).json({ error: 'email, name, and password (min 8 chars) required' });
    return;
  }

  if (await pg.queryOne('SELECT 1 FROM users WHERE email = $1', [email])) {
    ctx.status(409).json({ error: 'email taken' });
    return;
  }

  const hashed = await password.hash(pass);
  const row = await pg.queryOne<{ id: number }>(
    'INSERT INTO users (email, name, password_hash) VALUES ($1, $2, $3) RETURNING id',
    [email, name, hashed]
  );

  ctx.status(201).json({ id: row!.id, email, name });
});

app.post('/auth/login', async (ctx) => {
  const { email, pass } = await ctx.json<{ email: string; pass: string }>();

  const user = await pg.queryOne<User & { password_hash: string }>(
    'SELECT id, email, name, password_hash FROM users WHERE email = $1',
    [email]
  );

  if (!user || !await password.verify(pass, user.password_hash)) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }

  const token = await jwt.sign(
    { sub: String(user.id), email: user.email, name: user.name },
    cfg.JWT_SECRET,
    { expiresIn: 86400 }
  );

  await rd.set(`session:${user.id}`, token, { ex: 86400 });
  ctx.json({ token });
});

app.post('/auth/logout', async (ctx) => {
  const user = await getUser(ctx);
  if (user) await rd.del(`session:${user.id}`);
  ctx.json({ ok: true });
});

// ── Tasks ─────────────────────────────────────────────────────────────────────

app.get('/tasks', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const cacheKey = `tasks:${user.id}`;
  const cached = await kv.get<Task[]>(cacheKey);
  if (cached) { ctx.json(cached); return; }

  const result = await pg.query<Task>(
    'SELECT id, title, done, created_at as "createdAt" FROM tasks WHERE user_id = $1 ORDER BY created_at DESC',
    [user.id]
  );

  await kv.set(cacheKey, result.rows, { ttlMs: 30_000 });
  ctx.json(result.rows);
});

app.post('/tasks', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const { title } = await ctx.json<{ title: string }>();
  if (!title?.trim()) { ctx.status(400).json({ error: 'title required' }); return; }

  const row = await pg.queryOne<{ id: number; created_at: string }>(
    'INSERT INTO tasks (user_id, title) VALUES ($1, $2) RETURNING id, created_at',
    [user.id, title.trim()]
  );

  await kv.delete(`tasks:${user.id}`);

  const task: Task = { id: row!.id, userId: user.id, title, done: false, createdAt: row!.created_at, attachments: [] };
  ctx.status(201).json(task);

  if (cfg.WEBHOOK_URL) {
    http.post(cfg.WEBHOOK_URL, { event: 'task.created', task }).catch(() => {});
  }
});

app.patch('/tasks/:id', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const taskId = parseInt(ctx.params.id, 10);
  const { done, title } = await ctx.json<{ done?: boolean; title?: string }>();

  const existing = await pg.queryOne<{ user_id: number }>(
    'SELECT user_id FROM tasks WHERE id = $1',
    [taskId]
  );

  if (!existing || existing.user_id !== user.id) {
    ctx.status(404).json({ error: 'task not found' });
    return;
  }

  await pg.query(
    'UPDATE tasks SET done = COALESCE($1, done), title = COALESCE($2, title) WHERE id = $3',
    [done ?? null, title ?? null, taskId]
  );

  await kv.delete(`tasks:${user.id}`);
  ctx.json({ ok: true });
});

app.delete('/tasks/:id', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const taskId = parseInt(ctx.params.id, 10);
  await pg.query('DELETE FROM tasks WHERE id = $1 AND user_id = $2', [taskId, user.id]);
  await kv.delete(`tasks:${user.id}`);
  ctx.json({ ok: true });
});

// ── File attachments ──────────────────────────────────────────────────────────

app.post('/tasks/:id/attach', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const taskId   = parseInt(ctx.params.id, 10);
  const filename = ctx.header('x-filename') as string | undefined;

  if (!filename) { ctx.status(400).json({ error: 'x-filename header required' }); return; }

  const data   = await ctx.body<Uint8Array>();
  const digest = await hash.hash('sha256', data);
  const name   = `${digest.slice(0, 16)}-${filename}`;
  const path   = `${cfg.UPLOAD_DIR}/${name}`;

  await fs.write(path, data);

  await pg.query(
    'INSERT INTO task_attachments (task_id, filename, path) VALUES ($1, $2, $3)',
    [taskId, filename, path]
  );

  ctx.json({ name, path });
});

// ── Comments ──────────────────────────────────────────────────────────────────

app.get('/tasks/:id/comments', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const taskId = parseInt(ctx.params.id, 10);
  const result = await pg.query<Comment>(
    'SELECT id, task_id as "taskId", user_id as "userId", text, created_at as "createdAt" FROM comments WHERE task_id = $1 ORDER BY created_at',
    [taskId]
  );

  ctx.json(result.rows);
});

app.post('/tasks/:id/comments', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const taskId = parseInt(ctx.params.id, 10);
  const { text } = await ctx.json<{ text: string }>();

  const row = await pg.queryOne<Comment>(
    'INSERT INTO comments (task_id, user_id, text) VALUES ($1, $2, $3) RETURNING id, created_at as "createdAt"',
    [taskId, user.id, text]
  );

  ctx.status(201).json({ ...row, taskId, userId: user.id, text });
});

// ── Stats dashboard ───────────────────────────────────────────────────────────

app.get('/stats', async (ctx) => {
  const user = await getUser(ctx);
  if (!user) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const [totalRes, doneRes, overdueRes] = await Promise.all([
    pg.queryOne<{ count: string }>('SELECT COUNT(*) as count FROM tasks WHERE user_id = $1', [user.id]),
    pg.queryOne<{ count: string }>('SELECT COUNT(*) as count FROM tasks WHERE user_id = $1 AND done = true', [user.id]),
    pg.queryOne<{ count: string }>('SELECT COUNT(*) as count FROM tasks WHERE user_id = $1 AND done = false AND created_at < NOW() - INTERVAL \'7 days\'', [user.id]),
  ]);

  ctx.json({
    total:   parseInt(totalRes!.count, 10),
    done:    parseInt(doneRes!.count, 10),
    overdue: parseInt(overdueRes!.count, 10),
  });
});

serve(app, { port: cfg.PORT });
