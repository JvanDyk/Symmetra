# Symmetra Package Manager (SPM) User Guide

## Table of Contents
1. [Installation](#installation)
2. [Quick Start](#quick-start)
3. [Commands](#commands)
4. [Configuration](#configuration)
5. [Workflows](#workflows)
6. [Troubleshooting](#troubleshooting)
7. [FAQ](#faq)

---

## Installation

### Prerequisites
- Symmetra v1.0.0+ (GPU compiler)
- Node.js 18+ (for development)
- npm (for dependency management)

### Install SPM CLI

```bash
# Via npm (when published)
npm install -g spm-cli

# Or use directly via npx
npx spm-cli@latest
```

### Verify Installation

```bash
spm --version      # Should print: SPM v1.0.0
spm doctor         # Check system setup
```

---

## Quick Start

### 1. Create a Project

```bash
mkdir my-gpu-app
cd my-gpu-app
```

### 2. Initialize spm.json

```bash
spm init   # Creates spm.json with defaults
```

Or manually create `spm.json`:

```json
{
  "name": "my-gpu-app",
  "version": "1.0.0",
  "dependencies": {},
  "spm": {
    "compatibility": "warn"
  }
}
```

### 3. Add npm Packages

```bash
# Add lodash (pure computation)
spm add lodash

# Add UUID generator
spm add uuid

# Add Symmetra HTTP server
spm add @symmetra/http
```

### 4. Write Code

Create `server.ts`:

```typescript
import { serve, Router } from '@symmetra/http';
import _ from 'lodash';
import { v4 as uuidv4 } from 'uuid';

const app = new Router();

app.get('/', (ctx) => {
  ctx.json({ 
    message: 'Hello GPU World!',
    id: uuidv4(),
    timestamp: _.now()
  });
});

serve(app, { port: 8080 });
```

### 5. Compile & Run

```bash
# Compile to GPU
symmetra build server.ts --with-spm

# Run on GPU
symmetra run dist/server.cuda
```

**No code changes needed!** SPM automatically transforms npm imports.

---

## Commands

### `spm install`

Install all dependencies from `spm.lock` (reproducible build).

```bash
spm install                  # Install all dependencies
spm install --force         # Force re-download
spm install --with-dev      # Include devDependencies
```

### `spm add <package>[@version]`

Add a new package to dependencies.

```bash
spm add lodash               # Latest version
spm add express@4.18.0      # Specific version
spm add @babel/core         # Scoped packages
spm add lodash@^4.17.0      # Semver versions

# Options
spm add lodash --save-dev   # Add to devDependencies
spm add lodash --exact      # Lock to exact version
```

**What happens:**
1. Fetches package metadata from npm
2. Analyzes GPU compatibility (0-100 score)
3. Transforms Node.js imports
4. Validates compilation safety
5. Caches package locally
6. Updates `spm.lock`

### `spm remove <package>`

Remove a package from dependencies.

```bash
spm remove lodash
spm remove @symmetra/http
```

### `spm update [package]`

Update packages to latest versions.

```bash
spm update                  # Update all packages
spm update lodash          # Update specific package
spm update --check         # Show available updates (dry-run)
```

### `spm list`

List installed packages with versions.

```bash
spm list                    # All packages
spm list --depth 0         # Top-level only
```

Output:
```
Installed packages (5):
  lodash@4.17.21 (score: 85/100)
  uuid@9.0.0 (score: 95/100)
  @symmetra/http@1.0.0 (local)
  @symmetra/crypto@1.0.0 (local)
  crypto-js@4.1.0 (score: 78/100)
```

### `spm info <package>`

Show detailed information about a package.

```bash
spm info lodash
spm info @symmetra/http
```

Output:
```
lodash@4.17.21

Description: JavaScript utility library
Latest: 4.17.21
Size: 149 KB
Dependencies: 0
GPU Score: 85/100 (Excellent)

GPU Compatibility:
  ✓ Pure computation (no I/O)
  ✓ Array operations optimizable
  ✓ Object utilities
  ⚠️ Some regexes (use sparingly)

Repository: https://github.com/lodash/lodash
License: MIT
```

### `spm search <query>`

Search npm registry for packages.

```bash
spm search "http server"
spm search math
spm search uuid
```

### `spm doctor`

Diagnose SPM installation and project setup.

```bash
spm doctor
```

Checks:
- ✓ npm registry connectivity
- ✓ Cache status
- ✓ Compiler integration
- ✓ Dependency conflicts
- ✓ GPU compatibility scores

### `spm cache clean`

Clear local package cache.

```bash
spm cache clean             # Clear all cache
spm cache clean lodash     # Clear specific package
spm cache status            # Show cache stats
```

### `spm help [command]`

Show help for a command.

```bash
spm help                    # Show all commands
spm help add               # Help for 'add' command
spm help --version         # Show version
```

---

## Configuration

### spm.json

Configure SPM behavior for your project.

```json
{
  "name": "my-gpu-app",
  "version": "1.0.0",
  "description": "GPU-accelerated application",
  "spm": {
    "registry": "https://registry.npmjs.org",
    "compatibility": "warn",
    "cache": ".spm/cache",
    "transforms": {
      "fs": "@symmetra/compat-fs",
      "crypto": "@symmetra/compat-crypto",
      "path": "@symmetra/compat-path"
    },
    "incompatible": [
      "puppeteer",      // Browser automation
      "fs-extra",       // Heavy file I/O
      "child_process"   // Process spawning
    ]
  },
  "dependencies": {
    "lodash": "^4.17.21",
    "uuid": "^9.0.0",
    "@symmetra/http": "^1.0.0"
  },
  "devDependencies": {
    "@symmetra/cli": "^1.0.0"
  },
  "scripts": {
    "dev": "symmetra ./src/server.ts --watch --with-spm",
    "build": "symmetra build ./src/server.ts --with-spm",
    "test": "symmetra test --with-spm",
    "deploy": "symmetra build && spm publish"
  }
}
```

### Compatibility Modes

**warn** (default): Warn about incompatibilities but continue
```json
{
  "spm": {
    "compatibility": "warn"
  }
}
```

**strict**: Fail on any incompatibility
```json
{
  "spm": {
    "compatibility": "strict"
  }
}
```

**loose**: Ignore incompatibilities (experimental)
```json
{
  "spm": {
    "compatibility": "loose"
  }
}
```

### spm.lock Format

Auto-generated file for reproducible installs. **Commit to version control.**

```yaml
version: 1
timestamp: 2026-05-31T12:00:00Z
registry: https://registry.npmjs.org
compatibility: warn

packages:
  lodash@4.17.21:
    resolved: https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz
    integrity: sha512-v2kDEe57lecTulaDIuNTPy3Ry4gLGuWOsem13Md2xvTm+l9eUwGpLMn9kRcZrgrQbGoP1ErMiI6mUCP5eBVPbA==
    dependencies: {}

  uuid@9.0.0:
    resolved: https://registry.npmjs.org/uuid/-/uuid-9.0.0.tgz
    integrity: sha512-MXcSTerfPa4uqyzStbRQiXvVIara1RxZYWo8TNiQmBLMsKqJ+0XELVlIcHM1t0h6eoKmlixOFE2YJg6g19gqmoA==
    dependencies: {}

depGraph:
  my-gpu-app:
    - lodash@4.17.21
    - uuid@9.0.0
    - @symmetra/http@1.0.0
```

---

## Workflows

### Single Project Setup

```bash
# 1. Create project
mkdir my-app && cd my-app

# 2. Initialize
spm init

# 3. Add dependencies
spm add lodash express uuid

# 4. Write code using npm packages
# (No API changes - use packages exactly as you would in Node.js)

# 5. Compile for GPU
symmetra build src/server.ts --with-spm

# 6. Run on GPU
symmetra run dist/server.cuda
```

### Monorepo Setup

```bash
# Root spm.json
{
  "workspaces": ["packages/*"],
  "dependencies": {
    "@symmetra/http": "^1.0.0"
  }
}

# packages/utils/spm.json
{
  "name": "@myorg/utils",
  "dependencies": {
    "lodash": "^4.17.21"
  }
}

# packages/server/spm.json
{
  "name": "@myorg/server",
  "dependencies": {
    "@myorg/utils": "*",
    "@symmetra/http": "^1.0.0"
  }
}
```

Then:

```bash
spm install              # Install all workspace deps
symmetra build packages/server --with-spm
```

### Development Workflow

```bash
# Watch mode (auto-recompile on change)
symmetra ./src/server.ts --watch --with-spm

# Run tests
spm test

# Check for updates
spm update --check
```

---

## Troubleshooting

### "Package not found"

```bash
# Verify package exists
spm info the-package-name

# Try alternative name
spm search similar-package
```

### "Incompatible module"

Some packages use Node.js APIs that don't make sense on GPU.

```bash
# Check compatibility score
spm info puppeteer
# Output: GPU Score: 15/100 (Incompatible - uses fs, network I/O, process spawning)

# Solution: Use CPU-suitable package or add compat module
```

### "Cache corruption"

```bash
# Clear and reinstall
spm cache clean
spm install --force
```

### "Compilation fails"

```bash
# Check doctor output
spm doctor

# Verify dependencies
spm list

# Check compiler logs
symmetra build src/server.ts --with-spm --verbose
```

---

## FAQ

### Q: Do I need to change my code?

**A:** No! Use npm packages exactly as you would in Node.js. SPM automatically transforms imports.

```typescript
// No changes needed:
import _ from 'lodash';
import crypto from 'crypto';

const unique = _.uniq([1, 2, 1, 3]);
const hash = crypto.createHash('sha256');
```

### Q: Which npm packages work?

**A:** Most do! Check the GPU compatibility score:
- **85-100** (Excellent): Pure computation (lodash, uuid, math)
- **60-84** (Good): Mostly compatible (express, axios)
- **30-59** (Fair): Some limitations (webpack, babel)
- **0-29** (Poor): Requires I/O (puppeteer, jsdom)

### Q: Can I use packages with I/O?

**A:** Partially. File I/O operations get stubbed with clear errors. Network calls work via @symmetra/http compatibility layer.

### Q: What about package updates?

**A:** spm.lock locks exact versions for reproducibility. Update with:

```bash
spm update lodash              # Update one package
spm update                     # Update all packages
spm update --check            # See available updates
```

### Q: Can I publish private packages?

**A:** Not yet. Currently, SPM fetches from public npm registry. Private registry support coming soon.

### Q: How do I debug GPU issues?

**A:** Enable verbose compilation:

```bash
symmetra build server.ts --with-spm --verbose
```

This shows:
- Import transformations
- Compatibility analysis
- Compilation details
- GPU kernel info

### Q: Performance vs Node.js?

**A:** GPU outperforms CPU for:
- Crypto operations (50-100x faster)
- Data processing (10-50x faster)
- Matrix operations (100x+ faster)

CPU outperforms GPU for:
- Small datasets (<1MB)
- I/O-bound operations
- Single-threaded tasks

### Q: How do monorepos work?

**A:** Define workspaces in root spm.json:

```json
{
  "workspaces": ["packages/*"]
}
```

Each workspace has its own spm.json. Dependencies are resolved across all workspaces.

---

## Getting Help

- **Command help**: `spm help <command>`
- **System diagnostics**: `spm doctor`
- **Package info**: `spm info <package>`
- **Report issues**: [github.com/JvanDyk/symmetra/issues](https://github.com/JvanDyk/symmetra/issues)

---

## Next Steps

- Read [SPM_COMPAT_REFERENCE.md](SPM_COMPAT_REFERENCE.md) for API compatibility details
- Check [examples/](examples/) for real-world projects
- See [SPM_PUBLISHING.md](SPM_PUBLISHING.md) to publish your own packages

**Happy GPU computing! 🚀**
