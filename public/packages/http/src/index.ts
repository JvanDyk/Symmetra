// @symmetra/http
// GPU-native HTTP server for Symmetra.
// Replaces Express, Fastify, Hono — nothing runs on CPU except TCP accept/recv/send.
//
// All types in this file are compile-time only. The Symmetra engine resolves imports
// to GPU kernel calls. There is no JavaScript runtime executing this code.

export type { SymmCtx, GpuApi, KvApi, RuntimeOptions, GpuBuffer } from '@symmetra/core'

import type { SymmCtx, RuntimeOptions } from '@symmetra/core'

// ── Handler ────────────────────────────────────────────────────────────────────

export type Handler = (ctx: SymmCtx) => void | Promise<void>
export type Middleware = (ctx: SymmCtx, next: () => Promise<void>) => Promise<void>

// ── Router ─────────────────────────────────────────────────────────────────────

// Routes register GPU handler functions. Each route compiles to a device function.
export class Router {
  get(path: string, handler: Handler): this    { return this }
  post(path: string, handler: Handler): this   { return this }
  put(path: string, handler: Handler): this    { return this }
  delete(path: string, handler: Handler): this { return this }
  patch(path: string, handler: Handler): this  { return this }

  route(prefix: string): RouterGroup { return new RouterGroup(prefix) }
  use(middleware: Middleware): this   { return this }
}

export class RouterGroup {
  constructor(private prefix: string) {}
  get(path: string, handler: Handler): this    { return this }
  post(path: string, handler: Handler): this   { return this }
  put(path: string, handler: Handler): this    { return this }
  delete(path: string, handler: Handler): this { return this }
  patch(path: string, handler: Handler): this  { return this }
}

// ── serve() ────────────────────────────────────────────────────────────────────

export interface ServeOptions extends RuntimeOptions {
  port?:     number   // TCP port. Default: 8080
  hostname?: string   // Bind address. Default: '0.0.0.0'
  streams?:  number   // CUDA stream pool size. Default: 32
  batch?:    number   // Requests per GPU dispatch. Default: 64
}

// serve() is the entry point. The Symmetra engine intercepts this call during
// compilation and uses it to configure the GPU server. This function never
// executes in JavaScript.
export function serve(app: Router, opts?: ServeOptions): void {
  throw new Error(
    'serve() must be called from a Symmetra script, not from Node.js.\n' +
    'Run: sym ./your-server.ts'
  )
}

export declare const gpu: import('@symmetra/core').GpuApi
