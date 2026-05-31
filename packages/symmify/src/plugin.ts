import type { PluginFn, DoneFn } from './types.js'

const PLUGIN_META = Symbol('symmify.plugin-meta')

export interface PluginMeta {
  name: string
  skipOverride: boolean
}

export function fp<TOptions = Record<string, unknown>>(
  fn: PluginFn<TOptions>,
  _opts?: {
    name?: string
    fastify?: string
    decorators?: string[]
  },
): (PluginFn<TOptions> & Record<symbol, PluginMeta>) {
  const pluginFn = fn as any
  pluginFn[PLUGIN_META] = {
    name: _opts?.name ?? fn.name ?? 'anonymous',
    skipOverride: true,
  }
  return pluginFn
}

export function getPluginMeta(fn: PluginFn): PluginMeta | undefined {
  return (fn as any)[PLUGIN_META]
}

export async function promisifyPlugin(
  plugin: PluginFn,
  instance: any,
  opts: Record<string, unknown>,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const result = plugin(instance, opts, (err) => {
      if (err) reject(err)
      else resolve()
    })

    if (result && typeof result.then === 'function') {
      result.then(() => resolve(), reject)
    }
  })
}
