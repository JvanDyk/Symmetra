/**
 * Timers example — Symmetra
 *
 * Demonstrates: setTimeout, setInterval, clearTimeout, clearInterval
 * The CPU min-heap timer thread fires callbacks onto a dedicated GPU stream.
 * clearTimeout/clearInterval are safe to call from any thread at any time.
 */

import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';

const app = new Router();
const kv  = new KvStore({ maxMb: 64 });

// ── 1. Request timeout using AbortController + setTimeout ───────────────────

app.get('/slow-op', async (ctx) => {
  const result = await new Promise<string>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Operation timed out')), 3_000);

    // Simulate a slow GPU operation
    heavyCompute()
      .then(val => { clearTimeout(timer); resolve(val); })
      .catch(err => { clearTimeout(timer); reject(err); });
  });

  ctx.json({ result });
});

async function heavyCompute(): Promise<string> {
  // Placeholder for actual GPU-heavy work
  return 'computed';
}

// ── 2. Recurring cache invalidation via setInterval ─────────────────────────

const CACHE_TTL_MS = 30_000;

// Clear all cached items every 30 seconds
const cacheFlushTimer = setInterval(async () => {
  await kv.del('__all__');
  console.log('Cache flushed at', Date.now());
}, CACHE_TTL_MS);

app.get('/cached/:key', async (ctx) => {
  const key = ctx.params.key;
  const cached = await kv.get<string>(key);
  if (cached) {
    ctx.json({ value: cached, source: 'cache' });
    return;
  }

  const value = `computed-${key}-${Date.now()}`;
  await kv.set(key, value, { ttlMs: CACHE_TTL_MS });
  ctx.json({ value, source: 'fresh' });
});

// ── 3. Debounced handler — collapses bursts into one GPU dispatch ───────────

const pendingDebounce = new Map<string, TimerId>();

app.post('/debounce/:key', async (ctx) => {
  const key  = ctx.params.key;
  const body = await ctx.json<{ value: string }>();

  // Cancel any pending flush for this key
  const existing = pendingDebounce.get(key);
  if (existing) clearTimeout(existing);

  // Schedule a flush 500ms from the last write
  const id = setTimeout(async () => {
    pendingDebounce.delete(key);
    await kv.set(key, body.value);
    console.log(`Debounced write committed: ${key} = ${body.value}`);
  }, 500);

  pendingDebounce.set(key, id);
  ctx.json({ queued: true, key });
});

// ── 4. Rate limiter using setInterval-based token bucket ────────────────────

const tokenBuckets = new Map<string, number>();
const BUCKET_CAPACITY = 10;
const REFILL_RATE_MS  = 1_000;  // one token per second

// Refill all buckets every second
setInterval(() => {
  for (const [key, tokens] of tokenBuckets) {
    tokenBuckets.set(key, Math.min(BUCKET_CAPACITY, tokens + 1));
  }
}, REFILL_RATE_MS);

app.get('/rate-limited/:ip', async (ctx) => {
  const ip = ctx.params.ip;
  const tokens = tokenBuckets.get(ip) ?? BUCKET_CAPACITY;

  if (tokens <= 0) {
    ctx.status(429).json({ error: 'Too Many Requests', retryAfterMs: REFILL_RATE_MS });
    return;
  }

  tokenBuckets.set(ip, tokens - 1);
  ctx.json({ allowed: true, remainingTokens: tokens - 1 });
});

// ── 5. One-shot delayed notification ────────────────────────────────────────

app.post('/remind', async (ctx) => {
  const { message, delayMs } = await ctx.json<{ message: string; delayMs: number }>();

  const id = setTimeout(() => {
    console.log(`REMINDER: ${message}`);
    // In a real app, push via WebSocket or SSE
  }, Math.max(100, Math.min(delayMs, 60_000)));

  ctx.json({ scheduled: true, timerId: id, delayMs });
});

serve(app, { port: 8083 });
