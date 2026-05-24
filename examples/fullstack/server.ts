/**
 * Symmetra Fullstack Example
 * A complete mini-app: user auth, VRAM sessions, product catalog (PostgreSQL),
 * VRAM cache (KvStore), file uploads, and a static HTML frontend.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';
import { jwt, bcrypt, randomUUID } from '@symmetra/crypto';
import { postgres } from '@symmetra/db';
import { readFile, writeFile, exists, mkdir } from '@symmetra/fs';

// ── Config ────────────────────────────────────────────────────────────────────

const SECRET = process.env.JWT_SECRET ?? 'fullstack-demo-secret';
const PORT   = parseInt(process.env.PORT ?? '8080', 10);
const DB_URL = process.env.DATABASE_URL ?? 'postgresql://localhost/fullstack_demo';

// ── Infrastructure ────────────────────────────────────────────────────────────

const app      = new Router();
const sessions = new KvStore({ namespace: 'sessions', maxMb: 64 });
const cache    = new KvStore({ namespace: 'products',  maxMb: 128 });
const pg       = postgres(DB_URL);

await mkdir('./uploads', { recursive: true });

// ── Types ─────────────────────────────────────────────────────────────────────

interface User     { id: string; username: string; role: string; }
interface Product  { id: string; name: string; price: number; stock: number; category: string; }
interface Session  { userId: string; username: string; role: string; expiresAt: number; }

// ── Auth middleware ───────────────────────────────────────────────────────────

async function getSession(ctx: SymmCtx): Promise<Session | null> {
  const auth = ctx.headers['authorization'] ?? '';
  if (!auth.startsWith('Bearer ')) return null;
  try {
    const payload = await jwt.verify<{ sub: string }>(auth.slice(7), SECRET);
    return await sessions.get<Session>(`session:${payload.sub}`);
  } catch {
    return null;
  }
}

// ── Auth routes ───────────────────────────────────────────────────────────────

app.post('/auth/register', async (ctx) => {
  const { username, password } = await ctx.json<{ username: string; password: string }>();

  if (!username || password.length < 8) {
    ctx.status(400).json({ error: 'username required; password min 8 chars' });
    return;
  }

  const exists_ = await pg.queryOne<{ id: string }>(
    'SELECT id FROM users WHERE username = $1', [username],
  );
  if (exists_) { ctx.status(409).json({ error: 'username taken' }); return; }

  const id     = randomUUID();
  const pwHash = await bcrypt.hash(password, 10);
  await pg.execute(
    'INSERT INTO users (id, username, password_hash, role) VALUES ($1, $2, $3, $4)',
    [id, username, pwHash, 'user'],
  );

  ctx.status(201).json({ id, username });
});

app.post('/auth/login', async (ctx) => {
  const { username, password } = await ctx.json<{ username: string; password: string }>();

  const user = await pg.queryOne<{ id: string; password_hash: string; role: string }>(
    'SELECT id, password_hash, role FROM users WHERE username = $1', [username],
  );
  if (!user || !await bcrypt.verify(password, user.password_hash)) {
    ctx.status(401).json({ error: 'invalid credentials' });
    return;
  }

  const sessionId  = randomUUID();
  const expiresAt  = Date.now() + 8 * 60 * 60 * 1000;  // 8h
  const sessionData: Session = { userId: user.id, username, role: user.role, expiresAt };

  await sessions.set(`session:${sessionId}`, sessionData, { ttlMs: 8 * 60 * 60 * 1000 });

  const token = await jwt.sign({ sub: sessionId }, SECRET, { expiresIn: '8h' });
  ctx.json({ token, expiresAt });
});

app.post('/auth/logout', async (ctx) => {
  const session = await getSession(ctx);
  if (session) {
    const auth = ctx.headers['authorization']!.slice(7);
    const payload = await jwt.verify<{ sub: string }>(auth, SECRET);
    await sessions.del(`session:${payload.sub}`);
  }
  ctx.json({ ok: true });
});

app.get('/auth/me', async (ctx) => {
  const session = await getSession(ctx);
  if (!session) { ctx.status(401).json({ error: 'unauthorized' }); return; }
  ctx.json({ userId: session.userId, username: session.username, role: session.role });
});

// ── Product catalog ───────────────────────────────────────────────────────────

app.get('/products', async (ctx) => {
  const category = ctx.query.category;
  const cacheKey = `list:${category ?? 'all'}`;

  // L1: VRAM cache
  const cached = await cache.get<Product[]>(cacheKey);
  if (cached) { ctx.json({ products: cached, from: 'cache' }); return; }

  const rows = category
    ? await pg.query<Product>('SELECT * FROM products WHERE category = $1 ORDER BY name', [category])
    : await pg.query<Product>('SELECT * FROM products ORDER BY category, name');

  await cache.set(cacheKey, rows, { ttlMs: 30_000 });
  ctx.json({ products: rows, from: 'db' });
});

app.get('/products/:id', async (ctx) => {
  const cached = await cache.get<Product>(ctx.params.id);
  if (cached) { ctx.json({ product: cached, from: 'cache' }); return; }

  const product = await pg.queryOne<Product>('SELECT * FROM products WHERE id = $1', [ctx.params.id]);
  if (!product) { ctx.status(404).json({ error: 'not found' }); return; }

  await cache.set(ctx.params.id, product, { ttlMs: 60_000 });
  ctx.json({ product, from: 'db' });
});

app.post('/products', async (ctx) => {
  const session = await getSession(ctx);
  if (!session || session.role !== 'admin') { ctx.status(403).json({ error: 'forbidden' }); return; }

  const { name, price, stock, category } = await ctx.json<Omit<Product, 'id'>>();
  const id = randomUUID();
  await pg.execute(
    'INSERT INTO products (id, name, price, stock, category) VALUES ($1, $2, $3, $4, $5)',
    [id, name, price, stock, category],
  );
  await cache.del(id);
  await cache.del('list:all');
  await cache.del(`list:${category}`);

  ctx.status(201).json({ id, name, price, stock, category });
});

// ── File upload ───────────────────────────────────────────────────────────────

app.put('/upload/:name', async (ctx) => {
  const session = await getSession(ctx);
  if (!session) { ctx.status(401).json({ error: 'unauthorized' }); return; }

  const name = ctx.params.name;
  if (name.includes('/') || name.includes('..')) {
    ctx.status(400).json({ error: 'invalid filename' });
    return;
  }

  const content = await ctx.text();
  await writeFile(`./uploads/${name}`, content);
  ctx.status(201).json({ ok: true, name, uploadedBy: session.username });
});

app.get('/upload/:name', async (ctx) => {
  const path = `./uploads/${ctx.params.name}`;
  if (!await exists(path)) { ctx.status(404).json({ error: 'not found' }); return; }
  const content = await readFile(path, 'utf8');
  ctx.text(content);
});

// ── Stats ─────────────────────────────────────────────────────────────────────

app.get('/stats', async (ctx) => {
  const session = await getSession(ctx);
  if (!session || session.role !== 'admin') { ctx.status(403).json({ error: 'forbidden' }); return; }

  const [userCount, productCount, sessionStats, cacheStats] = await Promise.all([
    pg.queryOne<{ count: string }>('SELECT COUNT(*) as count FROM users'),
    pg.queryOne<{ count: string }>('SELECT COUNT(*) as count FROM products'),
    sessions.stats(),
    cache.stats(),
  ]);

  ctx.json({
    users:    parseInt(userCount?.count ?? '0', 10),
    products: parseInt(productCount?.count ?? '0', 10),
    sessions: sessionStats,
    cache:    cacheStats,
  });
});

// ── Static HTML ───────────────────────────────────────────────────────────────

app.get('/', async (ctx) => {
  ctx.text(`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Symmetra Fullstack Demo</title>
  <style>
    body { font-family: system-ui, sans-serif; max-width: 640px; margin: 2rem auto; padding: 1rem; }
    h1 { color: #0f172a; } code { background: #f1f5f9; padding: 2px 6px; border-radius: 4px; }
    .endpoint { margin: .5rem 0; padding: .5rem; background: #f8fafc; border-left: 3px solid #6366f1; }
  </style>
</head>
<body>
  <h1>⚡ Symmetra Fullstack Demo</h1>
  <p>A GPU-native server with JWT auth, VRAM sessions, PostgreSQL, and file uploads.</p>
  <h2>API Endpoints</h2>
  <div class="endpoint"><code>POST /auth/register</code> — create account</div>
  <div class="endpoint"><code>POST /auth/login</code> — get JWT token</div>
  <div class="endpoint"><code>GET  /auth/me</code> — current user (requires Bearer token)</div>
  <div class="endpoint"><code>GET  /products</code> — list products (VRAM-cached)</div>
  <div class="endpoint"><code>GET  /products/:id</code> — single product</div>
  <div class="endpoint"><code>PUT  /upload/:name</code> — upload a file (requires auth)</div>
  <div class="endpoint"><code>GET  /stats</code> — admin stats (requires admin role)</div>
  <h2>Quick Start</h2>
  <pre><code>curl -X POST http://localhost:${PORT}/auth/register \\
  -H 'Content-Type: application/json' \\
  -d '{"username":"alice","password":"password123"}'

TOKEN=$(curl -sX POST http://localhost:${PORT}/auth/login \\
  -H 'Content-Type: application/json' \\
  -d '{"username":"alice","password":"password123"}' | jq -r .token)

curl http://localhost:${PORT}/auth/me -H "Authorization: Bearer $TOKEN"</code></pre>
</body>
</html>`, 'text/html');
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: PORT });
