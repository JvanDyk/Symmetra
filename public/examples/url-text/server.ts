/**
 * URL / URLSearchParams / TextEncoder / TextDecoder example — Symmetra
 *
 * URL parsing is CPU-side (WHATWG compliant). Parsed fields are uploaded to
 * the request VRAM slab. TextEncoder/Decoder run GPU kernels (one thread per
 * code point for encode; one per start byte for decode).
 */

import { serve, Router } from '@symmetra/http';
import { http }          from '@symmetra/net';

const app = new Router();

const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8');

// ── 1. URL construction and query string manipulation ────────────────────────

app.get('/build-url', async (ctx) => {
  const base     = new URL('https://api.example.com');
  const endpoint = new URL('/v2/search', base);

  endpoint.searchParams.set('q',      ctx.query.q      ?? 'default');
  endpoint.searchParams.set('page',   ctx.query.page   ?? '1');
  endpoint.searchParams.set('limit',  ctx.query.limit  ?? '20');
  endpoint.searchParams.append('tag', 'gpu');
  endpoint.searchParams.append('tag', 'fast');

  ctx.json({
    href:     endpoint.href,
    origin:   endpoint.origin,
    pathname: endpoint.pathname,
    search:   endpoint.search,
    params:   Object.fromEntries(endpoint.searchParams.entries()),
  });
});

// ── 2. Parse an inbound URL from a POST body ─────────────────────────────────

app.post('/parse-url', async (ctx) => {
  const { url: rawUrl } = await ctx.json<{ url: string }>();

  let parsed: URL;
  try {
    parsed = new URL(rawUrl);
  } catch {
    ctx.status(400).json({ error: 'Invalid URL' });
    return;
  }

  ctx.json({
    protocol: parsed.protocol,
    hostname: parsed.hostname,
    port:     parsed.port,
    pathname: parsed.pathname,
    search:   parsed.search,
    hash:     parsed.hash,
    origin:   parsed.origin,
    valid:    true,
  });
});

// ── 3. URLSearchParams: parse and modify query strings ───────────────────────

app.post('/query-ops', async (ctx) => {
  const { qs, set, remove } = await ctx.json<{
    qs:     string;
    set?:   Record<string, string>;
    remove?: string[];
  }>();

  const params = new URLSearchParams(qs);

  if (set) {
    for (const [k, v] of Object.entries(set)) params.set(k, v);
  }
  if (remove) {
    for (const k of remove) params.delete(k);
  }

  const result: Record<string, string[]> = {};
  for (const key of new Set([...params.keys()])) {
    result[key] = params.getAll(key);
  }

  ctx.json({ serialised: params.toString(), params: result });
});

// ── 4. TextEncoder — encode strings to UTF-8 bytes on GPU ───────────────────
// encoder.encode() launches symm_text_encode kernel — one thread per code point.

app.post('/encode', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();

  const bytes   = encoder.encode(text);     // GpuBuffer<Uint8Array> in VRAM
  const decoded = decoder.decode(bytes);    // back to string — round-trip check

  ctx.json({
    original:     text,
    byteLength:   bytes.byteLength,
    roundtrip:    decoded,
    roundtripOk:  decoded === text,
  });
});

// ── 5. TextDecoder — decode UTF-8 bytes on GPU ───────────────────────────────
// Accepts raw bytes in the request body, returns the decoded string.

app.post('/decode', async (ctx) => {
  const body   = await ctx.body<Uint8Array>();
  const text   = decoder.decode(body);

  ctx.json({ text, byteLength: body.byteLength });
});

// ── 6. Proxy — rewrite outbound URLs using URL API ──────────────────────────

app.get('/proxy', async (ctx) => {
  const targetParam = ctx.query.target;
  if (!targetParam) { ctx.status(400).json({ error: 'missing ?target=' }); return; }

  let target: URL;
  try {
    target = new URL(targetParam);
  } catch {
    ctx.status(400).json({ error: 'Invalid target URL' }); return;
  }

  // Enforce HTTPS
  if (target.protocol !== 'https:') {
    target.protocol = 'https:';
  }

  // Forward any extra query params from our request
  for (const [k, v] of Object.entries(ctx.query)) {
    if (k === 'target') continue;
    target.searchParams.set(k, v);
  }

  const resp = await http.get(target.toString());
  ctx.json({ proxied: target.href, status: 200, body: resp.json() });
});

// ── 7. Base64-encode via TextEncoder → manual table lookup ──────────────────

app.post('/base64', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();
  const bytes    = encoder.encode(text);

  // Manual base64 — each GPU thread encodes 3 bytes → 4 chars
  const TABLE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const arr   = Array.from(bytes as unknown as Uint8Array);
  let b64     = '';

  for (let i = 0; i < arr.length; i += 3) {
    const a = arr[i], b = arr[i+1] ?? 0, c = arr[i+2] ?? 0;
    b64 += TABLE[a >> 2];
    b64 += TABLE[((a & 3) << 4) | (b >> 4)];
    b64 += i + 1 < arr.length ? TABLE[((b & 15) << 2) | (c >> 6)] : '=';
    b64 += i + 2 < arr.length ? TABLE[c & 63]                      : '=';
  }

  ctx.json({ input: text, base64: b64, byteLength: arr.length });
});

serve(app, { port: 8085 });
