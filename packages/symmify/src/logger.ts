import type { Logger, LoggerOptions } from './types.js'

function formatLog(msg: string | object, args: unknown[]): string {
  if (typeof msg === 'string') {
    return args.length > 0 ? `${msg} ${JSON.stringify(args)}` : msg
  }
  return JSON.stringify(msg)
}

class ConsoleLogger implements Logger {
  constructor(private bindings: Record<string, unknown> = {}) {}

  private log(level: string, msg: string | object, args: unknown[]): void {
    const formatted = formatLog(msg, args)
    const prefix =
      Object.keys(this.bindings).length > 0
        ? `[${JSON.stringify(this.bindings)}] `
        : ''
    console.log(`${prefix}${level}: ${formatted}`)
  }

  fatal(msg: string | object, ...args: unknown[]): void {
    this.log('FATAL', msg, args)
  }
  error(msg: string | object, ...args: unknown[]): void {
    this.log('ERROR', msg, args)
  }
  warn(msg: string | object, ...args: unknown[]): void {
    this.log('WARN', msg, args)
  }
  info(msg: string | object, ...args: unknown[]): void {
    this.log('INFO', msg, args)
  }
  debug(msg: string | object, ...args: unknown[]): void {
    this.log('DEBUG', msg, args)
  }
  trace(msg: string | object, ...args: unknown[]): void {
    this.log('TRACE', msg, args)
  }

  child(bindings: Record<string, unknown>): Logger {
    return new ConsoleLogger({ ...this.bindings, ...bindings })
  }
}

export const noopLogger: Logger = {
  fatal: () => {},
  error: () => {},
  warn: () => {},
  info: () => {},
  debug: () => {},
  trace: () => {},
  child: () => noopLogger,
}

export function createLogger(opts?: boolean | LoggerOptions): Logger {
  if (opts === false || opts === undefined) {
    return noopLogger
  }
  return new ConsoleLogger()
}
