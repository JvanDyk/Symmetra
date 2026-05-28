# Symmetra Test Automation Infrastructure

## Overview

Symmetra has a comprehensive, fully automated test infrastructure designed for continuous integration and development. The system includes:

1. **Unit Tests** (763 tests) — Core language features and codegen correctness
2. **Integration Tests** (22 tests) — Multi-feature interactions and complex scenarios
3. **End-to-End Tests** (13 tests) — Real-world API and algorithm scenarios
4. **Performance Benchmarks** (16 tests) — Compilation speed and efficiency metrics
5. **Automated Runner** — PowerShell script for orchestrating all test types

## Quick Start

### Run All Tests

```powershell
cd D:\GITHUB\SYMMETRA\engine
powershell -ExecutionPolicy Bypass -File .\run_tests.ps1 -Report
```

**Output:**
- Runs all 763 unit tests
- Executes 16 performance benchmarks
- Runs CTest suite
- Generates TEST_REPORT.md with results

**Expected output:**
```
Results: 763 passed, 0 failed
Total benchmark time: ~100-120 ms
All tests: [OK] PASS
```

### Run Individual Test Suites

#### Unit Tests Only
```powershell
cd D:\GITHUB\SYMMETRA\engine\build\Release
.\codegen_test.exe
```

#### Performance Benchmarks Only
```powershell
cd D:\GITHUB\SYMMETRA\engine\build\Release
.\benchmark_test.exe
```

#### Using CTest
```powershell
cd D:\GITHUB\SYMMETRA\engine\build
ctest -C Release --output-on-failure
```

## Test Structure

### Unit Tests (src/compiler/test_codegen.cpp)

**Organization:**
- 763 total tests across 40+ test functions
- Organized by feature category
- Each test validates codegen output contains expected patterns

**Categories:**
- Core language (literals, variables, operators)
- Control flow (if/else, switch, loops)
- Functions (declarations, arrows, rest params)
- Collections (arrays, objects, Set, Map)
- Advanced features (classes, async, generators)
- Phase 16+ features (Symbol, iterators, delete operator)

**Key test functions:**
```cpp
testLiterals()              // 10 tests
testVariables()             // 12 tests
testOperators()             // 28 tests
testControlFlow()           // 8 tests
testFunctions()             // 8 tests
testCollections()           // 2 tests
testBigInt()                // 4 tests
testIteratorProtocol()      // 3 tests
testDeleteAndOperators()    // 5 tests
// ... and many more
```

### Integration Tests (test_codegen.cpp)

**Function:** `testIntegrationComprehensive()`

**Coverage:**
- Array pipeline operations (filter + map + reduce)
- Class methods with arrow functions
- Nested object destructuring
- Multi-level error handling
- Template strings with expressions
- Collection operations (Set, Map)
- Operator chains (ternary, nullish coalescing)
- Iterator protocols with custom objects
- Generator functions with state machines

**Example test:**
```cpp
TEST("Array processing with filter, map, and reduce", {
    auto src = codegen(
        "app.get('/process', (ctx) => {\n"
        "  const nums = [1, 2, 3, 4, 5];\n"
        "  const result = nums\n"
        "    .filter(n => n > 2)\n"
        "    .map(n => n * 2)\n"
        "    .reduce((acc, n) => acc + n, 0);\n"
        "  ctx.json({ result });\n"
        "});"
    );
    ASSERT_CONTAINS(src, "filter");
    ASSERT_CONTAINS(src, "map");
    ASSERT_CONTAINS(src, "reduce");
});
```

### End-to-End Tests (test_codegen.cpp)

**Function:** `testEndToEndScenarios()`

**Real-world scenarios:**
1. User registration validation pipeline
2. Shopping cart calculation with tax
3. Leaderboard ranking with sorting
4. Data deduplication with Set
5. CSV parsing with transformation
6. Graph traversal with BFS
7. Report generation with grouping
8. Rate limiting counter
9. Request batching and aggregation
10. Event log analysis
11. Cache hit/miss simulation
12. Recursive factorial with memoization
13. Binary search on sorted array
14. Fibonacci with dynamic programming

**Purpose:** Validate that realistic, complex TypeScript programs compile correctly with full feature support.

### Performance Benchmarks (src/compiler/test_benchmarks.cpp)

**Structure:**
- 16 compilation scenarios
- Measures parsing + codegen time for each scenario
- Reports min/max/average microseconds
- Total time for full benchmark suite

**Metrics reported:**
```
Benchmark                               Iterations  Total (ms) Avg (µs)
───────────────────────────────────────────────────────────────────────
Simple GET endpoint                     100         14.72      147.16
String method chain                     100         11.99      119.93
Array map/filter/reduce                 100         12.46      124.58
...
Complex pipeline (real-world)           30          1.56       51.91
───────────────────────────────────────────────────────────────────────
Total time: 112.32 ms
Benchmarks completed: 16
```

**Scenarios:**
- Simple expressions (GET endpoint)
- String operations (method chains)
- Array operations (map/filter/reduce)
- Object manipulation
- Class definitions with methods
- Complex control flow
- Error handling (try/catch/finally)
- Async/await patterns
- Generator functions
- BigInt operations
- Collections (Set, Map)
- Data pipelines
- Template literals
- Destructuring
- Spread operators
- JSON operations

## Test Automation Script (run_tests.ps1)

**Features:**
- Builds project if `-Build` flag is provided
- Runs codegen_test.exe (763 tests)
- Runs benchmark_test.exe (16 scenarios)
- Runs CTest suite
- Generates TEST_REPORT.md if `-Report` flag is provided

**Usage:**
```powershell
# Full run with report generation
.\run_tests.ps1 -Report

# Build + run + report
.\run_tests.ps1 -Build -Report

# Run only (no build, no report)
.\run_tests.ps1
```

**Phases:**
1. Build (optional) — CMake Release build
2. Unit tests — 763 codegen tests
3. Benchmarks — 16 compilation scenarios
4. CTest — Framework tests
5. Report generation (optional) — Markdown report with metrics

## Expected Test Results

All tests should pass with this output:

```
Results: 763 passed, 0 failed
Total benchmark time: 107 ms
Benchmarks completed: 16
[OK] PASS (CTest)
```

## Integrating with CI/CD

### GitHub Actions Example

```yaml
name: Test Suite
on: [push, pull_request]
jobs:
  test:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build and Test
        run: |
          cd engine
          powershell -ExecutionPolicy Bypass -File run_tests.ps1 -Build -Report
      - name: Upload Report
        if: always()
        uses: actions/upload-artifact@v2
        with:
          name: test-report
          path: engine/TEST_REPORT.md
```

### Azure Pipelines Example

```yaml
trigger:
  - main
pool:
  vmImage: 'windows-latest'
steps:
  - script: |
      cd engine
      powershell -ExecutionPolicy Bypass -File run_tests.ps1 -Build -Report
    displayName: 'Run Test Suite'
  - task: PublishBuildArtifacts@1
    inputs:
      PathtoPublish: 'engine/TEST_REPORT.md'
      ArtifactName: 'test-report'
```

## Adding New Tests

### Adding Unit Tests

Edit `src/compiler/test_codegen.cpp`:

```cpp
static void testMyNewFeature() {
    std::cout << "\n=== My New Feature ===\n";
    
    TEST("Feature test 1", {
        auto src = codegen("... TypeScript code ...");
        ASSERT_CONTAINS(src, "expected pattern");
    });
    
    TEST("Feature test 2", {
        auto src = codegen("... TypeScript code ...");
        ASSERT_CONTAINS(src, "another pattern");
    });
}
```

Then add to main():
```cpp
symm::testMyNewFeature();
```

### Adding Integration Tests

Add to `testIntegrationComprehensive()`:
```cpp
TEST("New integration scenario", {
    auto src = codegen("... multi-feature code ...");
    ASSERT_CONTAINS(src, "pattern1");
    ASSERT_CONTAINS(src, "pattern2");
});
```

### Adding Benchmarks

Edit `src/compiler/test_benchmarks.cpp`:

```cpp
benchmark("My benchmark name", 100, []() {
    codegen("... TypeScript code ...");
});
```

## Debugging Failed Tests

### Identify Failing Test
```powershell
cd D:\GITHUB\SYMMETRA\engine\build\Release
.\codegen_test.exe 2>&1 | Select-String -Pattern "FAIL"
```

### View Generated Code
The test output shows first 2000 chars of generated CUDA code. Look for:
- Missing expected patterns (ASSERT_CONTAINS failures)
- Syntax errors in generated code
- Type mismatches

### Fix and Re-test
1. Identify the issue in Codegen.cpp or PreludeData.cpp
2. Make the fix
3. Rebuild: `cmake --build . --config Release`
4. Re-run: `.\codegen_test.exe`

## Performance Baseline

**Current Performance (May 2026):**
- Median compilation time: ~100 µs per test
- Peak compilation time: ~350 µs (class definitions)
- Minimum compilation time: ~40 µs (simple expressions)
- Full benchmark suite: ~107 ms for 16 scenarios

**Target Performance:**
- Keep median under 150 µs
- Keep peak under 500 µs
- Full suite under 200 ms

## Test Coverage Summary

| Category | Tests | Status | Coverage |
|----------|-------|--------|----------|
| Core language | 140+ | ✅ | 100% |
| Collections | 50+ | ✅ | 100% |
| Control flow | 50+ | ✅ | 100% |
| Functions | 50+ | ✅ | 100% |
| Advanced features | 100+ | ✅ | 95%+ |
| Phase 16+ features | 50+ | ✅ | 100% |
| Integration scenarios | 22 | ✅ | 100% |
| End-to-end real-world | 13 | ✅ | 100% |
| **Total** | **763** | **✅** | **100% pass** |

## Maintenance

### Weekly Tasks
- Run full test suite: `run_tests.ps1 -Report`
- Review TEST_REPORT.md for regressions
- Check benchmark times for performance degradation

### After Code Changes
- Run full test suite before committing
- Ensure all 763 tests pass
- Check for performance regressions
- Add new tests for new features

### Quarterly Review
- Analyze test coverage gaps
- Update benchmarks for new scenarios
- Refactor tests for clarity
- Update this documentation

## Support

For test-related issues:
- Check TEST_REPORT.md for detailed results
- Review test source code in src/compiler/test_*.cpp
- Rebuild project: `cmake --build . --config Release`
- Run individual tests with: `.\codegen_test.exe` or `.\benchmark_test.exe`
