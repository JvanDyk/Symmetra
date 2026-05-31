// Node.js url module compatibility

export class URL {
  href: string;
  origin: string;
  protocol: string;
  username: string;
  password: string;
  host: string;
  hostname: string;
  port: string;
  pathname: string;
  search: string;
  searchParams: URLSearchParams;
  hash: string;

  constructor(input: string, base?: string | URL) {
    const url = new URL_Impl(input, base?.toString());
    this.href = url.href;
    this.origin = url.origin;
    this.protocol = url.protocol;
    this.username = url.username;
    this.password = url.password;
    this.host = url.host;
    this.hostname = url.hostname;
    this.port = url.port;
    this.pathname = url.pathname;
    this.search = url.search;
    this.hash = url.hash;
    this.searchParams = new URLSearchParams(url.search);
  }

  toString(): string {
    return this.href;
  }

  toJSON(): string {
    return this.href;
  }

  static parse(input: string, base?: string): URL | null {
    try {
      return new URL(input, base);
    } catch {
      return null;
    }
  }

  static canParse(input: string, base?: string): boolean {
    try {
      new URL(input, base);
      return true;
    } catch {
      return false;
    }
  }
}

class URL_Impl {
  href: string;
  protocol: string;
  username: string;
  password: string;
  hostname: string;
  port: string;
  pathname: string;
  search: string;
  hash: string;

  constructor(input: string, base?: string) {
    let url = input;

    // Handle relative URLs with base
    if (base && !input.match(/^[a-zA-Z][a-zA-Z0-9+.-]*:/)) {
      const baseUrl = new URL_Impl(base);
      url = baseUrl.protocol + '//' + baseUrl.host + input;
    }

    // Parse URL
    const match = url.match(/^([a-z][a-z0-9+.-]*):(?:\/\/(?:([^:@/]*)(?::([^@/]*))?@)?([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/i);

    if (!match) {
      throw new TypeError('Invalid URL');
    }

    this.protocol = (match[1] || 'http').toLowerCase() + ':';
    this.username = decodeURIComponent(match[2] || '');
    this.password = decodeURIComponent(match[3] || '');

    const host = match[4] || '';
    const hostMatch = host.match(/^([^:]+)(?::(\d+))?$/);
    this.hostname = hostMatch ? hostMatch[1].toLowerCase() : '';
    this.port = hostMatch?.[2] || '';

    this.pathname = match[5] || '/';
    this.search = match[6] || '';
    this.hash = match[7] || '';

    this.href = this.toString();
  }

  get host(): string {
    return this.port ? `${this.hostname}:${this.port}` : this.hostname;
  }

  get origin(): string {
    return `${this.protocol}//${this.host}`;
  }

  toString(): string {
    let url = this.protocol + '//';
    if (this.username) {
      url += this.username;
      if (this.password) url += ':' + this.password;
      url += '@';
    }
    url += this.host + this.pathname + this.search + this.hash;
    return url;
  }
}

export class URLSearchParams {
  private params: Map<string, string[]> = new Map();

  constructor(init?: string | Record<string, string> | Iterable<[string, string]>) {
    if (typeof init === 'string') {
      const str = init.startsWith('?') ? init.slice(1) : init;
      if (str) {
        for (const pair of str.split('&')) {
          const [key, value] = pair.split('=');
          this.append(decodeURIComponent(key), decodeURIComponent(value || ''));
        }
      }
    } else if (init && typeof init === 'object' && !Array.isArray(init)) {
      for (const [key, value] of Object.entries(init)) {
        this.append(key, value);
      }
    } else if (init && Symbol.iterator in init) {
      for (const [key, value] of init as Iterable<[string, string]>) {
        this.append(key, value);
      }
    }
  }

  append(name: string, value: string): void {
    const values = this.params.get(name) || [];
    values.push(value);
    this.params.set(name, values);
  }

  delete(name: string): void {
    this.params.delete(name);
  }

  get(name: string): string | null {
    const values = this.params.get(name);
    return values?.[0] || null;
  }

  getAll(name: string): string[] {
    return this.params.get(name) || [];
  }

  has(name: string): boolean {
    return this.params.has(name);
  }

  set(name: string, value: string): void {
    this.params.set(name, [value]);
  }

  sort(): void {
    const entries = Array.from(this.params.entries());
    entries.sort(([a], [b]) => a.localeCompare(b));
    this.params = new Map(entries);
  }

  keys(): IterableIterator<string> {
    return this.params.keys();
  }

  values(): IterableIterator<string> {
    return (function* (self: URLSearchParams) {
      for (const [, values] of self.params) {
        for (const value of values) {
          yield value;
        }
      }
    })(this);
  }

  entries(): IterableIterator<[string, string]> {
    return (function* (self: URLSearchParams) {
      for (const [key, values] of self.params) {
        for (const value of values) {
          yield [key, value];
        }
      }
    })(this);
  }

  [Symbol.iterator](): IterableIterator<[string, string]> {
    return this.entries();
  }

  toString(): string {
    const parts = [];
    for (const [key, values] of this.params) {
      for (const value of values) {
        parts.push(`${encodeURIComponent(key)}=${encodeURIComponent(value)}`);
      }
    }
    return parts.join('&');
  }
}

export function parse(urlString: string, parseQueryString?: boolean, slashesDenoteHost?: boolean): Record<string, any> {
  const url = new URL_Impl(urlString);
  return {
    protocol: url.protocol,
    username: url.username,
    password: url.password,
    host: url.host,
    hostname: url.hostname,
    port: url.port || undefined,
    pathname: url.pathname,
    search: url.search,
    hash: url.hash,
    query: parseQueryString ? new URLSearchParams(url.search) : url.search
  };
}

export function format(urlObject: Record<string, any>): string {
  let url = urlObject.protocol || 'http:';
  if (urlObject.host) {
    url += '//' + urlObject.host;
  } else if (urlObject.hostname) {
    url += '//' + urlObject.hostname;
    if (urlObject.port) url += ':' + urlObject.port;
  }
  url += urlObject.pathname || '';
  url += urlObject.search || '';
  url += urlObject.hash || '';
  return url;
}

export function resolve(from: string, to: string): string {
  const baseUrl = new URL_Impl(from);
  const resolved = new URL_Impl(to, from);
  return resolved.href;
}
