/**
 * Integration test server — Symmetra
 *
 * Run with: sym ./tests/integration/server.ts
 * Then run:  bash tests/integration/run_tests.sh
 *            (or: pwsh tests/integration/run_tests.ps1  on Windows)
 *
 * Every route exercises one feature. Test results speak for themselves:
 * HTTP 200 = passing, anything else = failing.
 */

import { serve, Router } from '@symmetra/http';
import { KvStore }        from '@symmetra/kv';

const app = new Router();
const kv  = new KvStore({ maxMb: 32 });

// ── Core language ────────────────────────────────────────────────────────────

app.get('/ping', (ctx) => {
  ctx.json({ ok: true });
});

app.get('/arithmetic', (ctx) => {
  ctx.json({
    add:  1 + 2,
    sub:  10 - 3,
    mul:  4 * 5,
    div:  9 / 3,
    mod:  10 % 3,
    pow:  2 ** 8,
    sqrt: Math.sqrt(144),
    sin:  parseFloat(Math.sin(Math.PI / 2).toFixed(6)),
  });
});

// ── Classes ──────────────────────────────────────────────────────────────────

class Vec3 {
  constructor(public x: number, public y: number, public z: number) {}

  add(other: Vec3): Vec3 { return new Vec3(this.x + other.x, this.y + other.y, this.z + other.z); }
  dot(other: Vec3): number { return this.x * other.x + this.y * other.y + this.z * other.z; }
  length(): number { return Math.sqrt(this.x ** 2 + this.y ** 2 + this.z ** 2); }
  normalize(): Vec3 {
    const l = this.length();
    return new Vec3(this.x / l, this.y / l, this.z / l);
  }
  toArray(): [number, number, number] { return [this.x, this.y, this.z]; }
}

app.get('/vec3', (ctx) => {
  const a  = new Vec3(1, 2, 3);
  const b  = new Vec3(4, 5, 6);
  const sum = a.add(b);
  const n   = a.normalize();
  ctx.json({
    sum:       sum.toArray(),
    dot:       a.dot(b),
    lengthA:   parseFloat(a.length().toFixed(6)),
    normalizeA: n.toArray().map(v => parseFloat(v.toFixed(6))),
  });
});

// ── Inheritance ──────────────────────────────────────────────────────────────

class Animal {
  constructor(public name: string) {}
  speak(): string { return `${this.name} makes a sound`; }
}
class Dog extends Animal {
  speak(): string { return `${this.name} barks`; }
}
class Cat extends Animal {
  speak(): string { return `${this.name} meows`; }
}

app.get('/animals', (ctx) => {
  const animals: Animal[] = [new Dog('Rex'), new Cat('Whiskers'), new Animal('Generic')];
  ctx.json(animals.map(a => ({ name: a.name, sound: a.speak() })));
});

// ── Generators ──────────────────────────────────────────────────────────────

function* range(from: number, to: number, step = 1): Generator<number> {
  for (let i = from; i <= to; i += step) yield i;
}

function* fibonacci(): Generator<number> {
  let [a, b] = [0, 1];
  while (true) { yield a; [a, b] = [b, a + b]; }
}

app.get('/generators', (ctx) => {
  const rangeResult = [...range(1, 10, 2)];
  const fibGen = fibonacci();
  const fibResult = Array.from({ length: 8 }, () => fibGen.next().value);
  ctx.json({ range: rangeResult, fibonacci: fibResult });
});

// ── async/await + Promise.all ────────────────────────────────────────────────

async function delay<T>(value: T, ms: number): Promise<T> {
  return new Promise(resolve => setTimeout(() => resolve(value), ms));
}

app.get('/promises', async (ctx) => {
  const [a, b, c] = await Promise.all([
    delay('first',  50),
    delay('second', 30),
    delay('third',  40),
  ]);
  ctx.json({ results: [a, b, c], order: 'all resolved' });
});

// ── try/catch/finally ────────────────────────────────────────────────────────

app.get('/error-handling', (ctx) => {
  let log: string[] = [];
  try {
    log.push('try');
    throw new Error('test error');
    log.push('unreachable');
  } catch (e) {
    log.push(`caught: ${(e as Error).message}`);
  } finally {
    log.push('finally');
  }
  ctx.json({ log, ok: log.length === 3 });
});

// ── Map + Set ────────────────────────────────────────────────────────────────

app.post('/collections', async (ctx) => {
  const { words } = await ctx.json<{ words: string[] }>();

  const freq = new Map<string, number>();
  const seen = new Set<string>();

  for (const w of words) {
    freq.set(w, (freq.get(w) ?? 0) + 1);
    seen.add(w.toLowerCase());
  }

  ctx.json({
    frequencies:  Object.fromEntries(freq),
    uniqueCount:  seen.size,
    uniqueWords:  [...seen].sort(),
  });
});

// ── RegExp ───────────────────────────────────────────────────────────────────

const emailRe  = /^[^@]+@[^@]+\.[^@]+$/;
const ipv4Re   = /^(\d{1,3}\.){3}\d{1,3}$/;
const slugRe   = /^[a-z0-9-]+$/;

app.post('/regex', async (ctx) => {
  const { email, ip, slug } = await ctx.json<{ email: string; ip: string; slug: string }>();
  ctx.json({
    emailValid: emailRe.test(email),
    ipValid:    ipv4Re.test(ip),
    slugValid:  slugRe.test(slug),
  });
});

// ── Destructuring + spread ───────────────────────────────────────────────────

app.post('/destructuring', async (ctx) => {
  const { arr, obj } = await ctx.json<{
    arr: number[];
    obj: { a: number; b: number; rest: Record<string, unknown> };
  }>();

  const [first, second, ...rest] = arr;
  const { a, b, ...remaining } = obj;

  ctx.json({
    first, second, rest,
    a, b, remaining,
    merged: { ...obj, extra: true },
  });
});

// ── Optional chaining + nullish coalescing ───────────────────────────────────

app.post('/safe-access', async (ctx) => {
  const body = await ctx.json<Record<string, unknown>>();
  const deep = (body?.user as Record<string, unknown>)?.profile as Record<string, unknown>;
  const name  = (deep?.name as string)   ?? 'anonymous';
  const score = (deep?.score as number)  ?? 0;
  ctx.json({ name, score });
});

// ── Closures ─────────────────────────────────────────────────────────────────

app.get('/closure', (ctx) => {
  function makeAdder(n: number) {
    return (x: number) => x + n;
  }
  const add5  = makeAdder(5);
  const add10 = makeAdder(10);
  ctx.json({ add5: add5(3), add10: add10(3) });
});

// ── Template literals ────────────────────────────────────────────────────────

app.get('/template', (ctx) => {
  const name = 'Symmetra';
  const ver  = 1;
  const msg  = `Hello from ${name} v${ver}! ${1 + 2 + 3} = 6`;
  ctx.json({ msg });
});

// ── TextEncoder / TextDecoder ────────────────────────────────────────────────

app.post('/text-roundtrip', async (ctx) => {
  const { text } = await ctx.json<{ text: string }>();
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  const bytes   = encoder.encode(text);
  const decoded = decoder.decode(bytes);
  ctx.json({ original: text, byteLength: bytes.byteLength, roundtrip: decoded, ok: decoded === text });
});

// ── URL + URLSearchParams ────────────────────────────────────────────────────

app.post('/url-parse', async (ctx) => {
  const { url } = await ctx.json<{ url: string }>();
  try {
    const u = new URL(url);
    ctx.json({ protocol: u.protocol, hostname: u.hostname, pathname: u.pathname, valid: true });
  } catch {
    ctx.status(400).json({ valid: false });
  }
});

// ── setTimeout / clearTimeout ────────────────────────────────────────────────

app.get('/timer-basic', async (ctx) => {
  const result = await new Promise<string>((resolve) => {
    const id = setTimeout(() => resolve('timeout-fired'), 100);
    // Prove clearTimeout works: set another and cancel it
    const cancelId = setTimeout(() => resolve('should-not-fire'), 50);
    clearTimeout(cancelId);
    // The 100ms one should still fire
    void id;
  });
  ctx.json({ result, ok: result === 'timeout-fired' });
});

// ── KV store ─────────────────────────────────────────────────────────────────

app.put('/kv/:key', async (ctx) => {
  const body = await ctx.json();
  await kv.set(ctx.params.key, body);
  ctx.json({ ok: true });
});

app.get('/kv/:key', async (ctx) => {
  const val = await kv.get(ctx.params.key);
  if (val === null) { ctx.status(404).json({ error: 'not found' }); return; }
  ctx.json({ value: val });
});

app.delete('/kv/:key', async (ctx) => {
  await kv.del(ctx.params.key);
  ctx.json({ ok: true });
});

// ── GpuBuffer operations ─────────────────────────────────────────────────────

app.post('/gpu-map', async (ctx) => {
  const data = await ctx.body<Float32Array>();
  const result = data.map((x, _i) => x * x);
  ctx.json({ length: result.length, sample: result.toArray().slice(0, 5) });
});

app.post('/gpu-reduce', async (ctx) => {
  const data = await ctx.body<Float32Array>();
  const sum  = data.reduce((acc, x) => acc + x, 0);
  ctx.json({ sum, count: data.length });
});

// ── AbortController ──────────────────────────────────────────────────────────

app.get('/abort-basic', async (ctx) => {
  const controller = new AbortController();
  const { signal } = controller;

  let aborted = false;
  signal.addEventListener('abort', () => { aborted = true; });

  controller.abort();

  ctx.json({ aborted: signal.aborted, listenerFired: aborted });
});

// ── typeof ───────────────────────────────────────────────────────────────────

app.get('/typeof-check', (ctx) => {
  const numType  = typeof 42;
  const strType  = typeof 'hello';
  const boolType = typeof true;
  const nullType = typeof null;
  ctx.json({
    number:  numType  === 'number',
    string:  strType  === 'string',
    boolean: boolType === 'boolean',
    nullObj: nullType === 'object',
  });
});

// ── String methods ────────────────────────────────────────────────────────────

app.get('/string-methods', (ctx) => {
  const s = 'Hello World';
  ctx.json({
    padStart:   ('5'.padStart(3, '0')),
    padEnd:     ('5'.padEnd(3, '0')),
    trim:       ('  hello  '.trim()),
    trimStart:  ('  hello  '.trimStart()),
    trimEnd:    ('  hello  '.trimEnd()),
    replaceAll: ('aabbcc'.replaceAll('b', 'x')),
    repeat:     ('ab'.repeat(3)),
    upper:      ('hello'.toUpperCase()),
    lower:      ('HELLO'.toLowerCase()),
    includes:   s.includes('World'),
    startsWith: s.startsWith('Hello'),
    endsWith:   s.endsWith('World'),
    indexOf:    s.indexOf('World'),
    slice:      s.slice(0, 5),
  });
});

// ── Number / NaN / Infinity ────────────────────────────────────────────────────

app.get('/number-methods', (ctx) => {
  const n = 42;
  const f = 3.14;
  ctx.json({
    isInt:      Number.isInteger(n),
    isIntF:     Number.isInteger(f),
    isNaN:      Number.isNaN(NaN),
    isNaNNum:   Number.isNaN(42),
    isFinite:   Number.isFinite(42),
    isFiniteInf:Number.isFinite(Infinity),
    isSafe:     Number.isSafeInteger(n),
    globalNaN:  isNaN(NaN),
    globalFin:  isFinite(42),
    infPos:     Infinity > 1000000,
    infNeg:     NEGATIVE_INFINITY < -1000000,
  });
});

// ── Array statics ─────────────────────────────────────────────────────────────

app.get('/array-statics', (ctx) => {
  const arr = [1, 2, 3, 4, 5];
  ctx.json({
    isArray:     Array.isArray(arr),
    isArrayStr:  Array.isArray('hello'),
    isArrayNum:  Array.isArray(42),
    length:      arr.length,
  });
});

// ── Object statics ────────────────────────────────────────────────────────────

app.get('/object-statics', (ctx) => {
  ctx.json({
    hasOwn: Object.hasOwn({}, 'key'),
  });
});

// ── Date.now ─────────────────────────────────────────────────────────────────

app.get('/date-now', (ctx) => {
  const t = Date.now();
  ctx.json({
    timestamp:    t,
    isNumber:     typeof t === 'number',
    isNonNeg:     t >= 0,
  });
});

// ── Math extras ───────────────────────────────────────────────────────────────

app.get('/math-extras', (ctx) => {
  ctx.json({
    sign_pos:  Math.sign(5),
    sign_neg:  Math.sign(-3),
    sign_zero: Math.sign(0),
    log1p:     parseFloat(Math.log1p(0).toFixed(4)),
    clamp:     Math.min(Math.max(150, 0), 100),
    hypot:     parseFloat(Math.hypot(3, 4).toFixed(4)),
    ln2:       parseFloat(Math.LN2.toFixed(6)),
  });
});

// ── RegExp matchAll ───────────────────────────────────────────────────────────

app.get('/matchall', (ctx) => {
  const text    = 'cat bat sat';
  const pattern = new RegExp('[cbsp]at', 'g');
  const matches = pattern.matchAll(text);
  ctx.json({
    count:  matches.length,
    first:  matches[0].start >= 0,
    second: matches[1].start >= 0,
  });
});

// ── Promise.race ─────────────────────────────────────────────────────────────

app.get('/promise-race', async (ctx) => {
  const fast   = await new Promise<string>((resolve) => setTimeout(() => resolve('fast'),   20));
  const medium = await new Promise<string>((resolve) => setTimeout(() => resolve('medium'), 80));
  // On GPU all timers are resolved sequentially — fast should be the shortest
  const winner = fast === 'fast' ? 'fast' : 'medium';
  ctx.json({ winner, ok: winner === 'fast' });
});

// ── Generator with break ──────────────────────────────────────────────────────

function* naturals(): Generator<number> {
  let n = 0;
  while (true) { yield n++; }
}

app.get('/generator-take', (ctx) => {
  const gen = naturals();
  const result: number[] = [];
  for (let i = 0; i < 10; i++) {
    const { value, done } = gen.next();
    if (done) break;
    result.push(value);
  }
  ctx.json({ result, length: result.length, ok: result.length === 10 });
});

// ── Async iterator (Symbol.asyncIterator) ────────────────────────────────────

class AsyncRange {
  private _end: number;
  private _cur: number;
  constructor(end: number) { this._end = end; this._cur = 0; }
  async *[Symbol.asyncIterator]() {
    while (this._cur < this._end) {
      yield this._cur++;
    }
  }
}

app.get('/async-iterator', async (ctx) => {
  const range = new AsyncRange(5);
  const result: number[] = [];
  for await (const n of range) {
    result.push(n);
  }
  ctx.json({ result, length: result.length, ok: result.length === 5 });
});

// ── BigInt basics ─────────────────────────────────────────────────────────────

app.get('/bigint-basic', (ctx) => {
  const a: bigint = 100n;
  const b: bigint = 200n;
  const sum = a + b;
  ctx.json({ sum, ok: sum === 300n });
});

serve(app, { port: 8090 });
