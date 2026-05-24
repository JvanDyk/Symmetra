/**
 * Symmetra — HTTP client & networking example
 * Demonstrates outbound HTTP requests, WebSocket, TCP sockets, and DNS.
 *
 * Run: sym ./server.ts
 */
import { serve, Router } from '@symmetra/http';
import { http, connect, websocket, dns } from '@symmetra/net';
import { KvStore } from '@symmetra/kv';
import { env } from '@symmetra/env';

const cfg = env.load({
  PORT:                { type: 'port',   default: 8080 },
  EXTERNAL_API_URL:    { type: 'string', default: 'https://api.example.com' },
  EXTERNAL_API_KEY:    { type: 'string', default: '' },
  WS_UPSTREAM:         { type: 'string', default: 'wss://stream.example.com' },
} as const);

const responseCache = new KvStore({ maxMb: 128 });

const app = new Router();

// ── Outbound HTTP GET with caching ────────────────────────────────────────────

app.get('/proxy/weather', async (ctx) => {
  const city = (ctx.query.city as string) ?? 'London';
  const cacheKey = `weather:${city.toLowerCase()}`;

  const cached = await responseCache.get<unknown>(cacheKey);
  if (cached) {
    ctx.json(cached);
    return;
  }

  const response = await http.get(`https://wttr.in/${encodeURIComponent(city)}?format=j1`, {
    headers: { 'User-Agent': 'Symmetra/1.0' },
    timeout: 5000,
  });

  if (!response.ok) {
    ctx.status(502).json({ error: 'upstream error', status: response.status });
    return;
  }

  const data = response.json<unknown>();
  await responseCache.set(cacheKey, data, { ttlMs: 300_000 });
  ctx.json(data);
});

// ── Outbound POST — forward a webhook ────────────────────────────────────────

app.post('/webhook/forward', async (ctx) => {
  const payload = await ctx.json<Record<string, unknown>>();

  const response = await http.post(
    `${cfg.EXTERNAL_API_URL}/webhook`,
    payload,
    {
      headers: {
        'Authorization': `Bearer ${cfg.EXTERNAL_API_KEY}`,
        'Content-Type': 'application/json',
      },
      timeout: 10_000,
    }
  );

  ctx.status(response.status).json(response.json<unknown>());
});

// ── Fan-out: call multiple APIs in parallel ───────────────────────────────────

app.get('/aggregate', async (ctx) => {
  const [usersRes, productsRes, ordersRes] = await Promise.all([
    http.get(`${cfg.EXTERNAL_API_URL}/users`,    { headers: { Authorization: `Bearer ${cfg.EXTERNAL_API_KEY}` } }),
    http.get(`${cfg.EXTERNAL_API_URL}/products`, { headers: { Authorization: `Bearer ${cfg.EXTERNAL_API_KEY}` } }),
    http.get(`${cfg.EXTERNAL_API_URL}/orders`,   { headers: { Authorization: `Bearer ${cfg.EXTERNAL_API_KEY}` } }),
  ]);

  ctx.json({
    users:    usersRes.json(),
    products: productsRes.json(),
    orders:   ordersRes.json(),
  });
});

// ── WebSocket proxy ───────────────────────────────────────────────────────────

app.get('/ws/status', async (ctx) => {
  const ws = await websocket(cfg.WS_UPSTREAM, {
    headers: { Authorization: `Bearer ${cfg.EXTERNAL_API_KEY}` },
    timeoutMs: 5000,
  });

  await ws.send(JSON.stringify({ type: 'ping' }));

  const msg = await ws.receive();
  await ws.close();

  ctx.json({ received: msg.type === 'text' ? JSON.parse(msg.data as string) : null });
});

// ── Raw TCP socket ────────────────────────────────────────────────────────────

app.get('/smtp/ping', async (ctx) => {
  const sock = await connect({ host: 'smtp.gmail.com', port: 25, timeoutMs: 3000 });
  const banner = await sock.readLine();
  await sock.close();
  ctx.json({ banner: banner.trim() });
});

// ── DNS lookup ────────────────────────────────────────────────────────────────

app.get('/dns/:hostname', async (ctx) => {
  const hostname = ctx.params.hostname;

  const [aRecords, mxRecords, txtRecords] = await Promise.all([
    dns.resolve(hostname, 'A').catch(() => [] as string[]),
    dns.records(hostname, 'MX').catch(() => []),
    dns.records(hostname, 'TXT').catch(() => []),
  ]);

  ctx.json({
    hostname,
    a:   aRecords,
    mx:  mxRecords,
    txt: txtRecords,
  });
});

// ── Reverse DNS ───────────────────────────────────────────────────────────────

app.get('/rdns/:ip', async (ctx) => {
  const ip = ctx.params.ip;

  try {
    const hostnames = await dns.reverse(ip);
    ctx.json({ ip, hostnames });
  } catch {
    ctx.status(404).json({ error: 'no PTR record found' });
  }
});

// ── Health check with upstream ping ──────────────────────────────────────────

app.get('/health', async (ctx) => {
  const start = Date.now();
  let apiOk = false;

  try {
    const res = await http.get(`${cfg.EXTERNAL_API_URL}/health`, { timeout: 2000 });
    apiOk = res.ok;
  } catch {
    apiOk = false;
  }

  ctx.json({
    status:    apiOk ? 'ok' : 'degraded',
    upstreamMs: Date.now() - start,
    api:       apiOk,
  });
});

serve(app, { port: cfg.PORT });
