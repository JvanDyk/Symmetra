// @symmetra/net
// Raw TCP/UDP networking — CPU handles sockets, GPU processes the data.
// For outbound connections from Symmetra handlers (calling external APIs, services).

export interface TcpConnectOptions {
  host:       string
  port:       number
  timeoutMs?: number
  keepAlive?: boolean
  tls?:       TlsOptions
}

export interface TlsOptions {
  rejectUnauthorized?: boolean   // default: true — NEVER set false in production
  ca?:    string                 // PEM-encoded CA certificate
  cert?:  string                 // Client certificate (mTLS)
  key?:   string                 // Client private key (mTLS)
  sni?:   string                 // SNI hostname
}

export interface TcpSocket {
  // Write raw bytes (host → remote)
  write(data: string | Uint8Array): Promise<void>

  // Read up to n bytes
  read(n?: number): Promise<Uint8Array>

  // Read until a delimiter (e.g. '\n' for line-based protocols)
  readUntil(delimiter: string | Uint8Array): Promise<Uint8Array>

  // Check if connected
  readonly connected: boolean

  // Raw address info
  readonly localAddr:  string
  readonly remoteAddr: string

  close(): Promise<void>
}

// ── HTTP client (outbound, from GPU handlers) ─────────────────────────────────

export interface HttpRequestOptions {
  method?:    'GET' | 'POST' | 'PUT' | 'DELETE' | 'PATCH' | 'HEAD'
  headers?:   Record<string, string>
  body?:      string | Uint8Array
  timeoutMs?: number
  tls?:       TlsOptions
  followRedirects?: number   // max redirects (default: 5)
}

export interface HttpResponse {
  status:   number
  headers:  Record<string, string>
  body:     Uint8Array

  // Parse body as JSON
  json<T = unknown>(): T

  // Parse body as UTF-8 text
  text(): string
}

export interface HttpClient {
  // One-shot request
  fetch(url: string, opts?: HttpRequestOptions): Promise<HttpResponse>

  // Convenience methods
  get(url: string, opts?: Omit<HttpRequestOptions, 'method'>): Promise<HttpResponse>
  post(url: string, body: unknown, opts?: Omit<HttpRequestOptions, 'method' | 'body'>): Promise<HttpResponse>
  put(url: string, body: unknown, opts?: Omit<HttpRequestOptions, 'method' | 'body'>): Promise<HttpResponse>
  delete(url: string, opts?: Omit<HttpRequestOptions, 'method'>): Promise<HttpResponse>
}

// ── UDP ───────────────────────────────────────────────────────────────────────

export interface UdpSocket {
  send(data: Uint8Array, host: string, port: number): Promise<void>
  recv(timeoutMs?: number): Promise<{ data: Uint8Array; from: string; port: number }>
  bind(port: number, host?: string): Promise<void>
  close(): void
}

// ── WebSocket client ──────────────────────────────────────────────────────────

export interface WebSocket {
  send(data: string | Uint8Array): Promise<void>
  recv(): Promise<string | Uint8Array>
  readonly readyState: 'connecting' | 'open' | 'closing' | 'closed'
  close(code?: number, reason?: string): void
  on(event: 'message', handler: (data: string | Uint8Array) => void): this
  on(event: 'close', handler: (code: number, reason: string) => void): this
  on(event: 'error', handler: (err: Error) => void): this
}

// ── DNS ───────────────────────────────────────────────────────────────────────

export interface DnsApi {
  resolve(hostname: string): Promise<string[]>    // A records
  reverse(ip: string): Promise<string[]>          // PTR records
}

// ── Factory functions ──────────────────────────────────────────────────────────

export declare function connect(opts: TcpConnectOptions): Promise<TcpSocket>
export declare function createUdp(): Promise<UdpSocket>
export declare function websocket(url: string, headers?: Record<string, string>): Promise<WebSocket>
export declare const http: HttpClient
export declare const dns: DnsApi
