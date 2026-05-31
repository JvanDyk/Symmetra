export class SymmifyError extends Error {
  statusCode: number
  code: string
  validation?: unknown[]
  validationContext?: string

  constructor(message: string, statusCode = 500, code = 'FST_ERR_GENERIC') {
    super(message)
    this.name = 'SymmifyError'
    this.statusCode = statusCode
    this.code = code
    Object.setPrototypeOf(this, SymmifyError.prototype)
  }
}

export function createError(
  code: string,
  message: string,
  statusCode = 500,
): SymmifyError {
  return new SymmifyError(message, statusCode, code)
}
