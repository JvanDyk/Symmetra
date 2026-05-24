/**
 * Symmetra URL + TextEncoder Example
 * Demonstrates: URL parsing, URLSearchParams, TextEncoder/Decoder, string ops.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';

const app = new Router();

// ── URL parsing ───────────────────────────────────────────────────────────────

app.post('/url/parse', async (ctx) => {
  const { url } = await ctx.json<{ url: string }>();

  try {
    const u = new URL(url);
    ctx.json({
      href:     u.href,
      protocol: u.protocol,
      hostname: u.hostname,
      port:     u.port,
      pathname: u.pathname,
      search:   u.search,
      hash:     u.hash,
      origin:   u.origin,
      valid:    true,
    });
  } catch {
    ctx.status(400).json({ valid: false, error: 'invalid URL' });
  }
});

// ── URLSearchParams ───────────────────────────────────────────────────────────

app.post('/url/search-params', async (ctx) => {
  const { queryString } = await ctx.json<{ queryString: string }>();

  const params = new URLSearchParams(queryString);

  // Multi-value support
  const all: Record<string, string[]> = {};
  for (const key of params.keys()) {
    all[key] = params.getAll(key);
  }

  ctx.json({
    q:     params.get('q'),
    limit: params.get('limit'),
    all,
    has_q: params.has('q'),
    size:  [...params].length,
  });
});

app.get('/url/build', (ctx) => {
  const params = new URLSearchParams();
  params.set('q', 'symmetra gpu');
  params.set('page', '1');
  params.set('sort', 'relevance');
  params.append('tag', 'cuda');
  params.append('tag', 'typescript');

  const url = new URL('https://search.example.com/results');
  url.search = params.toString();

  ctx.json({
    url:    url.href,
    search: url.search,
    tags:   params.getAll('tag'),
  });
});

// ── TextEncoder / TextDecoder ─────────────────────────────────────────────────

app.post('/text/encode', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();

  const encoder = new TextEncoder();
  const bytes   = encoder.encode(text);

  ctx.json({
    input:      text,
    byteLength: bytes.byteLength,
    charLength: text.length,
    bytes:      Array.from(bytes.slice(0, 32)),   // first 32 bytes as numbers
    isUtf8:     true,
  });
});

app.post('/text/decode', async (ctx) => {
  const { bytes } = await ctx.json<{ bytes: number[] }>();

  const uint8   = new Uint8Array(bytes);
  const decoder = new TextDecoder('utf-8', { fatal: true });

  try {
    const text = decoder.decode(uint8);
    ctx.json({ text, length: text.length, ok: true });
  } catch {
    ctx.status(400).json({ error: 'invalid UTF-8 sequence', ok: false });
  }
});

app.post('/text/roundtrip', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();

  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const bytes   = encoder.encode(text);
  const decoded = decoder.decode(bytes);

  ctx.json({
    original:   text,
    byteLength: bytes.byteLength,
    roundtrip:  decoded,
    ok:         decoded === text,
  });
});

// ── String operations ─────────────────────────────────────────────────────────

app.post('/text/ops', async (ctx) => {
  const { s } = await ctx.json<{ s: string }>();

  ctx.json({
    length:      s.length,
    upper:       s.toUpperCase(),
    lower:       s.toLowerCase(),
    trimmed:     s.trim(),
    trimStart:   s.trimStart(),
    trimEnd:     s.trimEnd(),
    reversed:    s.split('').reverse().join(''),
    words:       s.trim().split(/\s+/).length,
    firstWord:   s.trim().split(/\s+/)[0] ?? '',
    padded:      s.padStart(20, '.'),
    repeated:    s.repeat(3),
    replaced:    s.replaceAll(' ', '_'),
    starts:      s.startsWith(' '),
    ends:        s.endsWith(' '),
    includes:    s.includes('e'),
  });
});

// ── Percent-encoding ──────────────────────────────────────────────────────────

app.post('/text/percent-encode', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();

  const encoded = encodeURIComponent(text);
  const decoded = decodeURIComponent(encoded);

  ctx.json({
    original: text,
    encoded,
    decoded,
    ok: decoded === text,
  });
});

// ── URL construction + manipulation ───────────────────────────────────────────

app.post('/url/manipulate', async (ctx) => {
  const { base, path, params } = await ctx.json<{
    base: string;
    path: string;
    params: Record<string, string>;
  }>();

  const url = new URL(path, base);
  for (const [k, v] of Object.entries(params)) {
    url.searchParams.set(k, v);
  }

  ctx.json({ result: url.href, search: url.search });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
