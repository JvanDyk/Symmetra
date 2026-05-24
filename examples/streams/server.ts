/**
 * Symmetra Streams Example
 * Demonstrates: GpuReadable, GpuWritable, pipeline, Transform, backpressure.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';
import { GpuReadable, GpuWritable, pipeline } from '@symmetra/core';

const app = new Router();

// ── Generator → Readable ──────────────────────────────────────────────────────

app.get('/stream/numbers', async (ctx) => {
  // Produce 100 chunks of 256 floats each
  async function* generate(): AsyncGenerator<Float32Array> {
    for (let i = 0; i < 100; i++) {
      const chunk = new Float32Array(256);
      for (let j = 0; j < 256; j++) chunk[j] = i * 256 + j;
      yield chunk;
    }
  }

  const readable = GpuReadable.from(generate());
  const chunks: Float32Array[] = [];

  for await (const chunk of readable) {
    chunks.push(chunk);
  }

  ctx.json({
    chunkCount:    chunks.length,
    totalElements: chunks.reduce((n, c) => n + c.length, 0),
    firstFive:     Array.from(chunks[0].slice(0, 5)),
    lastFive:      Array.from(chunks[99].slice(251, 256)),
  });
});

// ── Pipeline: source → transform → sink ──────────────────────────────────────

app.post('/stream/transform', async (ctx) => {
  const data = await ctx.body<Float32Array>();

  // Source: emit data in 64-element chunks
  async function* chunkify(input: Float32Array): AsyncGenerator<Float32Array> {
    for (let offset = 0; offset < input.length; offset += 64) {
      yield input.slice(offset, offset + 64) as Float32Array;
    }
  }

  // Sink: collect transformed chunks
  const results: Float32Array[] = [];
  const sink = new GpuWritable<Float32Array>({
    write(chunk) { results.push(chunk); },
  });

  // Pipeline: chunk → GPU map (x² + sin(x)) → collect
  await pipeline(
    GpuReadable.from(chunkify(data)),
    async function* transform(src: AsyncIterable<Float32Array>) {
      for await (const chunk of src) {
        yield chunk.map(x => x * x + Math.sin(x));
      }
    },
    sink,
  );

  const flat = results.flatMap(c => Array.from(c));
  ctx.json({
    inputLength:  data.length,
    outputLength: flat.length,
    sampleOutput: flat.slice(0, 5).map(v => parseFloat(v.toFixed(4))),
  });
});

// ── Readable.map and Readable.filter ─────────────────────────────────────────

app.post('/stream/filter-map', async (ctx) => {
  const { values } = await ctx.json<{ values: number[] }>();

  async function* source(): AsyncGenerator<Float32Array> {
    yield new Float32Array(values);
  }

  // Only keep positives, then square them
  const readable = GpuReadable.from(source())
    .filter(x => x > 0)
    .map(x => x * x);

  const out: number[] = [];
  for await (const chunk of readable) {
    out.push(...Array.from(chunk));
  }

  ctx.json({ input: values, output: out });
});

// ── Async generator as a stream ───────────────────────────────────────────────

class CountingStream {
  private _n: number;
  constructor(n: number) { this._n = n; }

  async *[Symbol.asyncIterator](): AsyncGenerator<number> {
    for (let i = 0; i < this._n; i++) {
      yield i;
    }
  }
}

app.get('/stream/count/:n', async (ctx) => {
  const n      = Math.min(parseInt(ctx.params.n, 10), 1000);
  const stream = new CountingStream(n);

  let sum = 0;
  let count = 0;
  for await (const value of stream) {
    sum += value;
    count++;
  }

  ctx.json({
    n:     count,
    sum,
    mean:  count > 0 ? sum / count : 0,
  });
});

// ── Pipeline with multiple transforms ────────────────────────────────────────

app.post('/stream/pipeline', async (ctx) => {
  const { n } = await ctx.json<{ n: number }>();
  const count = Math.min(n, 10_000);

  async function* generate(): AsyncGenerator<Float32Array> {
    for (let i = 0; i < count; i += 256) {
      const size  = Math.min(256, count - i);
      const chunk = new Float32Array(size);
      for (let j = 0; j < size; j++) chunk[j] = i + j;
      yield chunk;
    }
  }

  const totals = { sum: 0, count: 0, min: Infinity, max: -Infinity };

  await pipeline(
    GpuReadable.from(generate()),
    // Stage 1: normalize to [0, 1]
    async function* normalize(src: AsyncIterable<Float32Array>) {
      for await (const c of src) yield c.map(x => x / count);
    },
    // Stage 2: apply sigmoid
    async function* sigmoid(src: AsyncIterable<Float32Array>) {
      for await (const c of src) yield c.map(x => 1 / (1 + Math.exp(-x)));
    },
    // Stage 3: collect stats
    new GpuWritable<Float32Array>({
      write(chunk) {
        for (let i = 0; i < chunk.length; i++) {
          const v = chunk[i];
          totals.sum += v;
          totals.count++;
          if (v < totals.min) totals.min = v;
          if (v > totals.max) totals.max = v;
        }
      },
    }),
  );

  ctx.json({
    inputCount: count,
    outputCount: totals.count,
    mean: parseFloat((totals.sum / totals.count).toFixed(6)),
    min:  parseFloat(totals.min.toFixed(6)),
    max:  parseFloat(totals.max.toFixed(6)),
  });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
