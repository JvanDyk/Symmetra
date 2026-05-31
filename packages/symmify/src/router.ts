import type { RouteOptions } from './types.js'

export interface CompiledRoute {
  handler: any
  schema?: any
  config?: unknown
  constraints?: Record<string, unknown>
  attachValidation?: boolean
  url: string
  hooks?: any
  compiledSchemas?: any
}

export interface RouteMatch {
  route: CompiledRoute
  params: Record<string, string>
}

interface RadixNode {
  prefix: string
  staticChildren: Map<string, RadixNode>
  paramChild: RadixNode | null
  wildcardChild: RadixNode | null
  handlers: Map<string, CompiledRoute>
  paramName: string | null
  isWildcard: boolean
}

export class RadixRouter {
  private root: RadixNode

  constructor(
    private opts: {
      caseSensitive?: boolean
      ignoreTrailingSlash?: boolean
    } = {},
  ) {
    this.root = {
      prefix: '',
      staticChildren: new Map(),
      paramChild: null,
      wildcardChild: null,
      handlers: new Map(),
      paramName: null,
      isWildcard: false,
    }
  }

  private normalizePath(path: string): string {
    let result = path
    if (this.opts.ignoreTrailingSlash && result !== '/' && result.endsWith('/')) {
      result = result.slice(0, -1)
    }
    if (!this.opts.caseSensitive) {
      result = result.toLowerCase()
    }
    return result
  }

  insert(method: string, path: string, route: CompiledRoute): void {
    path = this.normalizePath(path)
    method = method.toUpperCase()

    let node = this.root
    let remaining = path

    while (remaining.length > 0) {
      // Find common prefix length
      const commonLen = this.commonPrefixLength(node.prefix, remaining)

      // If we need to split the node
      if (commonLen < node.prefix.length) {
        const splitNode: RadixNode = {
          prefix: node.prefix.slice(commonLen),
          staticChildren: node.staticChildren,
          paramChild: node.paramChild,
          wildcardChild: node.wildcardChild,
          handlers: node.handlers,
          paramName: node.paramName,
          isWildcard: node.isWildcard,
        }

        node.prefix = node.prefix.slice(0, commonLen)
        node.staticChildren = new Map()
        node.paramChild = null
        node.wildcardChild = null
        node.handlers = new Map()
        node.paramName = null
        node.isWildcard = false

        if (splitNode.prefix.length > 0) {
          const firstChar = splitNode.prefix[0]
          node.staticChildren.set(firstChar, splitNode)
        }
      }

      remaining = remaining.slice(commonLen)

      if (remaining.length === 0) {
        node.handlers.set(method, route)
        if (method === 'GET') {
          // Auto-register HEAD
          if (!node.handlers.has('HEAD')) {
            node.handlers.set('HEAD', route)
          }
        }
        return
      }

      const firstChar = remaining[0]

      if (firstChar === ':') {
        // Parameter segment
        if (!node.paramChild) {
          const paramEndIdx = remaining.indexOf('/')
          const paramName =
            paramEndIdx === -1
              ? remaining.slice(1)
              : remaining.slice(1, paramEndIdx)
          node.paramChild = {
            prefix: paramEndIdx === -1 ? '' : remaining.slice(0, paramEndIdx),
            staticChildren: new Map(),
            paramChild: null,
            wildcardChild: null,
            handlers: new Map(),
            paramName,
            isWildcard: false,
          }
        }
        node = node.paramChild
        const nextSlash = remaining.indexOf('/', 1)
        remaining =
          nextSlash === -1 ? '' : remaining.slice(nextSlash)
      } else if (firstChar === '*') {
        // Wildcard
        if (!node.wildcardChild) {
          node.wildcardChild = {
            prefix: '',
            staticChildren: new Map(),
            paramChild: null,
            wildcardChild: null,
            handlers: new Map(),
            paramName: null,
            isWildcard: true,
          }
        }
        node = node.wildcardChild
        remaining = ''
      } else {
        // Static child
        if (!node.staticChildren.has(firstChar)) {
          node.staticChildren.set(firstChar, {
            prefix: remaining,
            staticChildren: new Map(),
            paramChild: null,
            wildcardChild: null,
            handlers: new Map(),
            paramName: null,
            isWildcard: false,
          })
          return
        }
        node = node.staticChildren.get(firstChar)!
      }
    }

    node.handlers.set(method, route)
  }

  find(method: string, url: string): RouteMatch | null {
    url = url.split('?')[0] // Strip query string
    url = this.normalizePath(url)
    method = method.toUpperCase()

    const params: Record<string, string> = {}
    const node = this.matchNode(this.root, url, params)

    if (!node) return null

    // Try exact method match
    let handler = node.handlers.get(method)

    // Try ALL fallback
    if (!handler && method !== 'ALL') {
      handler = node.handlers.get('ALL')
    }

    // Special case: HEAD falls back to GET
    if (!handler && method === 'HEAD') {
      handler = node.handlers.get('GET')
    }

    if (!handler) return null

    return { route: handler, params }
  }

  private matchNode(
    node: RadixNode,
    path: string,
    params: Record<string, string>,
  ): RadixNode | null {
    if (!path.startsWith(node.prefix)) return null

    const remaining = path.slice(node.prefix.length)

    if (remaining.length === 0) {
      return node.handlers.size > 0 ? node : null
    }

    const ch = remaining[0]

    // Try static child first (faster)
    if (node.staticChildren.has(ch)) {
      const result = this.matchNode(
        node.staticChildren.get(ch)!,
        remaining,
        params,
      )
      if (result) return result
    }

    // Try parametric
    if (node.paramChild) {
      const segEnd = remaining.indexOf('/')
      const paramValue =
        segEnd === -1 ? remaining : remaining.slice(0, segEnd)
      const newPath = segEnd === -1 ? '' : remaining.slice(segEnd)

      if (node.paramChild.paramName) {
        params[node.paramChild.paramName] = paramValue
      }

      const result = this.matchNode(
        node.paramChild,
        newPath,
        params,
      )
      if (result) return result

      // Backtrack if no match
      delete params[node.paramChild.paramName!]
    }

    // Try wildcard
    if (node.wildcardChild) {
      params['*'] = remaining
      return node.wildcardChild.handlers.size > 0 ? node.wildcardChild : null
    }

    return null
  }

  private commonPrefixLength(a: string, b: string): number {
    let i = 0
    while (i < a.length && i < b.length && a[i] === b[i]) {
      i++
    }
    return i
  }

  hasRoute(method: string, path: string): boolean {
    return this.find(method, path) !== null
  }
}
