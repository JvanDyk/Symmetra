import type {
  HookName,
  HookHandler,
  SymmifyRequest,
  SymmifyReply,
  DoneFn,
} from './types.js'

export class HookRegistry {
  private hooks: Map<HookName, HookHandler<any>[]> = new Map()

  constructor(private parent: HookRegistry | null = null) {}

  add<N extends HookName>(name: N, fn: HookHandler<N>): void {
    if (!this.hooks.has(name)) {
      this.hooks.set(name, [])
    }
    const handlers = this.hooks.get(name)!
    handlers.push(fn)
  }

  get<N extends HookName>(name: N): HookHandler<N>[] {
    const parentHooks = this.parent?.get(name) ?? []
    const ownHooks = (this.hooks.get(name) ?? []) as HookHandler<N>[]
    return [...parentHooks, ...ownHooks]
  }

  createChild(): HookRegistry {
    return new HookRegistry(this)
  }
}

function promisifyHook(fn: (done: DoneFn) => void): Promise<void> {
  return new Promise((resolve, reject) => {
    fn((err) => {
      if (err) reject(err)
      else resolve()
    })
  })
}

export async function runRequestHooks(
  hooks: Array<
    (
      request: SymmifyRequest,
      reply: SymmifyReply,
      done: DoneFn,
    ) => void | Promise<void>
  >,
  req: SymmifyRequest,
  reply: SymmifyReply,
): Promise<void> {
  for (const hook of hooks) {
    await promisifyHook((done) => {
      const result = hook(req, reply, done)
      if (result && typeof result.then === 'function') {
        result.then(() => done(), done)
      }
    })
    if (reply.sent) return
  }
}

export async function runPreSerializationHooks(
  hooks: Array<
    (
      request: SymmifyRequest,
      reply: SymmifyReply,
      payload: unknown,
      done: DoneFn,
    ) => void | Promise<void>
  >,
  req: SymmifyRequest,
  reply: SymmifyReply,
  payload: unknown,
): Promise<unknown> {
  for (const hook of hooks) {
    let result = payload
    await promisifyHook((done) => {
      const hookResult = hook(req, reply, payload, done)
      if (hookResult && typeof hookResult.then === 'function') {
        hookResult.then(() => done(), done)
      }
    })
    payload = result
  }
  return payload
}

export async function runOnSendHooks(
  hooks: Array<
    (
      request: SymmifyRequest,
      reply: SymmifyReply,
      serializedPayload: string | Buffer,
      done: DoneFn,
    ) => void | Promise<void>
  >,
  req: SymmifyRequest,
  reply: SymmifyReply,
  payload: string | Buffer,
): Promise<string | Buffer> {
  for (const hook of hooks) {
    let result = payload
    await promisifyHook((done) => {
      const hookResult = hook(req, reply, payload, done)
      if (hookResult && typeof hookResult.then === 'function') {
        hookResult.then(() => done(), done)
      }
    })
    payload = result
  }
  return payload
}

export async function runServerHooks(
  hooks: Array<(done: DoneFn) => void | Promise<void>>,
): Promise<void> {
  for (const hook of hooks) {
    await promisifyHook((done) => {
      const result = hook(done)
      if (result && typeof result.then === 'function') {
        result.then(() => done(), done)
      }
    })
  }
}

export async function runErrorHooks(
  hooks: Array<
    (
      error: Error,
      request: SymmifyRequest,
      reply: SymmifyReply,
      done: DoneFn,
    ) => void | Promise<void>
  >,
  err: Error,
  req: SymmifyRequest,
  reply: SymmifyReply,
): Promise<void> {
  for (const hook of hooks) {
    await promisifyHook((done) => {
      const result = hook(err, req, reply, done)
      if (result && typeof result.then === 'function') {
        result.then(() => done(), done)
      }
    })
    if (reply.sent) return
  }
}

export function runOnRouteHooks(
  hooks: Array<(routeOptions: any) => void>,
  routeOptions: any,
): void {
  for (const hook of hooks) {
    hook(routeOptions)
  }
}
