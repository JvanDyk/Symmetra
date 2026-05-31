import type { ContentTypeParserFn, ContentTypeParserOptions } from './types.js'
import { createError } from './error.js'

const DEFAULT_BODY_LIMIT = 1048576 // 1 MiB

interface ResolvedParser {
  bodyLimit: number
  parseAs: 'string' | 'buffer'
  fn: ContentTypeParserFn
}

export async function readBody(
  stream: NodeJS.ReadableStream,
  limit: number,
): Promise<string | Buffer> {
  const chunks: Buffer[] = []
  let total = 0

  return new Promise((resolve, reject) => {
    stream.on('data', (chunk) => {
      const buf = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)
      total += buf.byteLength
      if (total > limit) {
        reject(
          createError(
            'FST_ERR_BODY_TOO_LARGE',
            'Request body is too large',
            413,
          ),
        )
        return
      }
      chunks.push(buf)
    })
    stream.on('end', () => {
      const result = Buffer.concat(chunks)
      resolve(result.toString('utf8'))
    })
    stream.on('error', reject)
  })
}

export class ContentTypeRegistry {
  private parsers: Map<string, ResolvedParser> = new Map()
  private regexParsers: Array<{ pattern: RegExp; parser: ResolvedParser }> = []

  constructor(private parent: ContentTypeRegistry | null = null) {
    if (!parent) {
      this.installDefaults()
    }
  }

  private installDefaults(): void {
    // application/json
    this.parsers.set('application/json', {
      bodyLimit: DEFAULT_BODY_LIMIT,
      parseAs: 'string',
      fn: (req, payload, done) => {
        readBody(payload, DEFAULT_BODY_LIMIT).then(
          (raw) => {
            try {
              done(null, JSON.parse(raw as string))
            } catch (e) {
              done(e as Error)
            }
          },
          done,
        )
      },
    })

    // text/plain
    this.parsers.set('text/plain', {
      bodyLimit: DEFAULT_BODY_LIMIT,
      parseAs: 'string',
      fn: (req, payload, done) => {
        readBody(payload, DEFAULT_BODY_LIMIT).then(
          (raw) => {
            done(null, raw)
          },
          done,
        )
      },
    })
  }

  add(
    contentType: string | string[] | RegExp,
    optsOrFn?: ContentTypeParserOptions | ContentTypeParserFn,
    fn?: ContentTypeParserFn,
  ): void {
    const finalFn = typeof optsOrFn === 'function' ? optsOrFn : fn
    const opts = typeof optsOrFn === 'object' ? optsOrFn : {}

    if (!finalFn) return

    const resolver: ResolvedParser = {
      bodyLimit: opts.bodyLimit ?? DEFAULT_BODY_LIMIT,
      parseAs: opts.parseAs ?? 'buffer',
      fn: finalFn,
    }

    if (typeof contentType === 'string') {
      this.parsers.set(contentType.toLowerCase(), resolver)
    } else if (Array.isArray(contentType)) {
      for (const ct of contentType) {
        this.parsers.set(ct.toLowerCase(), resolver)
      }
    } else if (contentType instanceof RegExp) {
      this.regexParsers.push({ pattern: contentType, parser: resolver })
    }
  }

  find(contentType: string): ResolvedParser | null {
    const ct = contentType.split(';')[0].trim().toLowerCase()

    if (this.parsers.has(ct)) {
      return this.parsers.get(ct)!
    }

    for (const { pattern, parser } of this.regexParsers) {
      if (pattern.test(ct)) {
        return parser
      }
    }

    return this.parent?.find(contentType) ?? null
  }

  has(contentType: string): boolean {
    return this.find(contentType) !== null
  }

  remove(contentType: string | string[]): void {
    const cts = Array.isArray(contentType) ? contentType : [contentType]
    for (const ct of cts) {
      this.parsers.delete(ct.toLowerCase())
    }
  }

  removeAll(): void {
    this.parsers.clear()
    this.regexParsers = []
  }

  createChild(): ContentTypeRegistry {
    return new ContentTypeRegistry(this)
  }
}
