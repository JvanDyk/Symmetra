import type {
  SymmifyRequest,
  SymmifyOptions,
  RouteContext,
  Logger,
  SymmifyInstance,
} from './types.js'
import type { IncomingMessage } from 'node:http'
import type { Socket } from 'node:net'

function parseQueryString(url: string): Record<string, string | string[]> {
  const idx = url.indexOf('?')
  if (idx === -1) return {}

  const qs = url.slice(idx + 1)
  const params = new URLSearchParams(qs)
  const result: Record<string, string | string[]> = {}

  for (const [key, value] of params) {
    if (key in result) {
      const existing = result[key]
      result[key] = Array.isArray(existing)
        ? [...existing, value]
        : [existing, value]
    } else {
      result[key] = value
    }
  }

  return result
}

export class RequestImpl implements SymmifyRequest {
  id: string
  params: Record<string, string> = {}
  query: Record<string, string | string[]> = {}
  body: unknown = undefined
  headers: Record<string, string | string[] | undefined>
  url: string
  routerPath: string = ''
  routerMethod: string = ''
  is404: boolean = false
  ip: string
  ips?: string[]
  hostname: string
  protocol: 'http' | 'https'
  socket: Socket
  validationError?: Error
  context!: RouteContext
  routeConfig: unknown = undefined
  log!: Logger
  server!: SymmifyInstance
  raw: IncomingMessage

  constructor(raw: IncomingMessage, id: string, opts: SymmifyOptions) {
    this.raw = raw
    this.id = id
    this.headers = (raw.headers as Record<
      string,
      string | string[] | undefined
    >) ?? {}
    this.url = raw.url ?? '/'
    this.socket = raw.socket
    this.query = parseQueryString(this.url)
    this.ip = this.resolveIp(opts)
    this.hostname = this.resolveHostname()
    this.protocol = this.resolveProtocol()
  }

  private resolveIp(opts: SymmifyOptions): string {
    if (opts.trustProxy) {
      const xff = this.headers['x-forwarded-for']
      if (xff) {
        const first = Array.isArray(xff) ? xff[0] : xff
        return first.split(',')[0].trim()
      }
    }
    return this.socket.remoteAddress ?? '0.0.0.0'
  }

  private resolveHostname(): string {
    const host = this.headers['host']
    if (typeof host === 'string') {
      return host.split(':')[0]
    }
    return 'localhost'
  }

  private resolveProtocol(): 'http' | 'https' {
    return (this.raw.socket as any).encrypted ? 'https' : 'http'
  }
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return (
    value !== null &&
    typeof value === 'object' &&
    Object.getPrototypeOf(value) === Object.prototype
  )
}

function deepClone(value: unknown): unknown {
  if (typeof value !== 'object' || value === null) {
    return value
  }
  if (Array.isArray(value)) {
    return value.map((item) => deepClone(item))
  }
  if (isPlainObject(value)) {
    const result: Record<string, unknown> = {}
    for (const [k, v] of Object.entries(value)) {
      result[k] = deepClone(v)
    }
    return result
  }
  return value
}

export function applyRequestDecorators(
  req: RequestImpl,
  decorators: Map<string, unknown>,
): void {
  for (const [name, initialValue] of decorators) {
    let val: unknown
    if (isPlainObject(initialValue)) {
      val = deepClone(initialValue)
    } else if (Array.isArray(initialValue)) {
      val = [...initialValue]
    } else {
      val = initialValue
    }
    ;(req as any)[name] = val
  }
}
