# spm-cli — Symmetra Package Manager

Official command-line interface for SPM (Symmetra Package Manager).

## Installation

```bash
npm install -g spm-cli
```

## Quick Start

```bash
# Add a package
spm add lodash

# Install dependencies
spm install

# List packages
spm list

# Get package info
spm info express

# Run diagnostics
spm doctor
```

## Commands

### `spm add <package>[@version]`
Add a package to your dependencies.

```bash
spm add lodash                    # Latest version
spm add express@4.18.0            # Specific version
spm add @babel/core               # Scoped packages
```

**What it does:**
1. Fetches package metadata from npm registry
2. Analyzes GPU compatibility (0-100 score)
3. Transforms Node.js imports to use @symmetra/compat-*
4. Validates compilation safety
5. Caches package locally
6. Updates spm.lock lockfile

### `spm install`
Install all dependencies from spm.lock.

```bash
spm install
```

Uses the locked versions in spm.lock to ensure reproducible builds.

### `spm remove <package>`
Remove a package from dependencies.

```bash
spm remove lodash
```

### `spm list`
List all installed packages with their versions.

```bash
spm list
```

### `spm search <query>`
Search npm registry for packages.

```bash
spm search http-server
spm search ui library
```

### `spm info <package>`
Show detailed information about a package.

```bash
spm info lodash
```

Displays:
- Latest version
- Description
- GPU compatibility score
- Dependencies

### `spm doctor`
Run diagnostics on your SPM installation.

```bash
spm doctor
```

Checks:
- npm registry connectivity
- Cache status
- System compatibility
- Version information

### `spm help`
Show help message.

```bash
spm help
spm help add         # Help for specific command
spm --help
```

## Configuration

### spm.json

Configure SPM behavior in your project:

```json
{
  "spm": {
    "registry": "https://registry.npmjs.com",
    "compatibility": "warn",
    "transforms": {
      "fs": "@symmetra/compat-fs",
      "crypto": "@symmetra/compat-crypto"
    },
    "incompatible": [
      "puppeteer",
      "fs-extra",
      "child_process"
    ]
  },
  "dependencies": {
    "lodash": "^4.17.21",
    "@symmetra/http": "^1.0.0"
  }
}
```

### .spmrc

Environment configuration:

```ini
registry=https://registry.npmjs.org
cache=.spm/cache
compatibility=warn
parallel=4
```

## Features

- ✅ **GPU-Compatible Packages** — Automatic transformation of npm packages
- ✅ **Reproducible Builds** — spm.lock ensures exact dependency versions
- ✅ **Compatibility Analysis** — Measures GPU suitability (0-100 score)
- ✅ **Smart Caching** — Local cache to avoid redundant downloads
- ✅ **Workspace Support** — Monorepo-friendly with pnpm-style lockfiles
- ✅ **Colored Output** — Beautiful, human-friendly CLI
- ✅ **Fast** — Parallel package downloads (configurable)

## How SPM Works

### 1. **Fetch**
Downloads package metadata and source from npm registry.

### 2. **Transform**
Rewrites Node.js imports to use GPU-compatible shims:
```javascript
// Input
import crypto from 'crypto';

// After transformation
import crypto from '@symmetra/compat-crypto';
```

### 3. **Analyze**
Checks GPU compatibility and generates a score:
- ✅ File I/O (compat-fs provides stubs)
- ✅ Cryptography (GPU-accelerated)
- ✅ Path operations (pure string manipulation)
- ❌ Child processes (not GPU-suitable)
- ❌ Network I/O (stubs provided)

### 4. **Cache**
Stores transformed packages locally in `.spm/cache/` with integrity verification.

### 5. **Lock**
Records exact versions in `spm.lock` for reproducible installs.

## Compatibility Modes

### Strict (`--strict`)
Fails on any incompatible code. Use for production GPU kernels.

```bash
spm add puppeteer --strict
# Error: puppeteer uses child_process, incompatible on GPU
```

### Warn (default)
Warns about incompatibilities but continues. Recommended.

```bash
spm add fs-extra
# ⚠ fs-extra uses heavy file I/O operations
# Performance may be degraded. Consider alternatives.
```

### Loose (`--loose`)
Silently proceeds even with major incompatibilities. Experimental.

```bash
spm add child_process --loose
```

## Examples

### Simple HTTP Server

```bash
spm add @symmetra/http
spm add lodash
spm add uuid
```

```typescript
import { serve, Router } from '@symmetra/http';
import _ from 'lodash';
import { v4 as uuidv4 } from 'uuid';

const app = new Router();

app.get('/:user', (ctx) => {
  ctx.json({
    id: uuidv4(),
    user: ctx.params.user,
    timestamp: _.now()
  });
});

serve(app, { port: 8080 });
```

### ML Pipeline

```bash
spm add lodash
spm add @symmetra/crypto
```

```typescript
import _ from 'lodash';
import { hash } from '@symmetra/crypto';

// Process data with lodash (CPU), hash with crypto (GPU)
const features = _.uniq(data).map(x => hash.sha256(x));
```

## Troubleshooting

### "Cannot reach npm registry"

```bash
# Check connectivity
spm doctor

# Try with custom registry
spm add lodash --registry https://registry.npmmirror.com
```

### "Package not found"

```bash
# Verify package exists on npm
spm info the-package-name

# Try different version
spm add package@1.0.0
```

### "Incompatible module"

View why a package is incompatible:

```bash
spm info lodash
# Shows GPU compatibility score and incompatibilities
```

## Performance

Typical installation times:
- **Small package** (lodash): ~500ms
- **Medium package** (express): ~1.5s  
- **Large package** (webpack): ~3s

With parallel mode (`--parallel 4`):
- **10 packages**: ~5s
- **50 packages**: ~20s

## Next Steps

- Run `spm doctor` to check your setup
- Run `spm help add` for detailed add command usage
- Check [SPM_USER_GUIDE.md](../../SPM_USER_GUIDE.md) for complete documentation
- View [SPM_COMPAT_REFERENCE.md](../../SPM_COMPAT_REFERENCE.md) for API compatibility

## License

MIT

---

For more information, visit [symmetra.dev](https://symmetra.dev)
