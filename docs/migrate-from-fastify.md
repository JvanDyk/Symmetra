# Migrating from Fastify to Symmetra

This guide walks through converting a Fastify app to Symmetra. Both frameworks use a similar routing model — the main differences are the context API and middleware pattern.

---

## Overview

| Concept | Fastify | Symmetra |
|---------|---------|----------|
| App object | `fastify()` | `new Router()` |
| Start server | `fastify.listen({ port })` | `serve(app, { port })` |
| Handler signature | `(request, reply)` | `(ctx)` |
| JSON response | `reply.send(obj)` | `ctx.json(obj)` |
| Text response | `reply.type('text/plain').send(text)` | `ctx.text(text)` |
| Status code | `reply.status(404).send(...)` | `ctx.status(404).json(...)` |
| Path params | `request.params.id` | `ctx.params.id` |
| Query params | `request.query.q` | `ctx.query.q` |
| Request headers | `request.headers.authorization` | `ctx.headers['authorization']` |
| Read JSON body | `request.body` (auto-parsed) | `await ctx.json()` |
| Plugins | `fastify.register(plugin)` | (not yet supported — inline helpers) |
| Lifecycle hooks | `fastify.addHook('onRequest', ...)` | Inline guard at top of handler |
| Schema validation | `{ schema: { body: { ... } } }` | TypeScript types + manual validation |
| Decorators | `fastify.decorate('db', ...)` | Module-level constants |

---

## Side-by-side examples

### Hello world

**Fastify**
```typescript
import Fastify from 'fastify';
const fastify = Fastify();
fastify.get('/ping', () => ({ message: 'pong' }));
await fastify.listen({ port: 3000 });
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

### Request body

**Fastify**
```typescript
fastify.post('/users', async (request, reply) => {
  const { name, email } = request.body as { name: string; email: string };
  reply.status(201).send({ name, email });
});
```

**Symmetra**
```typescript
app.post('/users', async (ctx) => {
  const { name, email } = await ctx.json<{ name: string; email: string }>();
  ctx.status(201).json({ name, email });
});
```

---

### Path and query parameters

**Fastify**
```typescript
fastify.get('/search', async (request, reply) => {
  const { q, page } = request.query as { q: string; page?: string };
  const { id } = request.params as { id: string };
  reply.send({ id, q, page });
});
```

**Symmetra**
```typescript
app.get('/items/:id', async (ctx) => {
  const { id } = ctx.params;
  const { q, page } = ctx.query;
  ctx.json({ id, q, page });
});
```

---

### JSON Schema validation → TypeScript + manual guard

**Fastify** with built-in schema validation:
```typescript
const schema = {
  body: {
    type: 'object',
    required: ['name', 'price'],
    properties: {
      name:  { type: 'string', minLength: 1 },
      price: { type: 'number', minimum: 0 },
    },
  },
};
fastify.post('/products', { schema }, async (request, reply) => {
  const { name, price } = request.body as { name: string; price: number };
  reply.status(201).send({ name, price });
});
```

**Symmetra** — validate inline:
```typescript
app.post('/products', async (ctx) => {
  const body = await ctx.json<{ name?: string; price?: number }>();
  if (!body.name || typeof body.price !== 'number' || body.price < 0) {
    ctx.status(400).json({ error: 'name required; price must be non-negative number' });
    return;
  }
  ctx.status(201).json({ name: body.name, price: body.price });
});
```

Or extract a reusable validate helper:
```typescript
function validate<T>(body: unknown, check: (b: T) => string | null): [T, null] | [null, string] {
  const error = check(body as T);
  return error ? [null, error] : [body as T, null];
}
```

---

### Plugins → module-level setup

**Fastify** plugin:
```typescript
import fp from 'fastify-plugin';
async function dbPlugin(fastify: FastifyInstance) {
  const pool = new Pool({ connectionString: process.env.DATABASE_URL });
  fastify.decorate('db', pool);
}
fastify.register(fp(dbPlugin));

fastify.get('/users', async (request) => {
  const { rows } = await request.server.db.query('SELECT * FROM users');
  return rows;
});
```

**Symmetra** — create at module level:
```typescript
import { postgres } from '@symmetra/db';
const pg = postgres(process.env.DATABASE_URL!);

app.get('/users', async (ctx) => {
  const rows = await pg.query<User>('SELECT * FROM users');
  ctx.json(rows);
});
```

---

### Lifecycle hooks → inline guards

**Fastify** `onRequest` hook:
```typescript
fastify.addHook('onRequest', async (request, reply) => {
  if (!request.headers.authorization) {
    reply.status(401).send({ error: 'unauthorized' });
  }
});
```

**Symmetra** — call a guard at the top of each protected route:
```typescript
async function requireAuth(ctx: SymmCtx): Promise<Session | null> {
  const token = (ctx.headers['authorization'] ?? '').replace('Bearer ', '');
  if (!token) { ctx.status(401).json({ error: 'unauthorized' }); return null; }
  try {
    return await jwt.verify<Session>(token, SECRET);
  } catch {
    ctx.status(401).json({ error: 'invalid token' });
    return null;
  }
}

app.get('/me', async (ctx) => {
  const session = await requireAuth(ctx);
  if (!session) return;
  ctx.json({ user: session.username });
});
```

---

### Fastify plugins that map to Symmetra packages

| Fastify plugin | Symmetra equivalent |
|---------------|---------------------|
| `@fastify/postgres` | `import { postgres } from '@symmetra/db'` |
| `@fastify/redis` | `import { redis } from '@symmetra/db'` |
| `@fastify/jwt` | `import { jwt } from '@symmetra/crypto'` |
| `@fastify/cors` | Inline `ctx.header('Access-Control-Allow-Origin', '*')` |
| `@fastify/multipart` | `await ctx.body<Uint8Array>()` + boundary parsing |
| `@fastify/static` | `readFile()` + `ctx.text(content, mimeType)` |
| `@fastify/compress` | (not yet built-in) |
| `@fastify/rate-limit` | (planned — token bucket in VRAM) |
| `@fastify/websocket` | (planned) |

---

### TypeBox schema → plain TypeScript types

**Fastify + TypeBox**
```typescript
import { Type } from '@sinclair/typebox';
const Body = Type.Object({
  name:  Type.String({ minLength: 1 }),
  email: Type.String({ format: 'email' }),
});
fastify.post('/register', { schema: { body: Body } }, async (req) => {
  const { name, email } = req.body;
  return { name, email };
});
```

**Symmetra** — TypeScript interface + runtime guard:
```typescript
interface RegisterBody { name: string; email: string; }

function isRegisterBody(b: unknown): b is RegisterBody {
  const body = b as RegisterBody;
  return typeof body.name === 'string' && body.name.length > 0
    && typeof body.email === 'string' && body.email.includes('@');
}

app.post('/register', async (ctx) => {
  const body = await ctx.json();
  if (!isRegisterBody(body)) {
    ctx.status(400).json({ error: 'name and valid email required' });
    return;
  }
  ctx.json({ name: body.name, email: body.email });
});
```

---

### Serialization

**Fastify** uses a fast JSON serializer driven by schema. Symmetra uses the GPU's JSON serializer kernel (`symm_json_str`). For best performance, pass plain objects directly to `ctx.json()` — avoid unnecessary intermediate serialization.

---

## What is NOT supported

| Fastify feature | Status | Workaround |
|----------------|--------|------------|
| `fastify.register()` plugins | 🔲 | Module-level setup + inline helpers |
| `addHook` lifecycle hooks | 🔲 | Guard function at top of handler |
| `reply.hijack()` | 🔲 | Not applicable (GPU handles response) |
| `fastify.inject()` for testing | 🔲 | Use `curl` or `fetch` integration tests |
| Content-type auto-detection | 🔲 | Specify `ctx.text(body, mimeType)` explicitly |
| Route prefix via `fastify.register(..., { prefix })` | 🔲 | Spell out full paths |
| `reply.raw` (Node IncomingMessage) | 🔲 | No raw socket access — GPU pipeline handles I/O |

---

## Performance expectations

| Route type | Fastify (Node 20) | Symmetra |
|-----------|-------------------|---------|
| Static JSON (`/ping`) | ~90k req/s | ~800k req/s |
| JSON body parse + echo | ~60k req/s | ~600k req/s |
| Array transform (1024 floats) | ~10k req/s | ~2M elements/s GPU |
| JWT verify | ~80k req/s | ~400k req/s |
| PostgreSQL query (local) | ~15k req/s | ~15k req/s (DB is the bottleneck) |

Fastify is already the fastest Node.js framework — Symmetra's gains come from moving the handler kernel to the GPU and using VRAM for all intermediate data.
