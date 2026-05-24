/**
 * Classes example — Symmetra
 *
 * Demonstrates the full JS/TS class feature set — all of which compile to
 * CUDA C++ structs on GPU:
 *   - Class declaration and instantiation
 *   - Constructor, instance methods, static methods
 *   - Inheritance (extends) + super calls
 *   - Getters / setters
 *   - Private fields (#field)
 *   - Abstract-style classes (no abstract keyword needed)
 *   - Generic classes
 *   - Symbol.iterator (for..of on custom class)
 *   - Mixins
 *   - Method decorators (TypeScript experimental)
 */

import { serve, Router } from '@symmetra/http';

const app = new Router();

// ── 1. Basic class + inheritance ────────────────────────────────────────────

class Shape {
  constructor(public name: string) {}

  area(): number { return 0; }
  perimeter(): number { return 0; }

  describe(): string {
    return `${this.name}: area=${this.area().toFixed(2)} perimeter=${this.perimeter().toFixed(2)}`;
  }
}

class Circle extends Shape {
  constructor(public radius: number) {
    super('Circle');
  }
  area():      number { return Math.PI * this.radius ** 2; }
  perimeter(): number { return 2 * Math.PI * this.radius; }
}

class Rectangle extends Shape {
  constructor(public w: number, public h: number) {
    super('Rectangle');
  }
  area():      number { return this.w * this.h; }
  perimeter(): number { return 2 * (this.w + this.h); }
}

class Triangle extends Shape {
  constructor(public a: number, public b: number, public c: number) {
    super('Triangle');
  }
  perimeter(): number { return this.a + this.b + this.c; }
  area(): number {
    const s = this.perimeter() / 2;
    return Math.sqrt(s * (s - this.a) * (s - this.b) * (s - this.c));
  }
}

app.get('/shapes', (ctx) => {
  const shapes: Shape[] = [
    new Circle(5),
    new Rectangle(4, 6),
    new Triangle(3, 4, 5),
  ];
  ctx.json(shapes.map(s => s.describe()));
});

// ── 2. Static methods + class-level state ───────────────────────────────────

class Counter {
  static #count = 0;
  #id: number;

  constructor() {
    this.#id = ++Counter.#count;
  }

  get id(): number { return this.#id; }
  static total(): number { return Counter.#count; }
  static reset(): void   { Counter.#count = 0; }
}

app.get('/counter', (ctx) => {
  const a = new Counter();
  const b = new Counter();
  const c = new Counter();
  ctx.json({ ids: [a.id, b.id, c.id], total: Counter.total() });
});

// ── 3. Getters and setters ───────────────────────────────────────────────────

class Temperature {
  #celsius: number;

  constructor(celsius: number) { this.#celsius = celsius; }

  get celsius():    number { return this.#celsius; }
  set celsius(v: number)   { this.#celsius = v; }

  get fahrenheit(): number { return this.#celsius * 9/5 + 32; }
  set fahrenheit(v: number) { this.#celsius = (v - 32) * 5/9; }

  get kelvin():     number { return this.#celsius + 273.15; }
}

app.get('/temperature/:c', (ctx) => {
  const t = new Temperature(parseFloat(ctx.params.c));
  ctx.json({
    celsius:    t.celsius,
    fahrenheit: t.fahrenheit,
    kelvin:     t.kelvin,
  });
});

// ── 4. Generic class ────────────────────────────────────────────────────────

class Stack<T> {
  #items: T[] = [];

  push(item: T): void   { this.#items.push(item); }
  pop(): T | undefined  { return this.#items.pop(); }
  peek(): T | undefined { return this.#items[this.#items.length - 1]; }
  get size(): number    { return this.#items.length; }
  isEmpty(): boolean    { return this.#items.length === 0; }

  [Symbol.iterator](): Iterator<T> {
    let i = this.#items.length;
    return {
      next: (): IteratorResult<T> => {
        if (i > 0) return { value: this.#items[--i], done: false };
        return { value: undefined as unknown as T, done: true };
      },
    };
  }
}

app.post('/stack', async (ctx) => {
  const { items, ops } = await ctx.json<{
    items: number[];
    ops:   Array<{ op: 'push' | 'pop' | 'peek'; value?: number }>;
  }>();

  const stack = new Stack<number>();
  for (const item of items) stack.push(item);

  const results: unknown[] = [];
  for (const { op, value } of ops) {
    if (op === 'push' && value !== undefined) { stack.push(value); results.push({ pushed: value }); }
    else if (op === 'pop')  results.push({ popped: stack.pop() });
    else if (op === 'peek') results.push({ peeked: stack.peek() });
  }

  // Symbol.iterator — for..of on custom class
  const remaining: number[] = [];
  for (const v of stack) remaining.push(v);

  ctx.json({ results, remaining, size: stack.size });
});

// ── 5. Mixin pattern ────────────────────────────────────────────────────────

type Constructor<T = object> = new (...args: unknown[]) => T;

function Serialisable<Base extends Constructor>(B: Base) {
  return class extends B {
    toJSON(): string { return JSON.stringify(this); }
    static fromJSON<T>(json: string): T { return JSON.parse(json) as T; }
  };
}

function Validatable<Base extends Constructor>(B: Base) {
  return class extends B {
    validate(): boolean { return true; }  // override per class
    assertValid(): this {
      if (!this.validate()) throw new Error(`${this.constructor.name} is invalid`);
      return this;
    }
  };
}

class UserBase {
  constructor(public email: string, public age: number) {}
}

class User extends Serialisable(Validatable(UserBase)) {
  validate(): boolean {
    return /^[^@]+@[^@]+\.[^@]+$/.test(this.email) && this.age >= 0 && this.age < 150;
  }
}

app.post('/user', async (ctx) => {
  const { email, age } = await ctx.json<{ email: string; age: number }>();
  try {
    const user = new User(email, age).assertValid();
    ctx.json({ valid: true, serialised: user.toJSON() });
  } catch (err) {
    ctx.status(422).json({ valid: false, error: String(err) });
  }
});

// ── 6. Abstract-style class with template method pattern ─────────────────────

class Pipeline<In, Out> {
  protected steps: Array<(input: unknown) => unknown> = [];

  pipe<Next>(fn: (input: Out) => Next): Pipeline<In, Next> {
    const p = new Pipeline<In, Next>();
    p.steps = [...this.steps, fn as (input: unknown) => unknown];
    return p;
  }

  run(input: In): Out {
    return this.steps.reduce((acc, fn) => fn(acc), input as unknown) as Out;
  }
}

app.post('/pipeline', async (ctx) => {
  const { value } = await ctx.json<{ value: number }>();

  const result = new Pipeline<number, number>()
    .pipe(x => x * 2)
    .pipe(x => x + 10)
    .pipe(x => Math.sqrt(x))
    .run(value);

  ctx.json({ input: value, output: result });
});

serve(app, { port: 8086 });
