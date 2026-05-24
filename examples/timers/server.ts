/**
 * Symmetra Timers Example
 * Demonstrates: setTimeout, setInterval, clearTimeout, clearInterval,
 *               Promise + timer, race conditions, cleanup.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { KvStore } from '@symmetra/kv';

const app     = new Router();
const metrics = new KvStore({ namespace: 'timers', maxMb: 16 });

// ── Basic setTimeout ──────────────────────────────────────────────────────────

app.get('/timer/once/:ms', async (ctx) => {
  const ms    = Math.min(parseInt(ctx.params.ms, 10), 5_000);
  const start = Date.now();

  const result = await new Promise<string>((resolve) => {
    setTimeout(() => resolve('fired'), ms);
  });

  ctx.json({ result, requestedMs: ms, actualMs: Date.now() - start });
});

// ── clearTimeout cancels ──────────────────────────────────────────────────────

app.get('/timer/cancel', async (ctx) => {
  let fired = false;

  const cancelId = setTimeout(() => { fired = true; }, 100);
  clearTimeout(cancelId);   // cancel immediately

  // Wait a bit to confirm it did not fire
  await new Promise<void>((resolve) => setTimeout(resolve, 200));

  ctx.json({ cancelled: !fired, ok: !fired });
});

// ── Race: first timer wins ────────────────────────────────────────────────────

app.get('/timer/race', async (ctx) => {
  const start = Date.now();

  const winner = await Promise.race([
    new Promise<string>(resolve => setTimeout(() => resolve('slow'),   200)),
    new Promise<string>(resolve => setTimeout(() => resolve('fast'),    50)),
    new Promise<string>(resolve => setTimeout(() => resolve('medium'), 100)),
  ]);

  ctx.json({ winner, ok: winner === 'fast', elapsedMs: Date.now() - start });
});

// ── setInterval + auto-stop ───────────────────────────────────────────────────

app.get('/timer/interval/:n', async (ctx) => {
  const maxTicks = Math.min(parseInt(ctx.params.n, 10), 10);
  const ticks: number[] = [];
  const start = Date.now();

  await new Promise<void>((resolve) => {
    const id = setInterval(() => {
      ticks.push(Date.now() - start);
      if (ticks.length >= maxTicks) {
        clearInterval(id);
        resolve();
      }
    }, 50);
  });

  ctx.json({
    ticks:     ticks.length,
    intervals: ticks.map((t, i) => i === 0 ? t : t - ticks[i - 1]),
    totalMs:   ticks[ticks.length - 1],
  });
});

// ── Debounce pattern ──────────────────────────────────────────────────────────

let debounceTimer: ReturnType<typeof setTimeout> | null = null;
let debounceCount = 0;

app.post('/timer/debounce', async (ctx) => {
  debounceCount++;

  if (debounceTimer !== null) {
    clearTimeout(debounceTimer);
  }

  await new Promise<void>((resolve) => {
    debounceTimer = setTimeout(() => {
      debounceTimer = null;
      resolve();
    }, 200);
  });

  ctx.json({ finalCount: debounceCount, message: 'debounced action executed' });
  debounceCount = 0;  // reset after execution
});

// ── Timeout wrapper ───────────────────────────────────────────────────────────

async function withTimeout<T>(promise: Promise<T>, ms: number, label: string): Promise<T> {
  let timer: ReturnType<typeof setTimeout>;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new Error(`${label} timed out after ${ms}ms`)), ms);
  });
  try {
    const result = await Promise.race([promise, timeout]);
    clearTimeout(timer!);
    return result;
  } catch (e) {
    clearTimeout(timer!);
    throw e;
  }
}

app.get('/timer/timeout-wrapper', async (ctx) => {
  const limitMs = 150;

  try {
    // This completes in time:
    const fast = await withTimeout(
      new Promise<string>(resolve => setTimeout(() => resolve('ok'), 50)),
      limitMs,
      'fast-op',
    );

    // This would time out — demonstrate the error path
    let timedOut = false;
    try {
      await withTimeout(
        new Promise<string>(resolve => setTimeout(() => resolve('too-slow'), 500)),
        limitMs,
        'slow-op',
      );
    } catch {
      timedOut = true;
    }

    ctx.json({ fastResult: fast, slowTimedOut: timedOut });
  } catch (e) {
    ctx.status(500).json({ error: String(e) });
  }
});

// ── Background interval counter ───────────────────────────────────────────────

let tickCount = 0;
const bgInterval = setInterval(async () => {
  tickCount++;
  await metrics.set('tick-count', tickCount);
}, 1_000);

// Clean up on shutdown (Symmetra calls this on SIGINT)
// In production, store the interval ID for cleanup
void bgInterval;

app.get('/timer/ticks', async (ctx) => {
  const stored = await metrics.get<number>('tick-count');
  ctx.json({ ticks: stored ?? 0, approxUptimeSeconds: stored ?? 0 });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
