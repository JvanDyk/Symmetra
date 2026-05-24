// @symmetra/net — GPU-native networking type definitions

// ── HTTP Client ───────────────────────────────────────────────────────────────

export interface HttpRequestOptions {
  headers?: Record<string, string>;
  timeout?: number;
  followRedirects?: boolean;
  maxRedirects?: number;
  body?: string | Uint8Array | Record<string, unknown>;
}

export interface HttpResponse<T = string> {
  status: number;
  ok: boolean;
  headers: Record<string, string>;
  body: T;
  text(): string;
  json<J = unknown>(): J;
  bytes(): Uint8Array;
}

export interface HttpClient {
  get<T = string>(url: string, options?: HttpRequestOptions): Promise<HttpResponse<T>>;
  post<T = string>(url: string, body?: unknown, options?: HttpRequestOptions): Promise<HttpResponse<T>>;
  put<T = string>(url: string, body?: unknown, options?: HttpRequestOptions): Promise<HttpResponse<T>>;
  patch<T = string>(url: string, body?: unknown, options?: HttpRequestOptions): Promise<HttpResponse<T>>;
  delete<T = string>(url: string, options?: HttpRequestOptions): Promise<HttpResponse<T>>;
  fetch<T = string>(url: string, options?: RequestInit & HttpRequestOptions): Promise<HttpResponse<T>>;
}

export declare const http: HttpClient;

// ── TCP Socket ────────────────────────────────────────────────────────────────

export interface TcpSocketOptions {
  host: string;
  port: number;
  tls?: boolean;
  timeoutMs?: number;
  keepAlive?: boolean;
}

export interface TcpSocket {
  write(data: string | Uint8Array): Promise<void>;
  read(length?: number): Promise<Uint8Array>;
  readLine(): Promise<string>;
  readAll(): Promise<Uint8Array>;
  close(): Promise<void>;
  readonly connected: boolean;
  readonly remoteAddress: string;
  readonly remotePort: number;
}

export declare function connect(options: TcpSocketOptions): Promise<TcpSocket>;

// ── UDP Socket ────────────────────────────────────────────────────────────────

export interface UdpSocketOptions {
  port?: number;
  address?: string;
  broadcast?: boolean;
  multicastGroup?: string;
}

export interface UdpDatagram {
  data: Uint8Array;
  remoteAddress: string;
  remotePort: number;
}

export interface UdpSocket {
  send(data: string | Uint8Array, host: string, port: number): Promise<void>;
  receive(): Promise<UdpDatagram>;
  close(): void;
}

export declare function createUdp(options?: UdpSocketOptions): Promise<UdpSocket>;

// ── WebSocket ─────────────────────────────────────────────────────────────────

export interface WebSocketOptions {
  headers?: Record<string, string>;
  protocols?: string[];
  timeoutMs?: number;
}

export interface WebSocket {
  send(data: string | Uint8Array): Promise<void>;
  receive(): Promise<{ type: 'text' | 'binary' | 'close' | 'ping'; data: string | Uint8Array }>;
  close(code?: number, reason?: string): Promise<void>;
  readonly readyState: 'connecting' | 'open' | 'closing' | 'closed';
}

export declare function websocket(url: string, options?: WebSocketOptions): Promise<WebSocket>;

// ── DNS ───────────────────────────────────────────────────────────────────────

export interface DnsRecord {
  type: 'A' | 'AAAA' | 'CNAME' | 'MX' | 'TXT' | 'NS';
  value: string;
  ttl: number;
}

export interface DnsApi {
  resolve(hostname: string, type?: 'A' | 'AAAA'): Promise<string[]>;
  lookup(hostname: string): Promise<string>;
  reverse(ip: string): Promise<string[]>;
  records(hostname: string, type: DnsRecord['type']): Promise<DnsRecord[]>;
}

export declare const dns: DnsApi;
