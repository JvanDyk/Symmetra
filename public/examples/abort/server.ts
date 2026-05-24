/**
 * AbortController / AbortSignal example — Symmetra
 *
 * Demonstrates: AbortController, AbortSignal, signal-aware fetch,
 * request cancellation, and timeout patterns.
 *
 * AbortSignal compiles to a single VRAM uint8_t flag.
 * controller.abort() calls cuMemsetD8 — instant, no kernel launch.
 * Running GPU kernels check the flag at yield points and exit early.
 */

import { serve, Router }  from '@symmetra/http';
import { http }           from '@symmetra/net';

const app = new Router();

// ── 1. Fetch with a 5-second timeout ────────────────────────────────────────

app.get('/fetch-with-timeout', async (ctx) => {
  const controller = new AbortController();
  const timeoutId  = setTimeout(() => controller.abort(), 5_000);

  try {
    const resp = await http.get('https://httpbin.org/delay/2', {
      signal: controller.signal,
    });
    ctx.json({ data: resp.json(), aborted: false });
  } catch (err) {
    if (controller.signal.aborted) {
      ctx.status(408).json({ error: 'Request timed out after 5s' });
    } else {
      ctx.status(502).json({ error: String(err) });
    }
  } finally {
    clearTimeout(timeoutId);
  }
});

// ── 2. Cancel a long-running GPU computation mid-flight ──────────────────────
// The signal's VRAM flag is passed into the kernel. The kernel checks it
// at every iteration and exits early — no wasted clock cycles after abort.

app.post('/compute', async (ctx) => {
  const { iterations } = await ctx.json<{ iterations: number }>();
  const controller = new AbortController();

  // Client can abort via a companion DELETE endpoint
  abortRegistry.set(ctx.params.requestId ?? 'default', controller);

  try {
    // signal.aborted is checked inside the GPU kernel at each warp barrier
    const result = await runIterativeKernel(iterations, controller.signal);
    ctx.json({ result, aborted: false });
  } catch (err) {
    if (controller.signal.aborted) {
      ctx.status(409).json({ error: 'Computation was cancelled' });
    } else {
      throw err;
    }
  } finally {
    abortRegistry.delete(ctx.params.requestId ?? 'default');
  }
});

const abortRegistry = new Map<string, AbortController>();

app.delete('/compute/:requestId', (ctx) => {
  const ctrl = abortRegistry.get(ctx.params.requestId);
  if (!ctrl) { ctx.status(404).json({ error: 'Not found' }); return; }
  ctrl.abort();
  ctx.json({ cancelled: true });
});

async function runIterativeKernel(n: number, signal: AbortSignal): Promise<number> {
  // The compiler passes signal's VRAM device pointer into the generated kernel.
  // At each convergence-test step the kernel does: if (*abort_flag) return;
  let sum = 0;
  for (let i = 0; i < n && !signal.aborted; i++) {
    sum += i;
  }
  return sum;
}

// ── 3. Cascaded abort — cancel all child requests when parent aborts ─────────

app.get('/fan-out', async (ctx) => {
  const parent = new AbortController();
  const timeoutId = setTimeout(() => parent.abort(), 8_000);

  const urls = [
    'https://httpbin.org/get',
    'https://httpbin.org/uuid',
    'https://httpbin.org/ip',
  ];

  // All three child fetches share the same AbortSignal — aborting the parent
  // cancels all of them simultaneously via the single VRAM flag.
  try {
    const results = await Promise.all(
      urls.map(url => http.get(url, { signal: parent.signal }))
    );
    ctx.json({ results: results.map(r => r.json()) });
  } catch (err) {
    if (parent.signal.aborted) {
      ctx.status(408).json({ error: 'Fan-out aborted', reason: 'timeout' });
    } else {
      throw err;
    }
  } finally {
    clearTimeout(timeoutId);
  }
});

// ── 4. addEventListener — run cleanup when aborted ───────────────────────────

app.post('/with-cleanup', async (ctx) => {
  const controller = new AbortController();
  const { signal } = controller;
  let cleanedUp = false;

  signal.addEventListener('abort', () => {
    cleanedUp = true;
    console.log('Cleanup triggered by abort');
  });

  const timeoutId = setTimeout(() => controller.abort(), 2_000);

  try {
    const result = await http.get('https://httpbin.org/delay/10', { signal });
    ctx.json({ result: result.json(), cleanedUp });
  } catch {
    ctx.json({ aborted: signal.aborted, cleanedUp });
  } finally {
    clearTimeout(timeoutId);
  }
});

serve(app, { port: 8084 });
