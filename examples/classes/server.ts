/**
 * Symmetra Classes Example
 * Demonstrates: classes, inheritance, generics, private fields, mixins,
 *               Symbol.asyncIterator, static methods, decorators.
 * Run: sym ./server.ts
 */

import { serve, Router } from '@symmetra/http';

const app = new Router();

// ── Basic class + methods ─────────────────────────────────────────────────────

class Vec3 {
  constructor(public x: number, public y: number, public z: number) {}

  add(other: Vec3): Vec3     { return new Vec3(this.x + other.x, this.y + other.y, this.z + other.z); }
  sub(other: Vec3): Vec3     { return new Vec3(this.x - other.x, this.y - other.y, this.z - other.z); }
  scale(s: number): Vec3     { return new Vec3(this.x * s, this.y * s, this.z * s); }
  dot(other: Vec3): number   { return this.x * other.x + this.y * other.y + this.z * other.z; }
  cross(other: Vec3): Vec3   {
    return new Vec3(
      this.y * other.z - this.z * other.y,
      this.z * other.x - this.x * other.z,
      this.x * other.y - this.y * other.x,
    );
  }
  length(): number           { return Math.sqrt(this.dot(this)); }
  normalize(): Vec3          { return this.scale(1 / this.length()); }
  toArray(): [number, number, number] { return [this.x, this.y, this.z]; }
  toString(): string         { return `Vec3(${this.x}, ${this.y}, ${this.z})`; }
}

app.get('/class/vec3', (ctx) => {
  const a = new Vec3(1, 2, 3);
  const b = new Vec3(4, 5, 6);
  ctx.json({
    add:       a.add(b).toArray(),
    sub:       a.sub(b).toArray(),
    dot:       a.dot(b),
    cross:     a.cross(b).toArray(),
    length:    parseFloat(a.length().toFixed(6)),
    normalize: a.normalize().toArray().map(v => parseFloat(v.toFixed(6))),
    scale:     a.scale(2).toArray(),
  });
});

// ── Inheritance ───────────────────────────────────────────────────────────────

abstract class Shape {
  abstract area(): number;
  abstract perimeter(): number;
  describe(): string { return `${this.constructor.name}: area=${this.area().toFixed(2)}, perimeter=${this.perimeter().toFixed(2)}`; }
}

class Circle extends Shape {
  constructor(public radius: number) { super(); }
  area(): number      { return Math.PI * this.radius ** 2; }
  perimeter(): number { return 2 * Math.PI * this.radius; }
}

class Rectangle extends Shape {
  constructor(public width: number, public height: number) { super(); }
  area(): number      { return this.width * this.height; }
  perimeter(): number { return 2 * (this.width + this.height); }
}

class Triangle extends Shape {
  constructor(public a: number, public b: number, public c: number) { super(); }
  perimeter(): number { return this.a + this.b + this.c; }
  area(): number {
    const s = this.perimeter() / 2;
    return Math.sqrt(s * (s - this.a) * (s - this.b) * (s - this.c));
  }
}

app.get('/class/shapes', (ctx) => {
  const shapes: Shape[] = [
    new Circle(5),
    new Rectangle(4, 6),
    new Triangle(3, 4, 5),
  ];
  ctx.json(shapes.map(s => ({ type: s.constructor.name, describe: s.describe() })));
});

// ── Generic class ─────────────────────────────────────────────────────────────

class Stack<T> {
  private _items: T[] = [];

  push(item: T): void      { this._items.push(item); }
  pop(): T | undefined     { return this._items.pop(); }
  peek(): T | undefined    { return this._items[this._items.length - 1]; }
  get size(): number       { return this._items.length; }
  get isEmpty(): boolean   { return this._items.length === 0; }
  toArray(): T[]           { return [...this._items]; }
  clear(): void            { this._items = []; }
}

app.get('/class/stack', (ctx) => {
  const stack = new Stack<number>();
  stack.push(1); stack.push(2); stack.push(3);
  const top  = stack.peek();
  const popped = stack.pop();
  ctx.json({
    topBeforePop: top,
    popped,
    size:    stack.size,
    isEmpty: stack.isEmpty,
    remaining: stack.toArray(),
  });
});

// ── Private fields ────────────────────────────────────────────────────────────

class Counter {
  #count = 0;
  #max: number;

  constructor(max: number) { this.#max = max; }

  increment(): boolean {
    if (this.#count >= this.#max) return false;
    this.#count++;
    return true;
  }

  get value(): number  { return this.#count; }
  get maxed(): boolean { return this.#count >= this.#max; }
  reset(): void        { this.#count = 0; }
}

app.get('/class/private-fields', (ctx) => {
  const counter = new Counter(3);
  const results = [counter.increment(), counter.increment(), counter.increment(), counter.increment()];
  ctx.json({
    value:   counter.value,
    maxed:   counter.maxed,
    results,
    ok: counter.value === 3 && counter.maxed && results[3] === false,
  });
});

// ── Static methods ────────────────────────────────────────────────────────────

class MathUtils {
  static clamp(x: number, min: number, max: number): number { return Math.min(Math.max(x, min), max); }
  static lerp(a: number, b: number, t: number): number      { return a + (b - a) * t; }
  static map(x: number, inMin: number, inMax: number, outMin: number, outMax: number): number {
    return outMin + ((x - inMin) / (inMax - inMin)) * (outMax - outMin);
  }
  static roundTo(x: number, decimals: number): number {
    const factor = 10 ** decimals;
    return Math.round(x * factor) / factor;
  }
}

app.get('/class/static', (ctx) => {
  ctx.json({
    clamp:  MathUtils.clamp(150, 0, 100),
    lerp:   MathUtils.lerp(0, 100, 0.5),
    map:    MathUtils.map(5, 0, 10, -1, 1),
    roundTo: MathUtils.roundTo(3.14159, 2),
  });
});

// ── Symbol.asyncIterator on a class ──────────────────────────────────────────

class Range {
  constructor(private _start: number, private _end: number, private _step = 1) {}

  async *[Symbol.asyncIterator](): AsyncGenerator<number> {
    for (let i = this._start; i < this._end; i += this._step) {
      yield i;
    }
  }

  [Symbol.iterator](): Iterator<number> {
    let i = this._start;
    const end = this._end;
    const step = this._step;
    return {
      next(): IteratorResult<number> {
        if (i >= end) return { value: undefined as unknown as number, done: true };
        const value = i;
        i += step;
        return { value, done: false };
      },
    };
  }
}

app.get('/class/async-iterator', async (ctx) => {
  const range  = new Range(0, 8, 2);
  const result: number[] = [];
  for await (const n of range) result.push(n);

  const syncResult = [...new Range(0, 5)];

  ctx.json({ async: result, sync: syncResult, ok: result.length === 4 && syncResult.length === 5 });
});

// ── Mixin pattern ─────────────────────────────────────────────────────────────

type Constructor<T = {}> = new (...args: unknown[]) => T;

function Serializable<TBase extends Constructor>(Base: TBase) {
  return class extends Base {
    serialize(): string { return JSON.stringify(this); }
    static deserialize<T>(json: string): T { return JSON.parse(json); }
  };
}

function Timestamped<TBase extends Constructor>(Base: TBase) {
  return class extends Base {
    readonly createdAt: number = Date.now();
    readonly updatedAt: number = Date.now();
  };
}

class BaseUser {
  constructor(public name: string, public email: string) {}
}

const TimestampedSerializableUser = Serializable(Timestamped(BaseUser));

class User extends TimestampedSerializableUser {
  constructor(name: string, email: string, public role: string) {
    super(name, email);
  }
}

app.get('/class/mixin', (ctx) => {
  const user = new User('Alice', 'alice@example.com', 'admin');
  const json = user.serialize();
  ctx.json({
    user:        JSON.parse(json),
    hasCreatedAt: 'createdAt' in user,
    hasSerialize: typeof user.serialize === 'function',
  });
});

// ── Start ─────────────────────────────────────────────────────────────────────

serve(app, { port: 8080 });
