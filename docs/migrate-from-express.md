# Migrating from Express to Symmetra

This guide walks through converting an Express app to Symmetra. The HTTP surface is deliberately similar — most routes convert with minimal changes.

---

## Overview

| Concept | Express | Symmetra |
|---------|---------|----------|
| App object | `express()` | `new Router()` |
| Start server | `app.listen(port)` | `serve(app, { port })` |
| Handler signature | `(req, res, next)` | `(ctx)` |
| JSON response | `res.json(obj)` | `ctx.json(obj)` |
| Text response | `res.send(text)` | `ctx.text(text)` |
| Status code | `res.status(404).json(...)` | `ctx.status(404).json(...)` |
| Path params | `req.params.id` | `ctx.params.id` |
| Query params | `req.query.q` | `ctx.query.q` |
| Request headers | `req.headers.authorization` | `ctx.headers['authorization']` |
| Read JSON body | `req.body` (after `express.json()`) | `await ctx.json()` |
| Read raw body | `req.body` (after raw middleware) | `await ctx.text()` or `await ctx.body()` |
| Middleware | `app.use(fn)` | (not yet supported — inline auth helpers instead) |
| Static files | `express.static(dir)` | `await readFile(path, 'utf8')` → `ctx.text(content)` |

---

## Side-by-side examples

### Hello world

**Express**
```typescript
import express from 'express';
const app = express();
app.get('/ping', (req, res) => {
  res.json({ message: 'pong' });
});
app.listen(3000);
```

**Symmetra**
```typescript
import { serve, Router } from '@symmetra/http';
const app = new Router();
app.get('/ping', (ctx) => {
  ctx.json({ message: 'pong' });
});
serve(app, { port: 3000 });
```

---

### Reading the request body

**Express**
```typescript
app.use(express.json());
app.post('/users', (req, res) => {
  const { name, email } = req.body;
  res.status(201).json({ name, email });
});
```

**Symmetra**
```typescript
app.post('/users', async (ctx) => {
  const { name, email } = await ctx.json<{ name: string; email: string }>();
  ctx.status(201).json({ name, email });
});
```

No `express.json()` middleware needed — body parsing is built in.

---

### Path parameters

**Express**
```typescript
app.get('/users/:id', (req, res) => {
  res.json({ id: req.params.id });
});
```

**Symmetra**
```typescript
app.get('/users/:id', (ctx) => {
  ctx.json({ id: ctx.params.id });
});
```

---

### Setting headers

**Express**
```typescript
app.get('/data', (req, res) => {
  res.set('X-Cache', 'miss').json({ data: '...' });
});
```

**Symmetra**
```typescript
app.get('/data', (ctx) => {
  ctx.header('X-Cache', 'miss').json({ data: '...' });
});
```

---

### Error handling

**Express**
```typescript
app.get('/risky', (req, res, next) => {
  try {
    const data = riskyOperation();
    res.json(data);
  } catch (err) {
    next(err);
  }
});
app.use((err, req, res, next) => {
  res.status(500).json({ error: err.message });
});
```

**Symmetra** — no global error handler; handle in the route:
```typescript
app.get('/risky', (ctx) => {
  try {
    const data = riskyOperation();
    ctx.json(data);
  } catch (err) {
    ctx.status(500).json({ error: (err as Error).message });
  }
});
```

---

### Middleware → inline helper

Express middleware can usually become a helper function:

**Express**
```typescript
function requireAuth(req, res, next) {
  const token = req.headers.authorization?.replace('Bearer ', '');
  if (!token) return res.status(401).json({ error: 'unauthorized' });
  try {
    req.user = jwt.verify(token, SECRET);
    next();
  } catch {
    res.status(401).json({ error: 'invalid token' });
  }
}
app.get('/me', requireAuth, (req, res) => {
  res.json({ user: req.user });
});
```

**Symmetra**
```typescript
async function requireAuth(ctx: SymmCtx) {
  const token = ctx.headers['authorization']?.replace('Bearer ', '');
  if (!token) { ctx.status(401).json({ error: 'unauthorized' }); return null; }
  try {
    return await jwt.verify<User>(token, SECRET);
  } catch {
    ctx.status(401).json({ error: 'invalid token' });
    return null;
  }
}

app.get('/me', async (ctx) => {
  const user = await requireAuth(ctx);
  if (!user) return;   // response already sent
  ctx.json({ user });
});
```

---

### CORS headers

**Express**
```typescript
import cors from 'cors';
app.use(cors({ origin: 'https://myapp.com' }));
```

**Symmetra** — add to each route or create a helper:
```typescript
function setCors(ctx: SymmCtx, origin = '*') {
  ctx.header('Access-Control-Allow-Origin', origin);
  ctx.header('Access-Control-Allow-Methods', 'GET,POST,PUT,DELETE,OPTIONS');
  ctx.header('Access-Control-Allow-Headers', 'Content-Type,Authorization');
}

app.get('/api/data', (ctx) => {
  setCors(ctx, 'https://myapp.com');
  ctx.json({ data: '...' });
});
```

---

### Sessions

**Express** with `express-session` uses a cookie-backed session store. In Symmetra, use `KvStore` (VRAM-backed) + JWT:

```typescript
import { KvStore } from '@symmetra/kv';
import { jwt, randomUUID } from '@symmetra/crypto';

const sessions = new KvStore({ namespace: 'sessions', maxMb: 128 });
const SECRET = process.env.SESSION_SECRET!;

// Login: create session
const sessionId = randomUUID();
await sessions.set(sessionId, { userId, username }, { ttlMs: 8 * 60 * 60 * 1000 });
const token = await jwt.sign({ sub: sessionId }, SECRET, { expiresIn: '8h' });
ctx.json({ token });

// Verify: decode token → look up session
const { sub } = await jwt.verify<{ sub: string }>(token, SECRET);
const session = await sessions.get(sub);
```

---

### Database access

**Express** (with `pg`):
```typescript
import { Pool } from 'pg';
const pool = new Pool({ connectionString: process.env.DATABASE_URL });

app.get('/users', async (req, res) => {
  const { rows } = await pool.query('SELECT * FROM users');
  res.json(rows);
});
```

**Symmetra** (with `@symmetra/db`):
```typescript
import { postgres } from '@symmetra/db';
const pg = postgres(process.env.DATABASE_URL!);

app.get('/users', async (ctx) => {
  const rows = await pg.query<User>('SELECT * FROM users');
  ctx.json(rows);
});
```

---

## What is NOT supported

| Express feature | Status | Workaround |
|----------------|--------|------------|
| `app.use()` global middleware | 🔲 | Inline helper functions |
| `Router.route('/path')` chaining | 🔲 | Register each method separately |
| `res.redirect()` | 🔲 | `ctx.header('Location', url).status(302).text('')` |
| `res.sendFile()` | 🔲 | `readFile()` + `ctx.text(content, mimeType)` |
| `res.download()` | 🔲 | `readFile()` + `ctx.header('Content-Disposition', ...)` |
| `app.set('view engine', ...)` | 🔲 | Render template string in handler, return HTML |
| `express.static()` | 🔲 | Manual `readFile()` per request |
| WebSocket (`ws` library) | ⚠️ Planned | Use `upgrade(ctx)` when WebSocket support ships |
| Streaming response | ✅ | `GpuReadable` / `GpuWritable` / `pipeline` |

---

## Performance expectations

Symmetra is not just a drop-in replacement — it is significantly faster for compute-heavy routes:

| Route type | Express (Node 20) | Symmetra |
|-----------|-------------------|---------|
| Static JSON (`/ping`) | ~50k req/s | ~800k req/s |
| Array transform (1024 floats, `x*x`) | ~8k req/s | ~2M elements/s on GPU |
| KV get (in-memory) | ~100k req/s (Redis) | ~3M req/s (VRAM) |
| JWT verify | ~50k req/s | ~400k req/s |

The GPU shines on compute routes. Pure-I/O routes (DB queries, upstream fetch) see smaller gains since the bottleneck is network, not CPU.
