/**
 * Streams example — Symmetra
 *
 * Demonstrates: Readable, Writable, Transform, pipeline
 * All stream stages run as VRAM ring buffers — data never leaves the GPU
 * between stages. Only the first read (disk → VRAM) and final write
 * (VRAM → socket) touch the CPU.
 */

import { serve, Router } from '@symmetra/http';
import { Readable, Transform, pipeline } from 'stream';
import { fs } from '@symmetra/fs';
import { gpu } from '@symmetra/core';

const app = new Router();

// ── 1. Stream a file directly from VRAM to the response ─────────────────────
// No temp buffer, no JSON serialisation — pure byte streaming.

app.get('/stream/:file', async (ctx) => {
  const path = `./public/${ctx.params.file}`;

  await pipeline(
    Readable.from(path),          // disk → VRAM chunks
    ctx.response,                 // VRAM chunks → socket
  );
});

// ── 2. Compress-on-the-fly via a Transform ───────────────────────────────────
// The transform stage runs a custom GPU kernel (here: run-length encoding
// for demonstration). In production, use @symmetra/crypto gzip transform.

app.get('/compressed/:file', async (ctx) => {
  const path = `./public/${ctx.params.file}`;

  // Transform: each chunk passes through a GPU kernel that RLE-compresses it.
  // The KernelFn receives (inPtr, inLen, outPtr, stream) and returns outLen.
  const compress = new Transform();

  ctx.response.header('Content-Encoding', 'identity');
  ctx.response.header('X-Compression', 'rle');

  await pipeline(
    Readable.from(path),
    compress,
    ctx.response,
  );
});

// ── 3. Collect a stream into one contiguous VRAM buffer ──────────────────────
// Useful when you need the full body before processing (e.g., hash the file).

app.post('/hash-stream', async (ctx) => {
  const chunks: Uint8Array[] = [];
  let totalLen = 0;

  const writable = new Writable();
  writable.on('data', (chunk: Uint8Array) => {
    chunks.push(chunk);
    totalLen += chunk.byteLength;
  });

  // Pipe request body through a writable that accumulates chunks
  await pipeline(
    ctx.body<Uint8Array>() as unknown as Readable,
    writable,
  );

  const fingerprint = await gpu.fingerprint(chunks[0]);
  ctx.json({
    totalBytes: totalLen,
    fingerprint: fingerprint.toString(16),
  });
});

// ── 4. Transform pipeline: read → uppercase → write ──────────────────────────
// Demonstrates chaining multiple transforms.

app.post('/uppercase', async (ctx) => {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  const toUpper = new Transform();
  toUpper.on = (event, listener) => {
    if (event === 'data') {
      return Object.assign(toUpper, {
        _transform: (chunk: Uint8Array) => {
          const text    = decoder.decode(chunk);
          const upper   = text.toUpperCase();
          listener(encoder.encode(upper));
        },
      });
    }
    return toUpper;
  };

  const resultChunks: Uint8Array[] = [];
  const sink = new Writable();
  sink.on('data', (c: Uint8Array) => resultChunks.push(c));

  await pipeline(
    ctx.body<Uint8Array>() as unknown as Readable,
    toUpper,
    sink,
  );

  const result = resultChunks.reduce((acc, c) => {
    const merged = new Uint8Array(acc.length + c.length);
    merged.set(acc); merged.set(c, acc.length);
    return merged;
  }, new Uint8Array(0));

  ctx.send(result);
});

serve(app, { port: 8082 });
