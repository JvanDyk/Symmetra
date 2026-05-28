// test_benchmarks.cpp — Symmetra Performance Benchmarks
//
// Measures compilation and code generation performance across various
// TypeScript patterns and feature combinations. No GPU execution required.

#include "Codegen.h"
#include "parser/SymmParser.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>

namespace symm {

struct BenchmarkResult {
    std::string name;
    int iterations;
    double totalMs;
    double avgUs;
    double minUs;
    double maxUs;
};

static std::vector<BenchmarkResult> gBenchmarks;

static ServerOptions defaultOpts() {
    ServerOptions o;
    o.port = 8080;
    o.verbose = false;
    return o;
}

static std::string codegen(const std::string& ts) {
    SymmParser parser;
    Module mod = parser.parse(ts);
    Codegen cg(defaultOpts());
    return cg.emit(mod);
}

template<typename Fn>
BenchmarkResult benchmark(const std::string& name, int iterations, Fn fn) {
    double minUs = 1e9, maxUs = 0, totalUs = 0;

    for (int i = 0; i < iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(end - start).count();
        minUs = std::min(minUs, us);
        maxUs = std::max(maxUs, us);
        totalUs += us;
    }

    BenchmarkResult result{name, iterations, totalUs / 1000.0, totalUs / iterations, minUs, maxUs};
    gBenchmarks.push_back(result);
    return result;
}

void runBenchmarks() {
    std::cout << "\n=== Compilation Performance Benchmarks ===\n\n";

    // Simple expression
    benchmark("Simple GET endpoint", 100, []() {
        codegen("app.get('/test', (ctx) => { ctx.json({ x: 1 }); });");
    });

    // String operations
    benchmark("String method chain", 100, []() {
        codegen("app.get('/str', (ctx) => {\n"
                "  const s = 'hello';\n"
                "  const r = s.toUpperCase().slice(0, 3);\n"
                "  ctx.json({ r });\n"
                "});");
    });

    // Array operations
    benchmark("Array map/filter/reduce", 100, []() {
        codegen("app.get('/arr', (ctx) => {\n"
                "  const nums = [1, 2, 3, 4, 5];\n"
                "  const result = nums.filter(n => n > 2)\n"
                "    .map(n => n * 2)\n"
                "    .reduce((a, b) => a + b, 0);\n"
                "  ctx.json({ result });\n"
                "});");
    });

    // Object operations
    benchmark("Object manipulation", 100, []() {
        codegen("app.get('/obj', (ctx) => {\n"
                "  const obj = { a: 1, b: 2, c: 3 };\n"
                "  const keys = Object.keys(obj);\n"
                "  const values = Object.values(obj);\n"
                "  ctx.json({ keys, values });\n"
                "});");
    });

    // Class definition
    benchmark("Class with methods", 50, []() {
        codegen("class Point {\n"
                "  constructor(x, y) { this.x = x; this.y = y; }\n"
                "  distance() { return Math.sqrt(this.x * this.x + this.y * this.y); }\n"
                "}\n"
                "app.get('/point', (ctx) => {\n"
                "  const p = new Point(3, 4);\n"
                "  ctx.json({ dist: p.distance() });\n"
                "});");
    });

    // Control flow
    benchmark("Complex control flow", 100, []() {
        codegen("app.get('/flow', (ctx) => {\n"
                "  const x = parseInt(ctx.query.x || '0');\n"
                "  let result;\n"
                "  if (x > 100) result = 'big';\n"
                "  else if (x > 50) result = 'medium';\n"
                "  else result = 'small';\n"
                "  ctx.json({ result });\n"
                "});");
    });

    // Error handling
    benchmark("Try/catch/finally", 100, []() {
        codegen("app.get('/error', (ctx) => {\n"
                "  try {\n"
                "    const x = parseInt(ctx.query.val);\n"
                "    if (x < 0) throw new RangeError('negative');\n"
                "    ctx.json({ value: x });\n"
                "  } catch (e) {\n"
                "    ctx.status(400);\n"
                "    ctx.json({ error: e.message });\n"
                "  } finally {\n"
                "    console.log('done');\n"
                "  }\n"
                "});");
    });

    // Async/await
    benchmark("Async handler", 50, []() {
        codegen("async function getData() {\n"
                "  const result = await Promise.resolve({ data: 42 });\n"
                "  return result;\n"
                "}\n"
                "app.get('/async', async (ctx) => {\n"
                "  const res = await getData();\n"
                "  ctx.json(res);\n"
                "});");
    });

    // Generator
    benchmark("Generator function", 50, []() {
        codegen("function* gen(n) {\n"
                "  for (let i = 0; i < n; i++) yield i;\n"
                "}\n"
                "app.get('/gen', (ctx) => {\n"
                "  const g = gen(10);\n"
                "  ctx.json({ type: 'generator' });\n"
                "});");
    });

    // BigInt operations
    benchmark("BigInt computation", 100, []() {
        codegen("app.get('/bigint', (ctx) => {\n"
                "  const a = 9007199254740991n;\n"
                "  const b = 1n;\n"
                "  const c = a + b;\n"
                "  ctx.json({ result: c.toString() });\n"
                "});");
    });

    // Set/Map operations
    benchmark("Set and Map", 100, []() {
        codegen("app.get('/collections', (ctx) => {\n"
                "  const s = new Set([1, 2, 3, 4, 5]);\n"
                "  const m = new Map();\n"
                "  m.set('a', 1);\n"
                "  m.set('b', 2);\n"
                "  ctx.json({ setSize: s.size, mapSize: m.size });\n"
                "});");
    });

    // Complex data pipeline
    benchmark("Complex pipeline (real-world)", 30, []() {
        codegen("app.post('/pipeline', (ctx) => {\n"
                "  const data = [\n"
                "    { id: 1, val: 10, type: 'A' },\n"
                "    { id: 2, val: 20, type: 'B' },\n"
                "    { id: 3, val: 30, type: 'A' }\n"
                "  ];\n"
                "  const result = data\n"
                "    .filter(d => d.type === 'A')\n"
                "    .map(d => ({ ...d, doubled: d.val * 2 }))\n"
                "    .reduce((sum, d) => sum + d.doubled, 0);\n"
                "  ctx.json({ result });\n"
                "});");
    });

    // Template literals
    benchmark("Template literals", 100, []() {
        codegen("app.get('/template', (ctx) => {\n"
                "  const user = ctx.query.user || 'guest';\n"
                "  const count = ctx.query.count || '0';\n"
                "  const msg = `User ${user} has ${count} items`;\n"
                "  ctx.json({ msg });\n"
                "});");
    });

    // Destructuring patterns
    benchmark("Complex destructuring", 100, []() {
        codegen("app.get('/destruct', (ctx) => {\n"
                "  const [a, b, ...rest] = [1, 2, 3, 4, 5];\n"
                "  const { x, y, ...props } = { x: 1, y: 2, z: 3, w: 4 };\n"
                "  ctx.json({ a, b, rest, x, y, props });\n"
                "});");
    });

    // Spread operators
    benchmark("Spread operators", 100, []() {
        codegen("app.get('/spread', (ctx) => {\n"
                "  const arr1 = [1, 2, 3];\n"
                "  const arr2 = [...arr1, 4, 5];\n"
                "  const obj1 = { a: 1, b: 2 };\n"
                "  const obj2 = { ...obj1, c: 3 };\n"
                "  ctx.json({ arr2, obj2 });\n"
                "});");
    });

    // JSON operations
    benchmark("JSON stringify/parse", 100, []() {
        codegen("app.post('/json', (ctx) => {\n"
                "  const obj = { x: 1, y: [2, 3], z: { a: 4 } };\n"
                "  const str = JSON.stringify(obj);\n"
                "  const parsed = JSON.parse(str);\n"
                "  ctx.json({ success: true });\n"
                "});");
    });

    // Print results
    std::cout << std::setw(40) << std::left << "Benchmark"
              << std::setw(12) << "Iterations"
              << std::setw(10) << "Total (ms)"
              << std::setw(12) << "Avg (µs)"
              << std::setw(12) << "Min (µs)"
              << std::setw(12) << "Max (µs)\n";
    std::cout << std::string(98, '-') << "\n";

    double totalMs = 0;
    for (const auto& result : gBenchmarks) {
        std::cout << std::setw(40) << std::left << result.name
                  << std::setw(12) << result.iterations
                  << std::fixed << std::setprecision(2)
                  << std::setw(10) << result.totalMs
                  << std::setw(12) << result.avgUs
                  << std::setw(12) << result.minUs
                  << std::setw(12) << result.maxUs << "\n";
        totalMs += result.totalMs;
    }

    std::cout << std::string(98, '-') << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(2) << totalMs << " ms\n";
    std::cout << "Benchmarks completed: " << gBenchmarks.size() << "\n";
}

}  // namespace symm

int main() {
    symm::runBenchmarks();
    return 0;
}
