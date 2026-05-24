/**
 * Symmetra AbortController Example
 * Demonstrates: AbortController, AbortSignal, cancellable fetch, graceful shutdown.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { fetch } from '@symmetra/net';

const app = new Router();

// ── Basic abort ───────────────────────────────────────────────────────────────

app.get('/abort/basic', (ctx) => {
  const controller = new AbortController();
  const { signal } = controller;

  let listenerFired = false;
  signal.addEventListener('abort', () => { listenerFired = true; });

  // Signal is not yet aborted
  const before = signal.aborted;

  controller.abort();

  ctx.json({
    before,
    after:         signal.aborted,
    listenerFired,
    ok: !before && signal.aborted && listenerFired,
  });
});

// ── Abort reason ──────────────────────────────────────────────────────────────

app.get('/abort/reason', (ctx) => {
  const controller = new AbortController();
  controller.abort(new Error('user cancelled'));

  ctx.json({
    aborted: controller.signal.aborted,
    reason:  (controller.signal.reason as Error)?.message ?? null,
  });
});

// ── Double-abort safety (must not throw) ─────────────────────────────────────

app.get('/abort/double', (ctx) => {
  const controller = new AbortController();
  controller.abort();
  controller.abort();   // safe — second abort is a no-op

  ctx.json({ aborted: controller.signal.aborted, ok: true });
});

// ── Cancellable timer ─────────────────────────────────────────────────────────

app.post('/abort/timer', async (ctx) => {
  const { cancelAfterMs, timerMs } = await ctx.json<{
    cancelAfterMs: number;
    timerMs:       number;
  }>();

  const controller = new AbortController();
  const { signal } = controller;

  let cancelled = false;

  const result = await new Promise<string>((resolve) => {
    const timerId = setTimeout(() => resolve('timer-fired'), timerMs);

    signal.addEventListener('abort', () => {
      clearTimeout(timerId);
      cancelled = true;
      resolve('cancelled');
    });

    setTimeout(() => controller.abort(), cancelAfterMs);
  });

  ctx.json({ result, cancelled, ok: result === (cancelAfterMs < timerMs ? 'cancelled' : 'timer-fired') });
});

// ── Cancellable fetch ─────────────────────────────────────────────────────────

app.get('/abort/fetch', async (ctx) => {
  const timeoutMs = parseInt(ctx.query.timeout ?? '1000', 10);

  const controller = new AbortController();
  setTimeout(() => controller.abort(), timeoutMs);

  try {
    const resp = await fetch('https://httpbin.org/delay/5', {
      signal: controller.signal,
    });
    ctx.json({ completed: true, status: resp.status });
  } catch {
    ctx.json({ completed: false, abortedAfterMs: timeoutMs, ok: true });
  }
});

// ── Multiple signal listeners ─────────────────────────────────────────────────

app.get('/abort/listeners', (ctx) => {
  const controller = new AbortController();
  const { signal } = controller;

  const fired: string[] = [];

  signal.addEventListener('abort', () => fired.push('listener-1'));
  signal.addEventListener('abort', () => fired.push('listener-2'));
  signal.addEventListener('abort', () => fired.push('listener-3'));

  controller.abort();

  ctx.json({
    listeners: fired.length,
    fired,
    ok: fired.length === 3,
  });
});

// ── Race: first of fetch or abort wins ───────────────────────────────────────

app.post('/abort/race-fetch', async (ctx) => {
  const { urls, timeoutMs } = await ctx.json<{ urls: string[]; timeoutMs: number }>();

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const results = await Promise.allSettled(
      urls.map(url => fetch(url, { signal: controller.signal, timeoutMs })),
    );
    clearTimeout(timer);

    ctx.json({
      total:     results.length,
      succeeded: results.filter(r => r.status === 'fulfilled').length,
      failed:    results.filter(r => r.status === 'rejected').length,
    });
  } catch {
    clearTimeout(timer);
    ctx.json({ aborted: true, timeoutMs });
  }
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
