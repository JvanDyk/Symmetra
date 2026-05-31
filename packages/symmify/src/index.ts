// Factory
export { symmify } from './instance.js'
export { symmify as default } from './instance.js'

// Types
export type {
  SymmifyInstance,
  SymmifyOptions,
  SymmifyRequest,
  SymmifyReply,
  RouteOptions,
  RouteSchema,
  RouteHandler,
  HookName,
  HookHandler,
  PluginFn,
  ValidatorCompiler,
  SerializerCompiler,
  ValidatorCompilerParams,
  SerializerCompilerParams,
  ListenOptions,
  Logger,
  LoggerOptions,
  ContentTypeParserOptions,
  ContentTypeParserFn,
  ZodTypeProvider,
  DoneFn,
} from './types.js'

// Error utilities
export { SymmifyError, createError } from './error.js'

// Plugin utility
export { fp } from './plugin.js'

// Zod integration
export { defaultValidatorCompiler, defaultSerializerCompiler } from './schema.js'
