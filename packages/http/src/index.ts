// @symmetra/http
// GPU-native HTTP server for Symmetra.
// Replaces Express, Fastify, Hono — nothing runs on CPU except TCP accept/recv/send.
//
// All types in this file are compile-time only. The Symmetra engine (symmetra binary)
// resolves imports to CUDA kernel calls via the codegen pipeline.
// There is no JavaScript runtime executing this code.

export type { SymmCtx, GpuApi, KvApi, RuntimeOptions, GpuBuffer, EngineVariant } from '@symmetra/core'

// ── Handler type ──────────────────────────────────────────────────────────────

import type { SymmCtx } from '@symmetra/core'

export type Handler = (ctx: SymmCtx) => void | Promise<void>

// ── Middleware ────────────────────────────────────────────────────────────────

export type Middleware = (ctx: SymmCtx, next: () => Promise<void>) => Promise<void>

// ── Router ────────────────────────────────────────────────────────────────────

// The Router registers routes. Each route compiles to a named __device__ function.
// The compiled dispatch table runs entirely on GPU.
export class Router {
  // These are type-level declarations; the engine reads them from the AST.
  get(path: string, handler: Handler): this { return this }
  post(path: string, handler: Handler): this { return this }
  put(path: string, handler: Handler): this { return this }
  delete(path: string, handler: Handler): this { return this }
  patch(path: string, handler: Handler): this { return this }

  // Group routes under a common prefix
  route(prefix: string): RouterGroup { return new RouterGroup(prefix) }

  // Apply middleware to all subsequent routes
  use(middleware: Middleware): this { return this }
}

export class RouterGroup {
  constructor(private prefix: string) {}

  get(path: string, handler: Handler): this { return this }
  post(path: string, handler: Handler): this { return this }
  put(path: string, handler: Handler): this { return this }
  delete(path: string, handler: Handler): this { return this }
  patch(path: string, handler: Handler): this { return this }
}

// ── serve() ───────────────────────────────────────────────────────────────────

import type { RuntimeOptions } from '@symmetra/core'

export interface ServeOptions extends RuntimeOptions {
  port?:     number          // TCP port. Default: 8080
  hostname?: string          // Bind address. Default: '0.0.0.0'
  streams?:  number          // CUDA stream pool. Default: 32
  batch?:    number          // Requests per GPU dispatch. Default: 64
}

// serve() is the entry point. The engine looks for this call in the AST
// and uses it to configure the GpuServer. This function never actually runs
// in JavaScript — the engine intercepts it during compilation.
export function serve(app: Router, opts?: ServeOptions): void {
  throw new Error(
    'serve() must be called from a Symmetra script, not from Node.js.\n' +
    'Run: symmetra ./your-server.ts'
  )
}

// ── Convenience re-export ─────────────────────────────────────────────────────

// gpu is injected by the runtime into every handler's closure.
// Import it for explicit GPU operations outside of ctx.gpu.
export declare const gpu: import('@symmetra/core').GpuApi

// ── Response helpers (used inside handlers) ───────────────────────────────────

// These compile to GPU device function calls via the codegen.
// They are type aliases only — no runtime JavaScript.

export type JsonResponse   = { json(data: unknown): void }
export type TextResponse   = { text(data: string): void }
export type StatusResponse = { status(code: number): JsonResponse & TextResponse }
