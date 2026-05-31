import type { IncomingMessage, ServerResponse } from 'node:http'
import type { Socket } from 'node:net'
import type { ZodType, ZodTypeAny } from 'zod'

// ── Options ───────────────────────────────────────────────────────────────────

export interface SymmifyOptions {
  logger?: boolean | LoggerOptions
  ignoreTrailingSlash?: boolean
  caseSensitive?: boolean
  bodyLimit?: number
  trustProxy?: boolean | number | string
  genReqId?: (req: IncomingMessage) => string
  requestIdHeader?: string
}

export interface LoggerOptions {
  level?: 'fatal' | 'error' | 'warn' | 'info' | 'debug' | 'trace'
  serializers?: Record<string, (val: unknown) => unknown>
}

// ── HTTP Methods ──────────────────────────────────────────────────────────────

export type HttpMethod =
  | 'GET'
  | 'POST'
  | 'PUT'
  | 'DELETE'
  | 'PATCH'
  | 'OPTIONS'
  | 'HEAD'
  | 'ALL'

// ── Schema ────────────────────────────────────────────────────────────────────

export interface RouteSchema {
  body?: ZodTypeAny
  querystring?: ZodTypeAny
  params?: ZodTypeAny
  headers?: ZodTypeAny
  response?: Record<number | string, ZodTypeAny>
}

// ── Validator / Serializer compiler ──────────────────────────────────────────

export interface ValidatorCompilerParams {
  schema: ZodTypeAny
  method: string
  url: string
  httpPart: 'body' | 'querystring' | 'params' | 'headers'
}

export type ValidatorCompiler = (
  params: ValidatorCompilerParams,
) => (data: unknown) => { value: unknown } | { error: unknown }

export interface SerializerCompilerParams {
  schema: ZodTypeAny
  method: string
  url: string
  httpStatus: string
}

export type SerializerCompiler = (
  params: SerializerCompilerParams,
) => (data: unknown) => string

// ── Route Options ─────────────────────────────────────────────────────────────

export interface RouteOptions<
  TBody = unknown,
  TQuerystring = unknown,
  TParams = unknown,
  THeaders = unknown,
> {
  method: HttpMethod | HttpMethod[]
  url: string
  schema?: RouteSchema
  attachValidation?: boolean
  validatorCompiler?: ValidatorCompiler
  serializerCompiler?: SerializerCompiler
  preValidation?: HookHandlerArray<'preValidation'>
  preHandler?: HookHandlerArray<'preHandler'>
  preSerialization?: HookHandlerArray<'preSerialization'>
  onSend?: HookHandlerArray<'onSend'>
  onResponse?: HookHandlerArray<'onResponse'>
  onError?: HookHandlerArray<'onError'>
  onTimeout?: HookHandlerArray<'onTimeout'>
  config?: unknown
  constraints?: Record<string, unknown>
  exposeHeadRoute?: boolean
  prefixTrailingSlash?: 'slash' | 'no-slash' | 'both'
  handler: RouteHandler<TBody, TQuerystring, TParams, THeaders>
}

// ── Request / Reply ───────────────────────────────────────────────────────────

export interface SymmifyRequest<
  TBody = unknown,
  TQuerystring = unknown,
  TParams = unknown,
  THeaders = unknown,
> {
  id: string
  params: TParams
  query: TQuerystring
  body: TBody
  headers: THeaders & Record<string, string | string[] | undefined>
  url: string
  routerPath: string
  routerMethod: string
  is404: boolean
  ip: string
  ips?: string[]
  hostname: string
  protocol: 'http' | 'https'
  socket: Socket
  validationError?: Error
  context: RouteContext
  routeConfig: unknown
  raw: IncomingMessage
  log: Logger
  server: SymmifyInstance
}

export interface SymmifyReply {
  code(statusCode: number): this
  status(statusCode: number): this
  statusCode: number
  header(key: string, value: string | string[]): this
  headers(values: Record<string, string | string[]>): this
  getHeader(key: string): string | string[] | undefined
  getHeaders(): Record<string, string | string[]>
  removeHeader(key: string): this
  hasHeader(key: string): boolean
  send(payload?: unknown): void
  type(contentType: string): this
  serialize(payload: unknown): string
  redirect(url: string, statusCode?: number): void
  sent: boolean
  raw: ServerResponse
  hijack(): this
  callNotFound(): void
  log: Logger
  server: SymmifyInstance
  context: RouteContext
  request: SymmifyRequest
}

// ── Route context ─────────────────────────────────────────────────────────────

export interface RouteContext {
  config: unknown
  schema: RouteSchema | undefined
  [key: string]: unknown
}

// ── Handlers ─────────────────────────────────────────────────────────────────

export type RouteHandler<
  TBody = unknown,
  TQuerystring = unknown,
  TParams = unknown,
  THeaders = unknown,
> = (
  request: SymmifyRequest<TBody, TQuerystring, TParams, THeaders>,
  reply: SymmifyReply,
) => void | unknown | Promise<void | unknown>

// ── Hook names ────────────────────────────────────────────────────────────────

export type RequestHookName =
  | 'onRequest'
  | 'preParsing'
  | 'preValidation'
  | 'preHandler'
  | 'preSerialization'
  | 'onSend'
  | 'onResponse'
  | 'onError'
  | 'onTimeout'

export type ServerHookName = 'onReady' | 'onClose' | 'onRoute' | 'onRegister'

export type HookName = RequestHookName | ServerHookName

// ── Hook handler signatures ───────────────────────────────────────────────────

export type HookHandler<N extends HookName> = N extends
  | 'onRequest'
  | 'preParsing'
  | 'preValidation'
  | 'preHandler'
  | 'onResponse'
  | 'onTimeout'
  ? (
      request: SymmifyRequest,
      reply: SymmifyReply,
      done: DoneFn,
    ) => void | Promise<void>
  : N extends 'preSerialization'
    ? (
        request: SymmifyRequest,
        reply: SymmifyReply,
        payload: unknown,
        done: DoneFn,
      ) => void | Promise<void>
    : N extends 'onSend'
      ? (
          request: SymmifyRequest,
          reply: SymmifyReply,
          serializedPayload: string | Buffer,
          done: DoneFn,
        ) => void | Promise<void>
      : N extends 'onError'
        ? (
            error: Error,
            request: SymmifyRequest,
            reply: SymmifyReply,
            done: DoneFn,
          ) => void | Promise<void>
        : N extends 'onReady' | 'onClose'
          ? (done: DoneFn) => void | Promise<void>
          : N extends 'onRoute'
            ? (routeOptions: RouteOptions) => void
            : N extends 'onRegister'
              ? (
                  instance: SymmifyInstance,
                  opts: Record<string, unknown>,
                  done: DoneFn,
                ) => void | Promise<void>
              : never

export type HookHandlerArray<N extends HookName> =
  | HookHandler<N>
  | HookHandler<N>[]

export type DoneFn = (err?: Error) => void

// ── Plugin ────────────────────────────────────────────────────────────────────

export type PluginFn<TOptions = Record<string, unknown>> = (
  instance: SymmifyInstance,
  opts: TOptions,
  done: DoneFn,
) => void | Promise<void>

// ── Logger ────────────────────────────────────────────────────────────────────

export interface Logger {
  fatal(msg: string | object, ...args: unknown[]): void
  error(msg: string | object, ...args: unknown[]): void
  warn(msg: string | object, ...args: unknown[]): void
  info(msg: string | object, ...args: unknown[]): void
  debug(msg: string | object, ...args: unknown[]): void
  trace(msg: string | object, ...args: unknown[]): void
  child(bindings: Record<string, unknown>): Logger
}

// ── Type Provider (Zod) ───────────────────────────────────────────────────────

export interface ZodTypeProvider {
  validator: ZodType
  serializer: ZodType
}

// ── Content Type Parser ───────────────────────────────────────────────────────

export interface ContentTypeParserOptions {
  bodyLimit?: number
  parseAs?: 'string' | 'buffer'
}

export type ContentTypeParserFn = (
  request: SymmifyRequest,
  payload: NodeJS.ReadableStream,
  done: (err: Error | null, body?: unknown) => void,
) => void

// ── Instance ──────────────────────────────────────────────────────────────────

export interface SymmifyInstance {
  (req: IncomingMessage, res: ServerResponse): void

  get(path: string, handler: RouteHandler): this
  get(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  post(path: string, handler: RouteHandler): this
  post(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  put(path: string, handler: RouteHandler): this
  put(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  delete(path: string, handler: RouteHandler): this
  delete(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  del(path: string, handler: RouteHandler): this
  del(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  patch(path: string, handler: RouteHandler): this
  patch(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  options(path: string, handler: RouteHandler): this
  options(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  head(path: string, handler: RouteHandler): this
  head(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  all(path: string, handler: RouteHandler): this
  all(
    path: string,
    opts: Omit<RouteOptions, 'method' | 'url' | 'handler'>,
    handler: RouteHandler,
  ): this
  route(opts: RouteOptions): this

  register(plugin: PluginFn, opts?: Record<string, unknown>): this

  addHook<N extends HookName>(name: N, handler: HookHandler<N>): this

  decorate(name: string, value: unknown): this
  decorateRequest(name: string, initialValue?: unknown): this
  decorateReply(name: string, value?: unknown): this
  hasDecorator(name: string): boolean
  hasRequestDecorator(name: string): boolean
  hasReplyDecorator(name: string): boolean

  setValidatorCompiler(compiler: ValidatorCompiler): this
  setSerializerCompiler(compiler: SerializerCompiler): this
  withTypeProvider<T extends ZodTypeProvider>(): this

  setErrorHandler(
    handler: (
      error: Error,
      request: SymmifyRequest,
      reply: SymmifyReply,
    ) => void | Promise<void>,
  ): this
  setNotFoundHandler(handler: RouteHandler): this

  addContentTypeParser(
    contentType: string | string[] | RegExp,
    opts: ContentTypeParserOptions,
    parser: ContentTypeParserFn,
  ): this
  addContentTypeParser(
    contentType: string | string[] | RegExp,
    parser: ContentTypeParserFn,
  ): this
  hasContentTypeParser(contentType: string): boolean
  removeContentTypeParser(contentType: string | string[]): this
  removeAllContentTypeParsers(): this

  ready(): Promise<void>
  listen(opts?: ListenOptions): Promise<string>
  close(): Promise<void>

  readonly prefix: string
  log: Logger
}

export interface ListenOptions {
  port?: number
  host?: string
  backlog?: number
  path?: string
  signal?: AbortSignal
}
