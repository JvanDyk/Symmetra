# `@symmetra/http` — Router + SymmCtx Reference

## Installation

No separate install. `@symmetra/http` is a type-only package bundled with `sym`.

```typescript
import { serve, Router } from '@symmetra/http';
```

---

## `Router`

The main routing object. Attach handlers with `.get()`, `.post()`, `.put()`, `.delete()`, `.patch()`.

```typescript
const app = new Router();
```

### Route registration

```typescript
app.get(path: string, handler: (ctx: SymmCtx) => void | Promise<void>): void
app.post(path: string, handler: (ctx: SymmCtx) => void | Promise<void>): void
app.put(path: string, handler: (ctx: SymmCtx) => void | Promise<void>): void
app.delete(path: string, handler: (ctx: SymmCtx) => void | Promise<void>): void
app.patch(path: string, handler: (ctx: SymmCtx) => void | Promise<void>): void
```

**Path parameters** are captured with `:name` syntax:

```typescript
app.get('/users/:id', (ctx) => {
  ctx.json({ id: ctx.params.id });
});
```

**Wildcard** routes are not yet supported. Use explicit parameter segments.

---

## `serve(router, options)`

Starts the HTTP server. Blocks until `Ctrl-C`.

```typescript
serve(app: Router, options?: ServeOptions): void
```

### `ServeOptions`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `port` | `number` | `8080` | TCP port to listen on |
| `streams` | `number` | `32` | CUDA stream pool size — maximum concurrent requests |
| `ioThreads` | `number` | `streams` | IO thread count; should equal `streams` |
| `verbose` | `boolean` | `false` | Print generated CUDA C++ and NVRTC output at startup |

```typescript
serve(app, {
  port:      3000,
  streams:   64,   // handle 64 requests concurrently
  verbose:   false,
});
```

---

## `SymmCtx`

Available as the sole argument to every route handler. Wraps the per-request GPU context.

### Request properties

#### `ctx.method: string`

HTTP method of the incoming request (`"GET"`, `"POST"`, etc.).

#### `ctx.path: string`

URL path without query string (e.g. `"/users/42"`).

#### `ctx.params: Record<string, string>`

Named path parameters captured from the route pattern.

```typescript
app.get('/item/:id', (ctx) => {
  const id = ctx.params.id;   // "42" for /item/42
});
```

#### `ctx.query: Record<string, string>`

Parsed query string parameters.

```typescript
// GET /search?q=foo&limit=10
app.get('/search', (ctx) => {
  const q     = ctx.query.q;       // "foo"
  const limit = ctx.query.limit;   // "10"
});
```

#### `ctx.headers: Record<string, string>`

Incoming request headers (lower-cased keys).

```typescript
const auth = ctx.headers['authorization'];
```

### Request body

#### `ctx.json<T>(): Promise<T>`

Parse the request body as JSON. Rejects if the body is not valid JSON.

```typescript
app.post('/echo', async (ctx) => {
  const body = await ctx.json<{ name: string }>();
  ctx.json({ hello: body.name });
});
```

#### `ctx.body<Float32Array>(): Promise<Float32Array>`

Read the request body as a typed binary array already placed in VRAM. Supported types: `Float32Array`, `Float64Array`, `Int32Array`, `Uint8Array`, `Uint32Array`.

```typescript
app.post('/transform', async (ctx) => {
  const data = await ctx.body<Float32Array>();
  const out  = data.map(x => x * x);
  ctx.json(out);
});
```

#### `ctx.text(): Promise<string>`

Read the request body as a UTF-8 string.

```typescript
app.post('/echo-text', async (ctx) => {
  const text = await ctx.text();
  ctx.text(text.toUpperCase());
});
```

### Response methods

All response methods return `ctx` for optional chaining.

#### `ctx.json(value: unknown): SymmCtx`

Serialize `value` to JSON and send with `Content-Type: application/json`.

```typescript
ctx.json({ ok: true, count: 42 });
ctx.json([1, 2, 3]);
ctx.json("hello");
```

#### `ctx.text(body: string, contentType?: string): SymmCtx`

Send a plain-text response.

```typescript
ctx.text('Hello, world!');
ctx.text('<h1>Hi</h1>', 'text/html');
```

#### `ctx.status(code: number): SymmCtx`

Set the HTTP status code. Must be called before `json()` / `text()`.

```typescript
ctx.status(404).json({ error: 'not found' });
ctx.status(201).json({ created: true });
```

#### `ctx.header(name: string, value: string): SymmCtx`

Set a response header.

```typescript
ctx.header('X-Request-Id', '123').json({ ok: true });
```

### GPU access

#### `ctx.gpu`

Exposes GPU-specific operations.

##### `ctx.gpu.fingerprint(input: string, options?: FingerprintOptions): Promise<bigint>`

Run the cellular-automaton GPU hash on `input`. Returns a 64-bit digest as `bigint`.

```typescript
const fp = await ctx.gpu.fingerprint('hello');
ctx.json({ hex: fp.toString(16) });
```

`FingerprintOptions`:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `engine` | `'APEX' \| 'OMEGA'` | `'APEX'` | Hash engine variant |
| `rounds` | `number` | `16` | Number of CA evolution steps |

---

## Complete example

```typescript
import { serve, Router } from '@symmetra/http';

const app = new Router();

app.get('/health', (ctx) => {
  ctx.json({ status: 'ok' });
});

app.post('/transform', async (ctx) => {
  const data = await ctx.body<Float32Array>();
  const out  = data.map(x => Math.sqrt(x));
  ctx.json(out);
});

app.get('/item/:id', async (ctx) => {
  if (ctx.params.id === '0') {
    ctx.status(404).json({ error: 'not found' });
    return;
  }
  ctx.json({ id: ctx.params.id });
});

serve(app, { port: 8080, streams: 32 });
```
