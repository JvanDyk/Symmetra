// Symmetra HTTP Server Example
// Demonstrates: routing, GPU compute, VRAM cache, GPU fingerprinting.
// Run: sym ./server.ts

import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';

const app = new Router();
const kv  = new KvStore({ namespace: 'app', maxMb: 256 });

// ── Static routes ─────────────────────────────────────────────────────────────

app.get('/health', (ctx) => {
  ctx.json({ status: 'ok', gpu: true });
});

// ── GPU compute route ─────────────────────────────────────────────────────────
// POST /transform   body: Float32Array (binary or JSON array)
// Applies x² + sin(x) to every element in parallel on GPU.
// Each element gets its own CUDA thread — no CPU involvement.

app.post('/transform', async (ctx) => {
  const data   = await ctx.body<Float32Array>();
  const result = data.map(x => x * x + Math.sin(x));
  ctx.json(result);
});

// ── GPU fingerprint route ─────────────────────────────────────────────────────
// GET /fingerprint/:key
// Hashes the key string entirely on GPU — no CPU round-trip.

app.get('/fingerprint/:key', async (ctx) => {
  const fp = await ctx.gpu.fingerprint(ctx.params.key);
  ctx.json({ key: ctx.params.key, fingerprint: fp.toString(16) });
});

// ── VRAM cache routes ─────────────────────────────────────────────────────────
// GET /cache/:key     — fetch from VRAM cache
// PUT /cache/:key     — store in VRAM cache (body = JSON value)

app.get('/cache/:key', async (ctx) => {
  const val = await kv.get(ctx.params.key);
  if (val === null) {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json({ key: ctx.params.key, value: val, from: 'vram' });
});

app.put('/cache/:key', async (ctx) => {
  const body = await ctx.json();
  await kv.set(ctx.params.key, body, { ttlMs: 60_000 });
  ctx.json({ ok: true });
});

app.get('/_stats', async (ctx) => {
  const stats = await kv.stats();
  ctx.json(stats);
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, {
  port:    8080,
  streams: 32,
});
