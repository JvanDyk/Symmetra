// Package transformer: Rewrite Node.js imports to use Symmetra compat layer

export interface TransformOptions {
  packageName: string;
  packageVersion: string;
  compatibilityMode?: 'strict' | 'warn' | 'loose';  // default: 'warn'
}

// Mapping of Node.js modules to Symmetra compat equivalents
const COMPAT_MAP: Record<string, string> = {
  'crypto': '@symmetra/compat-crypto',
  'path': '@symmetra/compat-path',
  'util': '@symmetra/compat-util',
  'events': '@symmetra/compat-events',
  'buffer': '@symmetra/compat-buffer',
  'stream': '@symmetra/compat-stream',
  'os': '@symmetra/compat-os',
  'url': '@symmetra/compat-url',
  'querystring': '@symmetra/compat-querystring',
  'assert': '@symmetra/compat-assert',
  'timers': '@symmetra/compat-timers',
  'process': '@symmetra/compat-process',
  'fs': '@symmetra/compat-fs',
};

// Modules that are explicitly incompatible with GPU
const INCOMPATIBLE_MODULES = new Set([
  'child_process',
  'cluster',
  'net',
  'http',
  'https',
  'tls',
  'dgram',
  'dns',
  'repl',
  'vm',
  'worker_threads',
  'perf_hooks',
  'inspector',
  'async_hooks',
  'v8'
]);

export interface TransformResult {
  code: string;
  warnings: string[];
  incompatibilities: string[];
  transformedImports: Record<string, string>;
}

/**
 * Transform npm package code by rewriting Node.js imports to compat layer
 */
export function transformPackageCode(code: string, options: TransformOptions): TransformResult {
  const warnings: string[] = [];
  const incompatibilities: string[] = [];
  const transformedImports: Record<string, string> = {};

  // Regex patterns for different import styles
  const importRegex = /import\s+(?:(?:\{[^}]*\})|(?:\*\s+as\s+\w+)|(?:\w+))\s+from\s+['"]([^'"]+)['"]/g;
  const requireRegex = /require\(['"]([^'"]+)['"]\)/g;
  const dynamicImportRegex = /import\s*\(['"]([^'"]+)['"]\)/g;

  let transformedCode = code;
  let lastIndex = 0;

  // Helper to transform a single import
  const transformImport = (moduleName: string): string => {
    // Handle subpath imports (e.g., 'lodash/flatten' -> 'lodash')
    const baseName = moduleName.split('/')[0];

    // Check if it's a scoped package (@org/package)
    if (baseName.startsWith('@')) {
      return moduleName;  // Leave scoped packages as-is
    }

    // Check for Node.js core modules
    if (COMPAT_MAP[baseName]) {
      const compatModule = COMPAT_MAP[baseName];
      transformedImports[moduleName] = compatModule;
      if (options.compatibilityMode !== 'loose') {
        warnings.push(`Transformed import: ${moduleName} → ${compatModule}`);
      }
      return compatModule;
    }

    // Check for incompatible modules
    if (INCOMPATIBLE_MODULES.has(baseName)) {
      incompatibilities.push(
        `Module '${baseName}' is not GPU-compatible. Operations will fail at runtime.`
      );
      if (options.compatibilityMode === 'strict') {
        throw new Error(`Incompatible module: ${baseName}`);
      } else if (options.compatibilityMode !== 'loose') {
        warnings.push(`⚠️  Incompatible module: ${baseName}`);
      }
    }

    return moduleName;  // Leave as-is (npm package or local import)
  };

  // Transform ES6 imports
  transformedCode = transformedCode.replace(importRegex, (match, moduleName) => {
    const transformed = transformImport(moduleName);
    return match.replace(moduleName, transformed);
  });

  // Transform CommonJS requires
  transformedCode = transformedCode.replace(requireRegex, (match, moduleName) => {
    const transformed = transformImport(moduleName);
    return match.replace(moduleName, transformed);
  });

  // Transform dynamic imports
  transformedCode = transformedCode.replace(dynamicImportRegex, (match, moduleName) => {
    const transformed = transformImport(moduleName);
    return match.replace(moduleName, transformed);
  });

  // Also check for string patterns in code (e.g., fs in template strings)
  for (const [nodeName, compatName] of Object.entries(COMPAT_MAP)) {
    // This is a heuristic - be careful not to replace string contents
    const regex = new RegExp(`(['"])${nodeName}\\1(?![a-zA-Z0-9_-])`, 'g');
    transformedCode = transformedCode.replace(regex, `'${compatName}'`);
  }

  return {
    code: transformedCode,
    warnings,
    incompatibilities,
    transformedImports
  };
}

/**
 * Analyze a package for GPU compatibility
 */
export function analyzePackageCompatibility(code: string): {
  score: number;  // 0-100
  issues: string[];
  recommendations: string[];
} {
  const issues: string[] = [];
  const recommendations: string[] = [];

  // Check for incompatible patterns
  if (/require\(['"]fs['"]\)/.test(code) || /import\s+.*\s+from\s+['"]fs['"]/.test(code)) {
    issues.push('Package uses file system (fs) operations');
    recommendations.push('Consider using memory buffers (@symmetra/compat-buffer) instead');
  }

  if (/require\(['"]net['"]\)/.test(code) || /require\(['"]http['"]\)/.test(code)) {
    issues.push('Package uses network operations (net/http)');
    recommendations.push('Network I/O not available on GPU');
  }

  if (/require\(['"]child_process['"]\)/.test(code)) {
    issues.push('Package uses child process spawning');
    recommendations.push('Process spawning not available on GPU');
  }

  if (/worker_threads|cluster|async_hooks/.test(code)) {
    issues.push('Package uses threading/clustering');
    recommendations.push('GPU compute is batched; manual threading not available');
  }

  // Check for heavy regex or parsing (potential hot path)
  if ((code.match(/for\s*\(/g) || []).length > 10) {
    recommendations.push('High loop count detected - consider GPU parallelization');
  }

  // Calculate score
  let score = 100;
  score -= Math.min(issues.length * 15, 50);  // -15 per issue, max -50

  return { score: Math.max(0, score), issues, recommendations };
}

/**
 * Validate that transformed code is likely to compile with Symmetra
 */
export function validateTransformedCode(code: string): {
  valid: boolean;
  errors: string[];
  warnings: string[];
} {
  const errors: string[] = [];
  const warnings: string[] = [];

  // Check for unsafe patterns
  if (/eval\s*\(/.test(code)) {
    errors.push('eval() is not supported on GPU');
  }

  if (/new\s+Function\s*\(/.test(code)) {
    errors.push('new Function() is not supported on GPU');
  }

  if (/with\s*\(/.test(code)) {
    warnings.push('with statement should be avoided');
  }

  if (/delete\s+\w+\[\w+\]/.test(code)) {
    warnings.push('delete operator may have unexpected behavior');
  }

  return {
    valid: errors.length === 0,
    errors,
    warnings
  };
}

export default {
  transformPackageCode,
  analyzePackageCompatibility,
  validateTransformedCode,
  COMPAT_MAP,
  INCOMPATIBLE_MODULES
};
