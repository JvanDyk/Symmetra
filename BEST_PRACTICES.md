# Symmetra Engine - Best Practices Guide

**For Production GPU JavaScript/TypeScript Code**

## Core Principles

### 1. Understand GPU Constraints
- **Stack-only allocation** — No dynamic memory, arrays max ~256-1024 elements
- **Synchronous execution** — No true async, fetch not wired, all I/O is simulated
- **Static typing** — Everything is strongly typed at compile time
- **No garbage collection** — All memory is stack-allocated and deterministic

### 2. Write GPU-Friendly Code
```javascript
// ✅ Good: Works efficiently on GPU
function process(arr) {
  return arr.map(x => x * 2);
}

// ❌ Bad: Too large for GPU stack
function process(arr) {
  const copy = structuredClone(arr);  // Over-allocates memory
  return copy;
}
```

## Best Practices by Feature

### Array Methods

**Do Use:**
- `arr.push()` / `arr.pop()` — O(1) operations
- `arr.map()` / `arr.filter()` — Efficient transformations
- `arr.reduce()` / `arr.reduceRight()` — Fold operations
- `arr.slice()` for subarray access

**Avoid or Be Careful:**
- `arr.shift()` / `arr.unshift()` — O(n) operations, use sparingly
- `arr.splice()` — Mutates in-place, ensure array has space
- Nested arrays — Stay flat when possible

```javascript
// ✅ Good: Efficient array operations
const data = [1, 2, 3, 4, 5];
const doubled = data.map(x => x * 2);
const summed = doubled.reduce((a, b) => a + b, 0);

// ❌ Bad: Inefficient mutations
const data = [1, 2, 3, 4, 5];
while (data.length > 0) {
  data.shift();  // O(n) for each element
}
```

### String Methods

**Efficient String Operations:**
```javascript
// ✅ Good: Efficient string handling
const parts = str.split(',');
const result = parts.join(';');
const trimmed = str.trim();

// ❌ Bad: Inefficient patterns
let result = '';
for (const char of str) {
  result += char;  // String concatenation in loops
}
```

### Object Handling

**Do Use:**
- `Object.keys(obj)` / `Object.values(obj)` for enumeration
- `Object.assign()` for shallow merge
- Plain objects for structured data

**Avoid:**
- Dynamic property access on non-Map objects
- `Object.getOwnPropertyNames()` on arbitrary objects (use Map for that)
- Complex computed properties

```javascript
// ✅ Good: Use Maps for dynamic keys
const config = new Map();
config.set('timeout', 1000);
config.set('retries', 3);

// ⚠️ Works but less efficient: Plain objects
const config = { timeout: 1000, retries: 3 };
```

### JSON Handling

**Do:**
```javascript
// ✅ Good: Works well
const obj = { a: 1, b: 2 };
const json = JSON.stringify(obj);
const parsed = JSON.parse(json);

// ✅ Good: ctx.json() is optimized
ctx.json({ status: 'ok', data: result });
```

**Avoid:**
```javascript
// ❌ Bad: Nested objects not optimized yet
const data = {
  user: { name: "Alice" },
  metadata: { version: 1 }
};
const json = JSON.stringify(data);  // May not handle nesting well

// ✅ Workaround: Flatten structure
const data = {
  userName: "Alice",
  metadataVersion: 1
};
```

### Map & Set Usage

**Efficient Patterns:**
```javascript
// ✅ Good: Direct operations
const map = new Map();
map.set('key', value);
const result = map.get('key');
if (map.has('key')) { /* ... */ }

// ❌ Bad: Avoid initialization from iterable
const map = new Map([['a', 1], ['b', 2]]);  // Partially wired

// ✅ Workaround: Manual population
const map = new Map();
map.set('a', 1);
map.set('b', 2);
```

### Class Usage

**Do:**
```javascript
// ✅ Good: Standard class patterns
class Dog {
  constructor(name) {
    this.name = name;
  }
  
  bark() {
    return this.name + ' says woof!';
  }
}

const dog = new Dog('Rex');
```

**Limitations to Know:**
```javascript
// ⚠️ Static initializers work but need explicit call (being fixed)
class Counter {
  static count = 0;
  static { Counter.count = 10; }  // Not auto-called yet
}
// Workaround: Explicitly call init
Counter.__static_init__?.();

// ⚠️ instanceof always returns true for custom classes (being fixed)
if (obj instanceof Dog) { }  // May return true even if not a Dog

// ✅ Workaround: Use type field
class Animal { type = 'animal'; }
class Dog extends Animal { type = 'dog'; }
if (obj.type === 'dog') { }
```

### Error Handling

**Recommended Pattern:**
```javascript
// ✅ Good: Try/catch works on GPU
try {
  const result = riskyOperation();
  ctx.json({ success: true, result });
} catch (e) {
  ctx.status(500).json({ error: e.message });
}
```

## Common Pitfalls

### Pitfall 1: Array Size Limits
```javascript
// ❌ Bad: Exceeds GPU stack
const huge = new Array(10000);  // Will fail or truncate
huge.forEach(() => { /* ... */ });

// ✅ Good: Keep arrays small
const data = new Array(256);  // Safe, typical GPU stack limit
```

### Pitfall 2: Nested Destructuring with Maps
```javascript
// ✅ Works: Simple destructuring
for (const [k, v] of map) {
  console.log(k, v);
}

// ⚠️ Edge case: Numeric key maps
const map = new Map();
map.set(42, 'value');
for (const [k, v] of map) { }  // May not destructure correctly
```

### Pitfall 3: Implicit Type Coercion
```javascript
// ❌ Bad: Implicit coercion not auto-triggered
const result = {}.toString();  // Returns "[object Object]", not "" + {}

// ✅ Good: Explicit conversion
const str = String(value);
const num = Number(value);
```

### Pitfall 4: Missing Initializers
```javascript
// ❌ Bad: Uninitialized variables
let count;
count++;  // Undefined behavior

// ✅ Good: Always initialize
let count = 0;
count++;
```

## Performance Guidelines

### Hot Path Optimization
```javascript
// ✅ Best: Cache computed values
const len = arr.length;
for (let i = 0; i < len; i++) { }  // Avoid arr.length in loop

// ✅ Good: Use built-in methods
arr.forEach(x => { });  // Optimized codegen

// ⚠️ Slower: Manual loops
for (let i = 0; i < arr.length; i++) {
  // Less optimal than forEach
}
```

### Memory Usage
```javascript
// ✅ Good: Non-mutating where possible
const result = arr.map(x => x * 2);

// ⚠️ Be careful: Mutating operations
arr.reverse();  // Modifies original
arr.splice(0, 1);  // In-place modification
```

## Testing on GPU

### Recommended Test Pattern
```javascript
app.post('/test', (ctx) => {
  try {
    // Test small array
    const small = [1, 2, 3];
    console.assert(small.length === 3);
    
    // Test edge case: empty array
    const empty = [];
    console.assert(empty.length === 0);
    
    // Test operations
    const mapped = small.map(x => x * 2);
    console.assert(mapped[0] === 2);
    
    ctx.json({ tests: 'passed' });
  } catch (e) {
    ctx.status(500).json({ error: e.message });
  }
});
```

## Debugging Tips

### 1. Use ctx.json() for Debugging
```javascript
// ✅ Good: Output diagnostic info
ctx.json({
  step: 'processing',
  arrayLen: data.length,
  value: result
});
```

### 2. Check Array Bounds
```javascript
// ✅ Good: Always validate
if (index >= 0 && index < arr.length) {
  return arr[index];
}
```

### 3. Remember GPU Limitations
```javascript
// When something strange happens, consider:
// 1. Array size exceeded limit
// 2. Nested objects not stringified
// 3. Static initializer not called
// 4. instanceof always returning true
```

## Migration Checklist

When porting JavaScript to Symmetra:

- [ ] Remove any async/await that doesn't just delegate to ctx methods
- [ ] Check array sizes — keep under 256 elements typical max
- [ ] Replace WeakMap/WeakSet with Map/Set
- [ ] Simplify object structures — flatten if needed
- [ ] Replace Proxy usage with simpler alternatives
- [ ] Test with small datasets first
- [ ] Remember: `typeof null === "object"` still applies!
- [ ] Use explicit `String()` / `Number()` conversions
- [ ] Cache array length in loops
- [ ] Use Map for dynamic keys, not plain objects

## Summary Table

| Feature | Status | Best For | Avoid |
|---------|--------|----------|-------|
| Array methods | ✅ Complete | Efficient data transformation | Large arrays (>256) |
| String methods | ✅ Complete | Text processing | Unicode beyond BMP |
| Maps | ✅ Complete | Dynamic key storage | Initialization from iterables |
| Sets | ✅ Complete | Unique value tracking | Large collections |
| Classes | ✅ Complete | OOP patterns | Dynamic dispatch |
| JSON | ✅ Mostly | Data serialization | Deeply nested objects |
| Async | ⚠️ Partial | Context operations only | Arbitrary fetch/setTimeout |
| Error handling | ✅ Complete | Exception safety | Implicit recovery |

---

**Version**: Round 6  
**Last Updated**: June 2026  
**Maintained by**: Symmetra Team
