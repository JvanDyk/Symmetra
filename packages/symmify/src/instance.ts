import type {
  SymmifyInstance,
  SymmifyOptions,
  RouteOptions,
  PluginFn,
  HookName,
  HookHandler,
  RouteHandler,
  ValidatorCompiler,
  SerializerCompiler,
  ListenOptions,
  Logger,
  SymmifyRequest,
  SymmifyReply,
} from './types.js'
import * as http from 'node:http'
import { createError, SymmifyError } from './error.js'
import { createLogger } from './logger.js'
import { HookRegistry, runRequestHooks, runServerHooks, runOnRouteHooks, runErrorHooks, runPreSerializationHooks, runOnSendHooks } from './hooks.js'
import { DecoratorRegistry } from './decorators.js'
import { ContentTypeRegistry, readBody } from './content-type.js'
import { RadixRouter, type CompiledRoute } from './router.js'
import { RequestImpl, applyRequestDecorators } from './request.js'
import { ReplyImpl } from './reply.js'
import {
  defaultValidatorCompiler,
  defaultSerializerCompiler,
  compileRouteSchemas,
  type CompiledSchemas,
} from './schema.js'
import { getPluginMeta, promisifyPlugin } from './plugin.js'

type InstanceState = 'initializing' | 'ready' | 'listening' | 'closed'

interface PendingPlugin {
  plugin: PluginFn
  opts: Record<string, unknown>
  skipOverride: boolean
}

const DEFAULT_BODY_LIMIT = 1048576

function genRequestId(req: http.IncomingMessage, opts: SymmifyOptions): string {
  const headerName = opts.requestIdHeader ?? 'request-id'
  const existing = req.headers[headerName]

  if (typeof existing === 'string') return existing
  if (Array.isArray(existing)) return existing[0] ?? ''

  if (opts.genReqId) {
    return opts.genReqId(req)
  }

  // Simple fallback: use a counter
  return `${Date.now()}-${Math.random().toString(36).slice(2)}`
}

export function symmify(opts: SymmifyOptions = {}): SymmifyInstance {
  let state: InstanceState = 'initializing'
  let server: http.Server | null = null
  const pendingPlugins: PendingPlugin[] = []
  let prefix = ''

  const hooks = new HookRegistry(null)
  const decorators = new DecoratorRegistry(null)
  const contentTypeParsers = new ContentTypeRegistry(null)
  const router = new RadixRouter({
    caseSensitive: opts.caseSensitive ?? true,
    ignoreTrailingSlash: opts.ignoreTrailingSlash ?? false,
  })
  const log = createLogger(opts.logger)

  let validatorCompiler: ValidatorCompiler = defaultValidatorCompiler
  let serializerCompiler: SerializerCompiler = defaultSerializerCompiler
  let errorHandler: (
    error: Error,
    req: SymmifyRequest,
    reply: SymmifyReply,
  ) => void | Promise<void> = defaultErrorHandler
  let notFoundHandler: RouteHandler = defaultNotFoundHandler

  // ── Request handler ────────────────────────────────────────────────────────

  async function handleRequest(
    rawReq: http.IncomingMessage,
    rawRes: http.ServerResponse,
  ): Promise<void> {
    const id = genRequestId(rawReq, opts)
    const reqImpl = new RequestImpl(rawReq, id, opts)
    const replyImpl = new ReplyImpl(rawRes)

    // Apply decorator initial values
    applyRequestDecorators(reqImpl, decorators.getAllRequestDecorators())
    for (const [name, value] of decorators.getAllReplyDecorators()) {
      ;(replyImpl as any)[name] = value
    }

    // Assign cross-refs
    reqImpl.server = instance as SymmifyInstance
    replyImpl.server = instance as SymmifyInstance
    replyImpl.request = reqImpl
    reqImpl.log = log.child({ reqId: id })
    replyImpl.log = reqImpl.log

    // Route lookup
    const method = rawReq.method ?? 'GET'
    const urlPath = (rawReq.url ?? '/').split('?')[0]
    const match = router.find(method, urlPath)

    if (!match) {
      reqImpl.is404 = true
      return runFullLifecycle(
        reqImpl,
        replyImpl,
        null,
        hooks,
        errorHandler,
        notFoundHandler,
        opts,
        rawReq,
      )
    }

    reqImpl.params = match.params
    reqImpl.routerPath = match.route.url
    reqImpl.routerMethod = method

    const context: any = {
      config: match.route.config,
      schema: match.route.schema,
    }
    reqImpl.context = context
    replyImpl.context = context

    return runFullLifecycle(
      reqImpl,
      replyImpl,
      match.route,
      hooks,
      errorHandler,
      notFoundHandler,
      opts,
      rawReq,
    )
  }

  // ── Full request lifecycle ─────────────────────────────────────────────────

  async function runFullLifecycle(
    req: RequestImpl,
    reply: ReplyImpl,
    route: CompiledRoute | null,
    registry: HookRegistry,
    errorHandler: (
      error: Error,
      req: SymmifyRequest,
      reply: SymmifyReply,
    ) => void | Promise<void>,
    notFoundHandler: RouteHandler,
    opts: SymmifyOptions,
    rawReq: http.IncomingMessage,
  ): Promise<void> {
    try {
      // Phase 1: onRequest
      await runRequestHooks(registry.get('onRequest'), req, reply)
      if (reply.sent) return

      // Phase 2: preParsing
      await runRequestHooks(registry.get('preParsing'), req, reply)
      if (reply.sent) return

      // Phase 3: Parse body
      if (
        route &&
        rawReq.method !== 'GET' &&
        rawReq.method !== 'HEAD'
      ) {
        const ct =
          (rawReq.headers['content-type'] as string) ?? 'application/json'
        const parser = contentTypeParsers.find(ct)
        if (parser) {
          try {
            const bodyStr = await readBody(
              rawReq,
              opts.bodyLimit ?? DEFAULT_BODY_LIMIT,
            )
            req.body = bodyStr
            // Parse the body if not already a string
            if (typeof req.body === 'string') {
              // Try to parse as JSON
              try {
                req.body = JSON.parse(req.body)
              } catch {
                // Keep as string if not valid JSON
              }
            }
          } catch (e) {
            if (e instanceof SymmifyError) throw e
            throw createError('FST_ERR_BODY_PARSE', String(e), 400)
          }
        }
      }

      // Phase 4: preValidation
      await runRequestHooks(registry.get('preValidation'), req, reply)
      if (reply.sent) return

      // Phase 5: Validate
      if (route && route.schema) {
        const validationErr = validateRequest(req, route, validatorCompiler)
        if (validationErr) {
          if (route.attachValidation) {
            req.validationError = validationErr
          } else {
            throw validationErr
          }
        }
      }

      // Phase 6: preHandler
      await runRequestHooks(registry.get('preHandler'), req, reply)
      if (reply.sent) return

      // Phase 7: Execute handler
      const handler = route?.handler ?? notFoundHandler
      let handlerResult: unknown

      try {
        handlerResult = await handler(req, reply)
      } catch (e) {
        throw e
      }

      // Phase 8-10: Serialization chain (if handler didn't send)
      if (!reply.sent && handlerResult !== undefined) {
        let payload: any = handlerResult

        // preSerialization
        payload = await runPreSerializationHooks(
          registry.get('preSerialization'),
          req,
          reply,
          payload,
        )

        // Serialize
        let serialized: string | Buffer
        if (typeof payload === 'string') {
          serialized = payload
        } else if (Buffer.isBuffer(payload)) {
          serialized = payload
        } else {
          // Use compiled serializer if available
          if (route && route.schema?.response) {
            const statusKey = String(reply.statusCode)
            // Try exact match, then 2xx/3xx/4xx/5xx pattern
            let serializer = route.compiledSchemas?.response?.get(statusKey)
            if (!serializer) {
              for (const [key, ser] of (
                route.compiledSchemas?.response ?? new Map()
              ).entries()) {
                if (
                  key === '2xx' ||
                  key === '3xx' ||
                  key === '4xx' ||
                  key === '5xx'
                ) {
                  if (
                    String(reply.statusCode).startsWith(key[0])
                  ) {
                    serializer = ser
                    break
                  }
                }
              }
            }
            if (serializer) {
              serialized = serializer(payload)
            } else {
              serialized = JSON.stringify(payload)
            }
          } else {
            serialized = JSON.stringify(payload)
          }
        }

        // onSend
        const finalPayload = await runOnSendHooks(
          registry.get('onSend'),
          req,
          reply,
          serialized,
        )

        // Send
        reply.send(finalPayload)
      }

      // Phase 11: onResponse (deferred)
      setImmediate(() => {
        runRequestHooks(registry.get('onResponse'), req, reply).catch((e) =>
          log.error('onResponse hook error', e),
        )
      })
    } catch (err) {
      // Error path
      const symErr =
        err instanceof SymmifyError
          ? err
          : createError('FST_ERR_GENERIC', String(err), 500)

      try {
        await runErrorHooks(registry.get('onError'), symErr, req, reply)
      } catch (hookErr) {
        log.error('onError hook threw', hookErr)
      }

      if (!reply.sent) {
        try {
          await errorHandler(symErr, req, reply)
        } catch (handlerErr) {
          log.error('Error handler threw', handlerErr)
          if (!reply.sent) {
            reply.code(500).send({ error: 'Internal Server Error' })
          }
        }
      }
    }
  }

  // ── Route registration ────────────────────────────────────────────────────

  function addRoute(opts: RouteOptions): void {
    if (state !== 'initializing') {
      throw createError(
        'FST_ERR_ALREADY_STARTED',
        'Cannot add routes after listen()',
        500,
      )
    }

    const compiledSchemas = compileRouteSchemas(
      opts.schema,
      Array.isArray(opts.method) ? opts.method[0] : opts.method,
      opts.url,
      opts.validatorCompiler ?? validatorCompiler,
      opts.serializerCompiler ?? serializerCompiler,
    )

    const compiled: CompiledRoute = {
      handler: opts.handler,
      schema: opts.schema,
      config: opts.config,
      constraints: opts.constraints ?? {},
      attachValidation: opts.attachValidation ?? false,
      url: prefix + opts.url,
    }
    ;(compiled as any).compiledSchemas = compiledSchemas

    const methods = Array.isArray(opts.method) ? opts.method : [opts.method]
    for (const m of methods) {
      router.insert(m === 'ALL' ? 'ALL' : m, prefix + opts.url, compiled)

      // Auto HEAD for GET
      if (
        m === 'GET' &&
        (opts.exposeHeadRoute ?? true)
      ) {
        router.insert('HEAD', prefix + opts.url, compiled)
      }
    }

    // Fire onRoute hooks (synchronously)
    runOnRouteHooks(hooks.get('onRoute'), opts)
  }

  // ── Lifecycle methods ──────────────────────────────────────────────────────

  async function ready(): Promise<void> {
    if (state === 'ready' || state === 'listening') return
    if (state === 'closed') {
      throw createError(
        'FST_ERR_ALREADY_CLOSED',
        'Instance is closed',
        500,
      )
    }

    await bootPlugins()

    // Fire onReady hooks
    await runServerHooks(hooks.get('onReady'))

    state = 'ready'
  }

  async function bootPlugins(): Promise<void> {
    for (const { plugin, opts: pluginOpts, skipOverride } of pendingPlugins) {
      const target = skipOverride
        ? (instance as any)
        : createChildScope()

      // Fire onRegister hooks
      for (const hook of hooks.get('onRegister')) {
        await new Promise<void>((resolve, reject) => {
          const result = hook(target, pluginOpts, (err) => {
            if (err) reject(err)
            else resolve()
          })
          if (result && typeof result.then === 'function') {
            result.then(() => resolve(), reject)
          }
        })
      }

      // Execute plugin
      await promisifyPlugin(plugin, target, pluginOpts)

      // Boot child's plugins recursively
      if (!skipOverride) {
        await bootPlugins.call(target)
      }
    }

    pendingPlugins.length = 0
  }

  function createChildScope(): any {
    const childHooks = hooks.createChild()
    const childDecorators = decorators.createChild()
    const childContentTypeParsers = contentTypeParsers.createChild()

    return createInstanceWithDeps(
      opts,
      childHooks,
      childDecorators,
      childContentTypeParsers,
      router,
      prefix,
      validatorCompiler,
      serializerCompiler,
    )
  }

  async function listen(listenOpts: ListenOptions = {}): Promise<string> {
    await ready()

    const port = listenOpts.port ?? 3000
    const host = listenOpts.host ?? '127.0.0.1'

    return new Promise((resolve, reject) => {
      server = http.createServer(instance as any)

      server.once('error', reject)

      server.listen(port, host, listenOpts.backlog, () => {
        const addr = server!.address()
        const url =
          typeof addr === 'string'
            ? addr
            : `http://${addr?.address ?? host}:${addr?.port ?? port}`
        state = 'listening'
        resolve(url)
      })

      if (listenOpts.signal) {
        listenOpts.signal.addEventListener('abort', () => {
          close().catch(() => {})
        })
      }
    })
  }

  async function close(): Promise<void> {
    if (state === 'closed') return

    // Fire onClose hooks
    await runServerHooks(hooks.get('onClose'))

    if (server) {
      await new Promise<void>((resolve, reject) => {
        server!.close((err) => (err ? reject(err) : resolve()))
      })
    }

    state = 'closed'
  }

  // ── Create instance function with all methods ────────────────────────────

  const instance = function (
    req: http.IncomingMessage,
    res: http.ServerResponse,
  ) {
    handleRequest(req, res).catch((err) => {
      log.error('Unhandled error in request listener', err)
      if (!res.headersSent) {
        res.writeHead(500)
        res.end('Internal Server Error')
      }
    })
  }

  const methodFactory = (method: string) => {
    return function (
      path: string,
      optsOrHandler?: RouteOptions | RouteHandler,
      maybeHandler?: RouteHandler,
    ): SymmifyInstance {
      const handler =
        typeof optsOrHandler === 'function' ? optsOrHandler : maybeHandler!
      const opts =
        typeof optsOrHandler === 'object' ? optsOrHandler : {}

      addRoute({
        method,
        url: path,
        ...opts,
        handler,
      } as RouteOptions)

      return instance as unknown as SymmifyInstance
    }
  }

  Object.assign(instance, {
    get: methodFactory('GET'),
    post: methodFactory('POST'),
    put: methodFactory('PUT'),
    delete: methodFactory('DELETE'),
    del: methodFactory('DELETE'),
    patch: methodFactory('PATCH'),
    options: methodFactory('OPTIONS'),
    head: methodFactory('HEAD'),
    all: methodFactory('ALL'),

    route(routeOpts: RouteOptions): SymmifyInstance {
      addRoute(routeOpts)
      return instance as unknown as SymmifyInstance
    },

    register(
      plugin: PluginFn,
      pluginOpts?: Record<string, unknown>,
    ): SymmifyInstance {
      const meta = getPluginMeta(plugin)
      const skipOverride = meta?.skipOverride ?? false

      pendingPlugins.push({
        plugin,
        opts: pluginOpts ?? {},
        skipOverride,
      })

      return instance as unknown as SymmifyInstance
    },

    addHook<N extends HookName>(
      name: N,
      handler: HookHandler<N>,
    ): SymmifyInstance {
      if (Array.isArray(handler)) {
        for (const h of handler) {
          hooks.add(name, h)
        }
      } else {
        hooks.add(name, handler)
      }
      return instance as unknown as SymmifyInstance
    },

    decorate(name: string, value: unknown): SymmifyInstance {
      decorators.decorate(name, value, instance)
      return instance as unknown as SymmifyInstance
    },

    decorateRequest(name: string, initialValue?: unknown): SymmifyInstance {
      decorators.decorateRequest(name, initialValue)
      return instance as unknown as SymmifyInstance
    },

    decorateReply(name: string, value?: unknown): SymmifyInstance {
      decorators.decorateReply(name, value)
      return instance as unknown as SymmifyInstance
    },

    hasDecorator(name: string): boolean {
      return decorators.hasDecorator(name)
    },

    hasRequestDecorator(name: string): boolean {
      return decorators.hasRequestDecorator(name)
    },

    hasReplyDecorator(name: string): boolean {
      return decorators.hasReplyDecorator(name)
    },

    setValidatorCompiler(
      compiler: ValidatorCompiler,
    ): SymmifyInstance {
      validatorCompiler = compiler
      return instance as unknown as SymmifyInstance
    },

    setSerializerCompiler(
      compiler: SerializerCompiler,
    ): SymmifyInstance {
      serializerCompiler = compiler
      return instance as unknown as SymmifyInstance
    },

    withTypeProvider<T>(): SymmifyInstance {
      return instance as unknown as SymmifyInstance
    },

    setErrorHandler(
      handler: (
        error: Error,
        req: SymmifyRequest,
        reply: SymmifyReply,
      ) => void | Promise<void>,
    ): SymmifyInstance {
      errorHandler = handler
      return instance as unknown as SymmifyInstance
    },

    setNotFoundHandler(handler: RouteHandler): SymmifyInstance {
      notFoundHandler = handler
      return instance as unknown as SymmifyInstance
    },

    addContentTypeParser(
      contentType: string | string[] | RegExp,
      optsOrFn?: any,
      fn?: any,
    ): SymmifyInstance {
      contentTypeParsers.add(contentType, optsOrFn, fn)
      return instance as unknown as SymmifyInstance
    },

    hasContentTypeParser(contentType: string): boolean {
      return contentTypeParsers.has(contentType)
    },

    removeContentTypeParser(contentType: string | string[]): SymmifyInstance {
      contentTypeParsers.remove(contentType)
      return instance as unknown as SymmifyInstance
    },

    removeAllContentTypeParsers(): SymmifyInstance {
      contentTypeParsers.removeAll()
      return instance as unknown as SymmifyInstance
    },

    ready,
    listen,
    close,

    get prefix(): string {
      return prefix
    },

    log,
  })

  return instance as unknown as SymmifyInstance
}

function createInstanceWithDeps(
  opts: SymmifyOptions,
  hooks: HookRegistry,
  decorators: DecoratorRegistry,
  contentTypeParsers: ContentTypeRegistry,
  router: RadixRouter,
  prefix: string,
  validatorCompiler: ValidatorCompiler,
  serializerCompiler: SerializerCompiler,
): any {
  // Create a new instance with shared deps
  // This is used for child scopes
  const childInstance = symmify(opts)
  // Override the registries...
  // This is complex; for now, return a basic instance
  return childInstance
}

function validateRequest(
  req: RequestImpl,
  route: CompiledRoute,
  validatorCompiler: ValidatorCompiler,
): SymmifyError | null {
  const compiled = (route as any).compiledSchemas as CompiledSchemas

  // Validate params
  if (compiled.params) {
    const result = compiled.params(req.params)
    if ('error' in result && result.error) {
      return createError('FST_ERR_VALIDATION', String(result.error), 400)
    }
    if ('value' in result) {
      req.params = result.value as Record<string, string>
    }
  }

  // Validate query
  if (compiled.querystring) {
    const result = compiled.querystring(req.query)
    if ('error' in result && result.error) {
      return createError('FST_ERR_VALIDATION', String(result.error), 400)
    }
    if ('value' in result) {
      req.query = result.value as Record<string, string | string[]>
    }
  }

  // Validate headers
  if (compiled.headers) {
    const result = compiled.headers(req.headers)
    if ('error' in result && result.error) {
      return createError('FST_ERR_VALIDATION', String(result.error), 400)
    }
    if ('value' in result) {
      req.headers = result.value as Record<string, string | string[] | undefined>
    }
  }

  // Validate body
  if (compiled.body) {
    const result = compiled.body(req.body)
    if ('error' in result && result.error) {
      return createError('FST_ERR_VALIDATION', String(result.error), 400)
    }
    if ('value' in result) {
      req.body = result.value
    }
  }

  return null
}

function defaultErrorHandler(
  _error: Error,
  _req: SymmifyRequest,
  reply: SymmifyReply,
): void {
  const err = _error as any
  reply
    .code(err.statusCode ?? 500)
    .send({
      error: err.name ?? 'Error',
      message: err.message ?? 'Unknown error',
      statusCode: err.statusCode ?? 500,
    })
}

function defaultNotFoundHandler(
  req: SymmifyRequest,
  reply: SymmifyReply,
): void {
  reply
    .code(404)
    .send({
      error: 'Not Found',
      message: `Route ${req.raw.method} ${req.url} not found`,
      statusCode: 404,
    })
}
