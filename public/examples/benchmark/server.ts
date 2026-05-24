// Symmetra Benchmark
// Compares GPU-native routing vs equivalent Express/Node baseline.
// Run: sym ./server.ts
// Then: bombardier -c 500 -n 100000 http://localhost:8080/ping

import { serve, Router } from '@symmetra/http';

const app = new Router();

// Route 1: Simple JSON — tests raw routing + response throughput
app.get('/ping', (ctx) => {
  ctx.json({ pong: true });
});

// Route 2: Array transform — tests GPU parallelism
// POST body: 1024 floats (4 KB)
// GPU maps each element through: x*x + sin(x) + cos(x*0.5)
app.post('/compute', async (ctx) => {
  const data   = await ctx.body<Float32Array>();
  const result = data.map(x => x * x + Math.sin(x) + Math.cos(x * 0.5));
  ctx.json(result);
});

// Route 3: GPU fingerprint
app.get('/fp/:id', async (ctx) => {
  const fp = await ctx.gpu.fingerprint(ctx.params.id);
  ctx.json({ id: ctx.params.id, fp: fp.toString(16) });
});

serve(app, { port: 8080, streams: 64 });
