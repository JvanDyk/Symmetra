/**
 * Symmetra GpuWorker Example
 * Demonstrates: GpuWorker message passing, background GPU computation,
 *               worker pool, result aggregation.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { GpuWorker } from '@symmetra/core';
import { KvStore } from '@symmetra/kv';

const app     = new Router();
const results = new KvStore({ namespace: 'worker-results', maxMb: 32 });

// ── Single worker — offload heavy GPU compute ─────────────────────────────────

// Worker: receives a Float32Array, applies expensive transform, returns result
const transformWorker = new GpuWorker();
let transformResolve: ((v: unknown) => void) | null = null;

transformWorker.onmessage = async (data: unknown) => {
  if (transformResolve) {
    transformResolve(data);
    transformResolve = null;
  }
};

function workerTransform(input: Float32Array): Promise<Float32Array> {
  return new Promise((resolve) => {
    transformResolve = resolve as (v: unknown) => void;
    transformWorker.postMessage(input);
  });
}

app.post('/worker/transform', async (ctx) => {
  const data   = await ctx.body<Float32Array>();
  const output = await workerTransform(data) as Float32Array;
  ctx.json({
    inputLength:  data.length,
    outputLength: output.length,
    sampleInput:  Array.from(data.slice(0, 4)),
    sampleOutput: Array.from(output.slice(0, 4)).map(v => parseFloat(v.toFixed(4))),
  });
});

// ── Worker pool — fixed number of workers, round-robin dispatch ───────────────

const POOL_SIZE = 4;
const pool: GpuWorker[] = [];
const pendingCallbacks: Array<((v: unknown) => void) | null> = new Array(POOL_SIZE).fill(null);
let poolIndex = 0;

for (let i = 0; i < POOL_SIZE; i++) {
  const worker = new GpuWorker();
  const idx    = i;
  worker.onmessage = (data: unknown) => {
    const cb = pendingCallbacks[idx];
    if (cb) {
      pendingCallbacks[idx] = null;
      cb(data);
    }
  };
  pool.push(worker);
}

function poolDispatch(input: unknown): Promise<unknown> {
  const idx    = poolIndex;
  poolIndex    = (poolIndex + 1) % POOL_SIZE;
  const worker = pool[idx];
  return new Promise((resolve) => {
    pendingCallbacks[idx] = resolve;
    worker.postMessage(input);
  });
}

app.post('/worker/pool', async (ctx) => {
  const { tasks } = await ctx.json<{ tasks: number[][] }>();
  if (!Array.isArray(tasks) || tasks.length === 0) {
    ctx.status(400).json({ error: 'tasks array required' });
    return;
  }

  // Dispatch all tasks to the pool in parallel
  const promises = tasks.map(t => poolDispatch(new Float32Array(t)));
  const outputs  = await Promise.all(promises);

  ctx.json({
    taskCount:    tasks.length,
    poolSize:     POOL_SIZE,
    outputLengths: (outputs as Float32Array[]).map(o => o.length),
  });
});

// ── Background worker — long-running computation ──────────────────────────────

let bgJobId = 0;
const bgWorker = new GpuWorker();
const bgCallbacks = new Map<number, (v: unknown) => void>();

bgWorker.onmessage = (msg: unknown) => {
  const { id, result } = msg as { id: number; result: unknown };
  const cb = bgCallbacks.get(id);
  if (cb) {
    bgCallbacks.delete(id);
    cb(result);
  }
};

// POST /worker/bg  { n: <number> } — submit job, get job ID
app.post('/worker/bg', async (ctx) => {
  const { n } = await ctx.json<{ n: number }>();
  const id    = ++bgJobId;

  // Fire and forget — don't await
  const resultPromise = new Promise<unknown>((resolve) => {
    bgCallbacks.set(id, resolve);
    bgWorker.postMessage({ id, n });
  });

  // Store result asynchronously
  resultPromise.then(async (result) => {
    await results.set(`job:${id}`, { id, result, completedAt: Date.now() }, { ttlMs: 60_000 });
  });

  ctx.status(202).json({ jobId: id, status: 'queued' });
});

// GET /worker/bg/:id — poll for result
app.get('/worker/bg/:id', async (ctx) => {
  const job = await results.get<{ id: number; result: unknown; completedAt: number }>(
    `job:${ctx.params.id}`,
  );

  if (!job) {
    ctx.json({ jobId: ctx.params.id, status: 'pending' });
    return;
  }

  ctx.json({ jobId: job.id, status: 'complete', result: job.result, completedAt: job.completedAt });
});

// ── Worker health ─────────────────────────────────────────────────────────────

app.get('/worker/health', (ctx) => {
  ctx.json({
    transformWorker: { status: 'running' },
    pool:            { size: POOL_SIZE, status: 'running' },
    bgWorker:        { status: 'running', pendingJobs: bgCallbacks.size },
  });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
