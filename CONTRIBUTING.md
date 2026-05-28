# Contributing to Symmetra

Thank you for your interest in contributing to Symmetra! This document provides guidelines for contributing code, documentation, and feedback.

---

## 🎯 Code of Conduct

Please be respectful and inclusive. We're building a welcoming community.

**Our Values:**
- ✅ Respect all contributors
- ✅ Assume good intent
- ✅ Welcome diverse perspectives
- ✅ Support learning and growth
- ✅ Address conflicts constructively

---

## 🚀 Getting Started

### Prerequisites

- **Windows 10/11** or **Linux** (Ubuntu 22.04+)
- **Git** for version control
- **CUDA 13.1+** for GPU support
- **Visual Studio 2022** Build Tools or **GCC 11+**
- **CMake 3.20+** for builds
- **esbuild** for TypeScript

### Setup Development Environment

```powershell
# 1. Clone repository
git clone https://github.com/symmetra/symmetra.git
cd symmetra

# 2. Validate prerequisites
.\Validate-Prerequisites.ps1

# 3. Build locally
.\Build.ps1 -Action Full -Configuration Debug

# 4. Run tests
ctest -C Debug --output-on-failure

# 5. You're ready to contribute!
```

---

## 📋 Contribution Types

### 1. Bug Reports

**File an Issue**: https://github.com/symmetra/symmetra/issues

**Include:**
- [ ] Clear description of the bug
- [ ] Steps to reproduce
- [ ] Expected vs actual behavior
- [ ] System information (GPU, CUDA version, OS)
- [ ] Log output or error message

**Example:**
```
Title: HTTP parser fails with headers >8KB

Description:
When sending HTTP request with headers larger than 8KB,
the parser crashes with CUDA error.

Steps to reproduce:
1. Send GET request with 9KB of headers
2. Observe crash

Expected:
Should either accept large headers or return 431 error

Actual:
Process crashes

System:
- GPU: RTX 4090
- CUDA: 13.2
- Driver: 535.01
- OS: Windows 11 x64
```

### 2. Feature Requests

**File an Issue**: https://github.com/symmetra/symmetra/issues (label: `enhancement`)

**Include:**
- [ ] Clear description of desired feature
- [ ] Use case and motivation
- [ ] Proposed implementation approach (optional)
- [ ] Examples of how it would be used

**Example:**
```
Title: Add support for async/await generators

Description:
Users want to be able to use generator functions
with async/await syntax on GPU:

async function* streamData() {
  for (let i = 0; i < count; i++) {
    yield await process(i);
  }
}

Motivation:
Many TypeScript developers use generators for
streaming data processing. This would enable
that pattern on GPU.
```

### 3. Code Contributions

**Process:**
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add/update tests
5. Submit pull request

**Guidelines:**
- One feature per PR
- Keep PRs focused and small
- Include tests for all changes
- Update documentation
- Follow code style
- Ensure all tests pass

### 4. Documentation Improvements

**Areas:**
- [ ] README and quick start
- [ ] API documentation
- [ ] Examples and tutorials
- [ ] Deployment guides
- [ ] Troubleshooting guides

**Process:**
1. Edit markdown files
2. Check for typos and clarity
3. Test code examples
4. Submit PR

### 5. Performance Improvements

**Areas:**
- [ ] HTTP parsing optimization
- [ ] Route caching improvements
- [ ] Memory pool management
- [ ] GPU kernel efficiency
- [ ] Compiler optimizations

**Process:**
1. Benchmark before changes
2. Make optimization
3. Benchmark after changes
4. Document performance gain
5. Submit PR with metrics

### 6. Security Fixes

**Process:**
1. Do NOT open public issues
2. Email security@symmetra.dev or use GitHub security advisory
3. Provide detailed description
4. Allow time for fix
5. Coordinate public disclosure

See [SECURITY.md](SECURITY.md) for details.

---

## 📝 Code Style Guide

### C++

**File Organization:**
```cpp
// Copyright and license header
#pragma once

// Includes (system, then project)
#include <cstdint>
#include "MyHeader.h"

// Using declarations
using std::string;

// Type definitions
struct MyStruct { };
class MyClass { };

// Function implementations
void MyFunction() { }
```

**Naming Conventions:**
```cpp
// Classes: PascalCase
class HttpParser { };

// Functions: camelCase
void parseHttpRequest() { }

// Variables: camelCase
int requestCount = 0;

// Constants: UPPER_CASE
const int MAX_BUFFER_SIZE = 4096;

// Private members: _prefix
class MyClass {
private:
    int _internalState;
};

// CUDA kernels: kernel_name
__global__ void kernel_compute() { }
```

**Formatting:**
```cpp
// 80 characters per line (soft limit)
// Indentation: 4 spaces (no tabs)
// Braces: Allman style for functions
void MyFunction()
{
    if (condition)
    {
        // code
    }
}

// Spaces around operators
int x = a + b * c;

// Space after keywords
if (condition) { }
for (int i = 0; i < n; i++) { }
```

**Comments:**
```cpp
// Single-line comments
// Start with capital letter
// End with period

/*
 * Multi-line comments
 * for complex explanations
 */

// Avoid comments that just repeat code
int x = a + b;  // ❌ Add a and b

// Write why, not what
// Use bump-pointer allocation for O(1) allocation
int x = a + b;  // ✅ Better
```

**Error Handling:**
```cpp
// Use error codes
ErrorCode result = function();
if (result != ErrorCode::OK) {
    // Handle error
    return result;
}

// Or throw on truly exceptional cases
try {
    // code
} catch (const std::exception& e) {
    // Handle
}
```

### TypeScript/JavaScript

**File Organization:**
```typescript
// Imports first
import { helper } from './utils';

// Type definitions
interface User {
    id: number;
    name: string;
}

// Constants
const MAX_USERS = 1000;

// Functions
export function processUser(user: User): boolean {
    return true;
}
```

**Naming Conventions:**
```typescript
// Classes: PascalCase
class UserManager { }

// Functions: camelCase
function getUserById(id: number): User { }

// Variables: camelCase
const userData = {};

// Constants: UPPER_CASE
const API_ENDPOINT = 'http://api.example.com';

// Interfaces: PascalCase
interface IUserService { }
```

**Formatting:**
```typescript
// 80 characters per line (soft limit)
// Indentation: 2 spaces
const obj = {
  key: 'value',
  nested: {
    value: 123
  }
};

// Type annotations
const count: number = 42;
function add(a: number, b: number): number {
  return a + b;
}

// Avoid any type
const value: any = someFunc();  // ❌ Avoid
const value: unknown = someFunc();  // ✅ Better
const value: MyType = someFunc();  // ✅ Best
```

---

## 🧪 Testing Requirements

**All contributions must include tests:**

### Unit Tests
```cpp
// Test single components
void test_http_parser_simple_request() {
    const char* request = "GET / HTTP/1.1\r\n\r\n";
    auto result = parseHttpRequest(request);
    assert(result.method == "GET");
}
```

### Integration Tests
```cpp
// Test components working together
void test_http_server_full_request() {
    // Start server
    // Send request
    // Verify response
}
```

### Performance Tests
```cpp
// Benchmark improvements
void benchmark_new_feature() {
    auto start = high_resolution_clock::now();
    // Run feature
    auto end = high_resolution_clock::now();
    // Verify performance meets target
}
```

### Running Tests
```powershell
# All tests
ctest -C Release

# Specific test
ctest -R test_name -V

# With output
ctest -C Release --output-on-failure
```

**Test Coverage Requirements:**
- New features: >80% code coverage
- Bug fixes: Test for the bug
- Refactoring: Same test coverage as original
- Performance: Benchmark before/after

---

## 🔍 Pull Request Process

### Before Submitting

1. **Code Review Checklist:**
   - [ ] Code follows style guide
   - [ ] Tests written and passing
   - [ ] Documentation updated
   - [ ] No breaking changes (or documented)
   - [ ] Performance impact considered
   - [ ] Security implications reviewed

2. **Local Testing:**
   ```powershell
   # Build locally
   .\Build.ps1 -Action Full -Configuration Release
   
   # Run all tests
   ctest -C Release --output-on-failure
   
   # Check code style
   # (Manual review for now)
   ```

### Submitting PR

**Title**: Clear, descriptive title
```
✅ Fix: HTTP parser crashes with large headers
✅ Feature: Add Symbol.iterator protocol support
✅ Perf: Optimize route matching to O(1)
❌ Update stuff
❌ Fix bug
```

**Description**:
```markdown
## Summary
Brief description of changes

## Motivation
Why this change is needed

## Changes
- Specific change 1
- Specific change 2
- Specific change 3

## Testing
Describe testing performed

## Performance Impact
Benchmark results (if applicable)

## Breaking Changes
Any API or behavior changes?

## Checklist
- [ ] Tests pass locally
- [ ] Code style follows guidelines
- [ ] Documentation updated
- [ ] No console errors/warnings
```

### Review Process

1. **Automated Checks** (CI/CD)
   - ✅ Build passes
   - ✅ Tests pass
   - ✅ Code style checks
   - ✅ Security scan

2. **Manual Review**
   - Code quality
   - Design appropriateness
   - Performance implications
   - Documentation quality

3. **Feedback Loop**
   - Maintainer suggests changes
   - You implement feedback
   - Approval and merge

**Timeline:**
- Simple fix: 1-2 days
- Feature: 3-7 days
- Performance: 1-2 weeks
- Security: Immediate

---

## 🔄 Branch Naming

```
feature/feature-name
bugfix/bug-description
perf/optimization-name
doc/documentation-update
security/vulnerability-name
refactor/refactoring-name
```

**Examples:**
```
feature/generator-functions
bugfix/http-parser-crash
perf/route-caching-optimization
doc/deployment-guide-updates
security/null-byte-injection
```

---

## 📚 Documentation Guidelines

### Code Documentation

**Write for clarity:**
```typescript
// ❌ Unhelpful
function f(x: number): number {
  return x * x;
}

// ✅ Clear
function square(value: number): number {
  return value * value;
}
```

**Complex algorithms:**
```cpp
// ❌ No explanation
void quickSort(int* arr, int left, int right) {
    int pivot = partition(arr, left, right);
    // ...
}

// ✅ Explains why
// Uses quicksort with random pivot selection
// to avoid O(n²) worst case on sorted data
void quickSort(int* arr, int left, int right) {
    int pivot = randomPartition(arr, left, right);
    // ...
}
```

### File Documentation

```cpp
/**
 * HttpParser.h - Zero-copy HTTP request parsing
 * 
 * Parses HTTP/1.1 requests without intermediate
 * allocations, returning const char* views into
 * the original buffer. Achieves +20-30% throughput
 * improvement vs traditional string-based parsing.
 * 
 * @author Your Name
 * @version 1.0
 */
```

---

## 🐛 Bug Squashing Workflow

1. **Find a Bug**
   - Browse issues labeled `bug`
   - Or report your own

2. **Understand the Bug**
   - Reproduce consistently
   - Add test that fails
   - Understand root cause

3. **Fix the Bug**
   - Implement minimal fix
   - Don't refactor surrounding code
   - Add comment explaining fix

4. **Test the Fix**
   - Verify test passes
   - Test edge cases
   - Check for regressions

5. **Submit PR**
   - Reference the issue
   - Explain root cause
   - Show before/after

---

## 🚀 Feature Development Workflow

1. **Discuss First**
   - Open issue for feature
   - Discuss approach and design
   - Get feedback before coding

2. **Create Branch**
   ```bash
   git checkout -b feature/my-feature
   ```

3. **Develop**
   - Write tests first (TDD)
   - Implement feature
   - Ensure all tests pass

4. **Benchmark** (if performance-critical)
   - Measure before
   - Measure after
   - Document improvement

5. **Update Documentation**
   - README changes
   - Example code
   - API documentation

6. **Submit PR**
   - Link to original issue
   - Explain implementation
   - Show performance impact

---

## 📊 What We're Looking For

### Good Contributions

✅ **Small, focused PRs** — Single feature per PR  
✅ **Clear commit messages** — Explain the why  
✅ **Tests included** — All code changes tested  
✅ **Documentation updated** — Examples and guides  
✅ **Performance considered** — Benchmarks if needed  
✅ **Security reviewed** — Input validation, edge cases  

### Challenging Contributions

❌ **Giant PRs** — 1000+ line changes  
❌ **Refactoring** — Without functional changes  
❌ **Untested code** — No test coverage  
❌ **Breaking changes** — Without discussion  
❌ **Style-only PRs** — Just formatting  

---

## 🎓 Learning Resources

### Building
- [BUILD_PROCEDURES.md](BUILD_PROCEDURES.md) — Build system guide
- [BUILD_SYSTEM_README.md](BUILD_SYSTEM_README.md) — Automation guide

### Architecture
- [PROJECT_COMPLETION_SUMMARY.md](PROJECT_COMPLETION_SUMMARY.md) — Architecture overview
- [PRODUCTION_READINESS_AUDIT.md](PRODUCTION_READINESS_AUDIT.md) — Technical details

### GPU Programming
- [NVIDIA CUDA Programming Guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/)
- [NVRTC Documentation](https://docs.nvidia.com/cuda/nvrtc/index.html)

### Code Generation
- [LLVM IR Reference](https://llvm.org/docs/LangRef/)
- [CUDA C++ Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/)

---

## 🤝 Getting Help

- **Questions**: GitHub Discussions (coming soon)
- **Build Issues**: See [BUILD_PROCEDURES.md](BUILD_PROCEDURES.md)
- **Architecture**: See [PROJECT_COMPLETION_SUMMARY.md](PROJECT_COMPLETION_SUMMARY.md)
- **Deployment**: See [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md)

---

## 🎖️ Recognition

Contributors will be recognized in:
- Release notes
- CONTRIBUTORS.md file
- GitHub contributors page
- Project website (coming soon)

---

## 📜 License

By contributing, you agree that your contributions will be licensed under the MIT License. See [LICENSE.txt](LICENSE.txt).

---

## Final Notes

- **Be Patient**: Maintainers are volunteers
- **Be Respectful**: We're all learning
- **Be Helpful**: Assist other contributors
- **Have Fun**: Let's build something great together!

---

**Thank you for contributing to Symmetra! 🚀**

For questions, open an issue or reach out to the community.

Happy coding!

---

**Version**: 1.0  
**Last Updated**: 2026-05-25  
**Status**: Active
