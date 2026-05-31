# @symmetra/symmify

Drop-in **Fastify-compatible HTTP framework** with **Zod-native validation** for modern API design.

Symmify provides the developer experience of Fastify—clean routing, decorators, plugins, and lifecycle hooks—combined with Zod as the built-in validator (instead of JSON Schema). It runs on Node.js `http` module, making it compatible with any HTTP framework ecosystem.

## Features

- **Fastify API compatibility**: Copy your Fastify code and use it with symmify without changes
- **Zod-native validation**: All schemas use Zod by default (no JSON Schema complexity)
- **Plugin system**: Encapsulated scopes for plugins with parent-chain decorator inheritance
- **Full lifecycle hooks**: `onRequest`, `preParsing`, `preValidation`, `preHandler`, `preSerialization`, `onSend`, `onResponse`, `onError`, `onReady`, `onClose`, and more
- **Decorators**: Add request and reply decorators for clean context passing
- **Content-type parsers**: Built-in JSON and plain-text parsers; register custom ones
- **TypeScript-first**: Strict types with full generic support
- **Error handling**: Global and per-route error handlers with proper propagation
- **Lightweight**: No external dependencies besides Zod

## Installation

```bash
npm install @symmetra/symmify zod
```

## Quick Start

```typescript
import symmify from '@symmetra/symmify'
import { z } from 'zod'

const app = symmify()

app.get('/', (req, reply) => {
  reply.send({ hello: 'world' })
})

app.post(
  '/users',
  {
    schema: {
      body: z.object({
        name: z.string(),
        email: z.string().email(),
      }),
    },
  },
  async (req, reply) => {
    // req.body is type-safe and validated
    reply.code(201).send({ user: req.body })
  }
)

await app.listen({ port: 3000 })
console.log('Server running on http://localhost:3000')
```

## API

### Factory

```typescript
const app = symmify(options?: SymmifyOptions)
```

**Options:**
- `logger?: boolean | LoggerOptions` — Enable/disable logging
- `ignoreTrailingSlash?: boolean` — Ignore trailing `/` in routes (default: false)
- `caseSensitive?: boolean` — Case-sensitive routing (default: true)
- `bodyLimit?: number` — Max body size in bytes (default: 1 MB)
- `trustProxy?: boolean` — Trust `X-Forwarded-*` headers for IP/protocol
- `genReqId?: (req) => string` — Custom request ID generator
- `requestIdHeader?: string` — Header name for request ID (default: "request-id")

### Route Methods

```typescript
app.get(path, handler)
app.get(path, options, handler)
app.post(path, handler)
app.post(path, options, handler)
app.put(path, options, handler)
app.delete(path, options, handler)
app.patch(path, options, handler)
app.options(path, options, handler)
app.head(path, options, handler)
app.all(path, options, handler)
app.route(routeOptions)
```

### Route Options

```typescript
interface RouteOptions {
  method: 'GET' | 'POST' | 'PUT' | 'DELETE' | 'PATCH' | 'HEAD' | 'OPTIONS' | 'ALL'
  url: string
  schema?: {
    body?: z.ZodType
    querystring?: z.ZodType
    params?: z.ZodType
    headers?: z.ZodType
    response?: Record<number | string, z.ZodType>
  }
  handler: (req: SymmifyRequest, reply: SymmifyReply) => void | Promise<void>
  // Hooks (per-route)
  preValidation?: HookHandler | HookHandler[]
  preHandler?: HookHandler | HookHandler[]
  preSerialization?: HookHandler | HookHandler[]
  onSend?: HookHandler | HookHandler[]
  onResponse?: HookHandler | HookHandler[]
  onError?: HookHandler | HookHandler[]
  // Other options
  config?: unknown
  attachValidation?: boolean
  exposeHeadRoute?: boolean
}
```

### Request Object

```typescript
interface SymmifyRequest {
  id: string
  params: Record<string, string>
  query: Record<string, string | string[]>
  body: unknown
  headers: Record<string, string | string[] | undefined>
  url: string
  routerPath: string
  routerMethod: string
  is404: boolean
  ip: string
  hostname: string
  protocol: 'http' | 'https'
  socket: Socket
  log: Logger
  server: SymmifyInstance
}
```

### Reply Object

```typescript
interface SymmifyReply {
  code(statusCode: number): this
  status(statusCode: number): this
  statusCode: number
  header(key: string, value: string | string[]): this
  headers(obj: Record<string, string | string[]>): this
  getHeader(key: string): string | string[] | undefined
  getHeaders(): Record<string, string | string[]>
  hasHeader(key: string): boolean
  removeHeader(key: string): this
  send(payload?: unknown): void
  type(contentType: string): this
  redirect(url: string, statusCode?: number): void
  hijack(): this
  callNotFound(): void
  sent: boolean
  raw: ServerResponse
  log: Logger
  server: SymmifyInstance
}
```

### Plugins & Scopes

```typescript
app.register(async (scope, opts) => {
  scope.get('/health', (req, reply) => {
    reply.send({ ok: true })
  })
}, { prefix: '/api' })

// Use fp() to escape scope isolation
import { fp } from '@symmetra/symmify'

app.register(fp(async (instance, opts) => {
  // Decorates the parent scope directly
  instance.decorate('config', { apiKey: '...' })
}))
```

### Decorators

```typescript
// Instance decorator
app.decorate('config', { env: 'production' })

// Request decorator
app.decorateRequest('user', null)
app.addHook('preHandler', (req, reply, done) => {
  req.user = { id: 1, name: 'Alice' }
  done()
})

// Reply decorator
app.decorateReply('sendSuccess', function(data) {
  return this.code(200).send({ success: true, data })
})
```

### Lifecycle Hooks

```typescript
// Request hooks
app.addHook('onRequest', (req, reply, done) => {
  console.log(`${req.routerMethod} ${req.url}`)
  done()
})

app.addHook('preHandler', async (req, reply) => {
  // Auth check, etc.
})

app.addHook('onSend', async (req, reply, payload) => {
  // Modify payload before sending
  return payload
})

// Server hooks
app.addHook('onReady', async () => {
  console.log('Server is ready')
})

app.addHook('onClose', async () => {
  console.log('Server closing')
})
```

### Error Handling

```typescript
app.setErrorHandler((error, req, reply) => {
  req.log.error(error)
  reply.code(error.statusCode || 500).send({
    error: error.name,
    message: error.message,
  })
})

app.setNotFoundHandler((req, reply) => {
  reply.code(404).send({
    error: 'Not Found',
    message: `${req.routerMethod} ${req.url} not found`,
  })
})
```

### Lifecycle

```typescript
// Boot plugins and run onReady hooks
await app.ready()

// Start listening
const url = await app.listen({ port: 3000, host: '0.0.0.0' })

// Graceful shutdown
await app.close()
```

## Zod Integration

Symmify uses Zod for all schema validation:

```typescript
import { z } from 'zod'

app.post('/login', {
  schema: {
    body: z.object({
      email: z.string().email(),
      password: z.string().min(8),
    }),
    response: {
      200: z.object({
        token: z.string(),
      }),
    },
  },
  handler: async (req, reply) => {
    // req.body is validated and typed
    const { email, password } = req.body
    // ...
    reply.send({ token: '...' })
  },
})
```

## Content-Type Parsers

```typescript
// Built-in: application/json, text/plain

// Add custom parser
app.addContentTypeParser(
  'application/x-www-form-urlencoded',
  { parseAs: 'string' },
  (req, payload, done) => {
    const data = Object.fromEntries(new URLSearchParams(payload))
    done(null, data)
  }
)
```

## License

MIT
