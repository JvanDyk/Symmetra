import type {
  SymmifyReply,
  SymmifyRequest,
  RouteContext,
  Logger,
  SymmifyInstance,
} from './types.js'
import type { ServerResponse } from 'node:http'
import { createError } from './error.js'

export class ReplyImpl implements SymmifyReply {
  statusCode: number = 200
  sent: boolean = false
  raw: ServerResponse
  log!: Logger
  server!: SymmifyInstance
  context!: RouteContext
  request!: SymmifyRequest

  private _headers: Record<string, string | string[]> = {}
  private _type: string = 'application/json'
  private _hijacked: boolean = false

  constructor(raw: ServerResponse) {
    this.raw = raw
  }

  code(n: number): this {
    this.statusCode = n
    return this
  }

  status(n: number): this {
    return this.code(n)
  }

  header(key: string, value: string | string[]): this {
    this._headers[key.toLowerCase()] = value
    return this
  }

  headers(values: Record<string, string | string[]>): this {
    for (const [k, v] of Object.entries(values)) {
      this.header(k, v)
    }
    return this
  }

  getHeader(key: string): string | string[] | undefined {
    return this._headers[key.toLowerCase()]
  }

  getHeaders(): Record<string, string | string[]> {
    return { ...this._headers }
  }

  removeHeader(key: string): this {
    delete this._headers[key.toLowerCase()]
    return this
  }

  hasHeader(key: string): boolean {
    return key.toLowerCase() in this._headers
  }

  type(ct: string): this {
    this._type = ct
    return this
  }

  hijack(): this {
    this._hijacked = true
    this.sent = true
    return this
  }

  redirect(url: string, statusCode = 302): void {
    this.code(statusCode).header('location', url).send()
  }

  callNotFound(): void {
    throw createError('FST_ERR_NOT_FOUND', 'Not Found', 404)
  }

  serialize(payload: unknown): string {
    return JSON.stringify(payload)
  }

  send(payload?: unknown): void {
    if (this.sent) {
      throw createError('FST_ERR_REP_ALREADY_SENT', 'Reply already sent', 500)
    }
    if (this._hijacked) return

    this.sent = true
    this._sendPayload(payload)
  }

  private _sendPayload(payload: unknown): void {
    const res = this.raw

    // Apply all accumulated headers
    for (const [k, v] of Object.entries(this._headers)) {
      res.setHeader(k, v)
    }

    // Always set request-id header (Fastify behavior)
    if (!this.hasHeader('request-id')) {
      res.setHeader('request-id', this.request.id)
    }

    // Handle null/undefined
    if (payload === undefined || payload === null) {
      res.writeHead(this.statusCode)
      res.end()
      return
    }

    // Handle Buffer
    if (Buffer.isBuffer(payload)) {
      if (!this.hasHeader('content-type')) {
        res.setHeader('content-type', 'application/octet-stream')
      }
      res.writeHead(this.statusCode)
      res.end(payload)
      return
    }

    // Handle string
    if (typeof payload === 'string') {
      if (!this.hasHeader('content-type')) {
        res.setHeader('content-type', 'text/plain; charset=utf-8')
      }
      const buf = Buffer.from(payload, 'utf8')
      res.setHeader('content-length', buf.byteLength)
      res.writeHead(this.statusCode)
      res.end(buf)
      return
    }

    // Handle Error
    if (payload instanceof Error) {
      throw payload
    }

    // Default: JSON serialization
    const serialized = this._serializeJson(payload)
    const buf = Buffer.from(serialized, 'utf8')

    if (!this.hasHeader('content-type')) {
      res.setHeader('content-type', 'application/json; charset=utf-8')
    }

    res.setHeader('content-length', buf.byteLength)
    res.writeHead(this.statusCode)
    res.end(buf)
  }

  private _serializeJson(payload: unknown): string {
    // Use compiled serializer if available in context
    const serializer = (this.context as any)?._serializer
    if (typeof serializer === 'function') {
      return serializer(payload)
    }
    return JSON.stringify(payload)
  }
}
