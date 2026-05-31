import { createError } from './error.js'

export class DecoratorRegistry {
  private instanceDecorators: Map<string, unknown> = new Map()
  private requestDecorators: Map<string, unknown> = new Map()
  private replyDecorators: Map<string, unknown> = new Map()

  constructor(private parent: DecoratorRegistry | null = null) {}

  decorate(name: string, value: unknown, target: object): void {
    if (this.hasDecorator(name)) {
      throw createError(
        'FST_ERR_DEC_ALREADY_PRESENT',
        `Decorator '${name}' has already been added`,
        500,
      )
    }
    this.instanceDecorators.set(name, value)
    Object.defineProperty(target, name, {
      value,
      enumerable: true,
      configurable: true,
    })
  }

  decorateRequest(name: string, initialValue: unknown): void {
    if (this.hasRequestDecorator(name)) {
      throw createError(
        'FST_ERR_DEC_ALREADY_PRESENT',
        `Request decorator '${name}' has already been added`,
        500,
      )
    }
    this.requestDecorators.set(name, initialValue)
  }

  decorateReply(name: string, value: unknown): void {
    if (this.hasReplyDecorator(name)) {
      throw createError(
        'FST_ERR_DEC_ALREADY_PRESENT',
        `Reply decorator '${name}' has already been added`,
        500,
      )
    }
    this.replyDecorators.set(name, value)
  }

  hasDecorator(name: string): boolean {
    return (
      this.instanceDecorators.has(name) ||
      (this.parent?.hasDecorator(name) ?? false)
    )
  }

  hasRequestDecorator(name: string): boolean {
    return (
      this.requestDecorators.has(name) ||
      (this.parent?.hasRequestDecorator(name) ?? false)
    )
  }

  hasReplyDecorator(name: string): boolean {
    return (
      this.replyDecorators.has(name) ||
      (this.parent?.hasReplyDecorator(name) ?? false)
    )
  }

  getAllRequestDecorators(): Map<string, unknown> {
    const result = new Map(this.parent?.getAllRequestDecorators() ?? [])
    for (const [k, v] of this.requestDecorators) {
      result.set(k, v)
    }
    return result
  }

  getAllReplyDecorators(): Map<string, unknown> {
    const result = new Map(this.parent?.getAllReplyDecorators() ?? [])
    for (const [k, v] of this.replyDecorators) {
      result.set(k, v)
    }
    return result
  }

  createChild(): DecoratorRegistry {
    return new DecoratorRegistry(this)
  }
}
