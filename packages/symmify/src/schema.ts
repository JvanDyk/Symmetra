import type {
  ValidatorCompiler,
  SerializerCompiler,
  ValidatorCompilerParams,
  SerializerCompilerParams,
  RouteSchema,
} from './types.js'
import type { ZodTypeAny } from 'zod'

export interface ZodTypeProvider {
  validator: ZodTypeAny
  serializer: ZodTypeAny
}

export interface CompiledSchemas {
  body: ((data: unknown) => { value: unknown } | { error: unknown }) | null
  querystring:
    | ((data: unknown) => { value: unknown } | { error: unknown }) | null
  params: ((data: unknown) => { value: unknown } | { error: unknown }) | null
  headers: ((data: unknown) => { value: unknown } | { error: unknown }) | null
  response: Map<string, (data: unknown) => string> | null
}

export const defaultValidatorCompiler: ValidatorCompiler = (
  params: ValidatorCompilerParams,
) => {
  const schema = params.schema
  return (data: unknown) => {
    const result = (schema as any).safeParse(data)
    if (result.success) {
      return { value: result.data }
    }
    return { error: result.error }
  }
}

export const defaultSerializerCompiler: SerializerCompiler = (
  params: SerializerCompilerParams,
) => {
  const schema = params.schema
  return (data: unknown) => {
    const result = (schema as any).safeParse(data)
    if (result.success) {
      return JSON.stringify(result.data)
    }
    // Fall back to raw serialization if schema fails
    return JSON.stringify(data)
  }
}

export function compileRouteSchemas(
  schema: RouteSchema | undefined,
  method: string,
  url: string,
  validatorCompiler: ValidatorCompiler,
  serializerCompiler: SerializerCompiler,
): CompiledSchemas {
  const compiled: CompiledSchemas = {
    body: null,
    querystring: null,
    params: null,
    headers: null,
    response: null,
  }

  if (!schema) return compiled

  if (schema.body) {
    compiled.body = validatorCompiler({
      schema: schema.body,
      method,
      url,
      httpPart: 'body',
    })
  }

  if (schema.querystring) {
    compiled.querystring = validatorCompiler({
      schema: schema.querystring,
      method,
      url,
      httpPart: 'querystring',
    })
  }

  if (schema.params) {
    compiled.params = validatorCompiler({
      schema: schema.params,
      method,
      url,
      httpPart: 'params',
    })
  }

  if (schema.headers) {
    compiled.headers = validatorCompiler({
      schema: schema.headers,
      method,
      url,
      httpPart: 'headers',
    })
  }

  if (schema.response) {
    compiled.response = new Map()
    for (const [statusCode, responseSchema] of Object.entries(
      schema.response,
    )) {
      const serializer = serializerCompiler({
        schema: responseSchema,
        method,
        url,
        httpStatus: statusCode,
      })
      compiled.response.set(statusCode, serializer)
    }
  }

  return compiled
}
