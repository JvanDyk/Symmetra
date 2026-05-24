# `@symmetra/net` — HTTP Client + DNS Reference

Outbound HTTP requests and DNS resolution from inside GPU handlers. Network calls are executed on the CPU thread pool — the GPU handler suspends (via stream synchronization) until the response is available.

## Installation

```typescript
import { fetch, dns } from '@symmetra/net';
```

---

## `fetch`

A subset of the Web Fetch API. Works identically to browser `fetch` from inside Symmetra handlers.

### `fetch(url: string, options?: FetchOptions): Promise<Response>`

Make an outbound HTTP request.

```typescript
const resp = await fetch('https://api.example.com/data');
const json = await resp.json();
ctx.json(json);
```

#### `FetchOptions`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `method` | `string` | `'GET'` | HTTP method |
| `headers` | `Record<string, string>` | `{}` | Request headers |
| `body` | `string \| Uint8Array` | — | Request body |
| `signal` | `AbortSignal` | — | Abort signal |
| `timeoutMs` | `number` | `30000` | Request timeout in milliseconds |
| `followRedirects` | `boolean` | `true` | Follow 3xx redirects |
| `maxRedirects` | `number` | `5` | Maximum number of redirects |

#### `Response`

| Property / Method | Type | Description |
|-------------------|------|-------------|
| `status` | `number` | HTTP status code |
| `statusText` | `string` | HTTP status text |
| `ok` | `boolean` | True if status is 200–299 |
| `headers` | `Headers` | Response headers |
| `json<T>()` | `Promise<T>` | Parse body as JSON |
| `text()` | `Promise<string>` | Read body as UTF-8 string |
| `arrayBuffer()` | `Promise<ArrayBuffer>` | Read body as binary |
| `url` | `string` | Final URL after redirects |

```typescript
const resp = await fetch('https://api.github.com/repos/octocat/hello-world');
if (!resp.ok) {
  ctx.status(502).json({ error: `upstream ${resp.status}` });
  return;
}
const repo = await resp.json<{ stargazers_count: number }>();
ctx.json({ stars: repo.stargazers_count });
```

---

## POST / PUT requests

```typescript
const resp = await fetch('https://hooks.example.com/notify', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ event: 'user.signup', id: userId }),
});
```

---

## Abort / timeout

```typescript
const controller = new AbortController();
const timeout = setTimeout(() => controller.abort(), 5000);

try {
  const resp = await fetch('https://slow-api.example.com/', {
    signal: controller.signal,
  });
  clearTimeout(timeout);
  ctx.json(await resp.json());
} catch (e) {
  ctx.status(504).json({ error: 'gateway timeout' });
}
```

Or use the built-in `timeoutMs` option:

```typescript
const resp = await fetch('https://api.example.com/', { timeoutMs: 3000 });
```

---

## DNS

### `dns.lookup(hostname: string): Promise<string>`

Resolve a hostname to its first IPv4 address.

```typescript
const ip = await dns.lookup('api.example.com');
ctx.json({ ip });
```

### `dns.resolve(hostname: string): Promise<string[]>`

Resolve a hostname to all IP addresses.

```typescript
const ips = await dns.resolve('api.example.com');
ctx.json({ ips });
```

### `dns.reverse(ip: string): Promise<string[]>`

Reverse-DNS lookup: IP → hostname(s).

```typescript
const names = await dns.reverse('8.8.8.8');
// ["dns.google"]
```

---

## Error handling

All network errors throw `NetError`:

```typescript
import { NetError } from '@symmetra/net';

try {
  const resp = await fetch('https://unreachable.internal/');
} catch (e) {
  if (e instanceof NetError) {
    ctx.status(502).json({ error: e.message, code: e.code });
  }
}
```

#### `NetError`

| Property | Type | Description |
|----------|------|-------------|
| `message` | `string` | Human-readable error |
| `code` | `string` | `'ECONNREFUSED'`, `'ETIMEDOUT'`, `'ENOTFOUND'`, … |

---

## Complete example

```typescript
import { serve, Router } from '@symmetra/http';
import { fetch } from '@symmetra/net';

const app = new Router();

// Proxy / aggregator: calls two upstream APIs in parallel
app.get('/summary/:user', async (ctx) => {
  const user = ctx.params.user;
  const base = 'https://api.example.com';

  const [profileResp, activityResp] = await Promise.all([
    fetch(`${base}/users/${user}`),
    fetch(`${base}/activity/${user}`),
  ]);

  if (!profileResp.ok) {
    ctx.status(profileResp.status).json({ error: 'profile fetch failed' });
    return;
  }

  const profile  = await profileResp.json();
  const activity = activityResp.ok ? await activityResp.json() : null;

  ctx.json({ profile, activity });
});

// Webhook forwarder
app.post('/webhook', async (ctx) => {
  const body = await ctx.text();
  await fetch('https://hooks.slack.com/services/YOUR/HOOK/URL', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body,
  });
  ctx.status(204).text('');
});

serve(app, { port: 8080 });
```
