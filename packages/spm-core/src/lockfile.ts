// spm.lock lockfile format and reader/writer

export interface LockfileEntry {
  version: string;
  resolved?: string;
  integrity?: string;
  dependencies?: Record<string, string>;
  bin?: Record<string, string>;
}

export interface Lockfile {
  version: number;
  timestamp: string;
  registry: string;
  compatibilityMode: 'strict' | 'warn' | 'loose';
  packages: Record<string, LockfileEntry>;
  depGraph: Record<string, string[]>;
}

/**
 * Parse spm.lock lockfile
 */
export function parseLockfile(content: string): Lockfile {
  try {
    // Handle YAML-like format
    const lockfile: any = {
      version: 1,
      timestamp: new Date().toISOString(),
      registry: 'https://registry.npmjs.org',
      compatibilityMode: 'warn',
      packages: {},
      depGraph: {}
    };

    // Simple YAML parser for spm.lock
    const lines = content.split('\n');
    let section = '';
    let currentPkg = '';

    for (const line of lines) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;

      if (line.startsWith('version:')) {
        lockfile.version = parseInt(line.split(':')[1].trim());
      } else if (line.startsWith('timestamp:')) {
        lockfile.timestamp = line.split(':').slice(1).join(':').trim();
      } else if (line.startsWith('registry:')) {
        lockfile.registry = line.split(':').slice(1).join(':').trim();
      } else if (line.startsWith('compatibility:')) {
        lockfile.compatibilityMode = line.split(':')[1].trim();
      } else if (line.startsWith('packages:')) {
        section = 'packages';
      } else if (line.startsWith('depGraph:')) {
        section = 'depGraph';
      } else if (section === 'packages' && trimmed.endsWith(':')) {
        currentPkg = trimmed.slice(0, -1);
        lockfile.packages[currentPkg] = {
          version: '1.0.0'
        };
      } else if (section === 'depGraph' && trimmed.endsWith(':')) {
        const key = trimmed.slice(0, -1);
        lockfile.depGraph[key] = [];
      }
    }

    return lockfile as Lockfile;
  } catch (error) {
    throw new Error(`Failed to parse lockfile: ${error instanceof Error ? error.message : String(error)}`);
  }
}

/**
 * Stringify lockfile to YAML-like format
 */
export function stringifyLockfile(lockfile: Lockfile): string {
  let yaml = `# spm.lock
version: ${lockfile.version}
timestamp: ${lockfile.timestamp}
registry: ${lockfile.registry}
compatibilityMode: ${lockfile.compatibilityMode}

packages:
`;

  for (const [name, entry] of Object.entries(lockfile.packages)) {
    yaml += `  ${name}:\n`;
    yaml += `    version: ${entry.version}\n`;
    if (entry.resolved) yaml += `    resolved: "${entry.resolved}"\n`;
    if (entry.integrity) yaml += `    integrity: "${entry.integrity}"\n`;
    if (entry.dependencies && Object.keys(entry.dependencies).length > 0) {
      yaml += `    dependencies:\n`;
      for (const [dep, ver] of Object.entries(entry.dependencies)) {
        yaml += `      ${dep}: "${ver}"\n`;
      }
    }
  }

  yaml += `\ndepGraph:\n`;
  for (const [name, deps] of Object.entries(lockfile.depGraph)) {
    yaml += `  ${name}:\n`;
    for (const dep of deps) {
      yaml += `    - ${dep}\n`;
    }
  }

  return yaml;
}

/**
 * Create a new lockfile
 */
export function createLockfile(
  packages: Record<string, LockfileEntry>,
  registry: string = 'https://registry.npmjs.org',
  compatibilityMode: 'strict' | 'warn' | 'loose' = 'warn'
): Lockfile {
  return {
    version: 1,
    timestamp: new Date().toISOString(),
    registry,
    compatibilityMode,
    packages,
    depGraph: {}
  };
}

/**
 * Merge lockfiles (for workspace support)
 */
export function mergeLockfiles(...lockfiles: Lockfile[]): Lockfile {
  const merged: Lockfile = {
    version: 1,
    timestamp: new Date().toISOString(),
    registry: lockfiles[0].registry,
    compatibilityMode: lockfiles[0].compatibilityMode,
    packages: {},
    depGraph: {}
  };

  for (const lockfile of lockfiles) {
    Object.assign(merged.packages, lockfile.packages);
    Object.assign(merged.depGraph, lockfile.depGraph);
  }

  return merged;
}

/**
 * Get dependencies from lockfile
 */
export function getDependencies(lockfile: Lockfile, packageName: string): Record<string, string> {
  const entry = lockfile.packages[packageName];
  return entry?.dependencies || {};
}

/**
 * Check if package is in lockfile
 */
export function hasPackage(lockfile: Lockfile, name: string, version?: string): boolean {
  const entry = lockfile.packages[name];
  if (!entry) return false;
  if (version && entry.version !== version) return false;
  return true;
}

/**
 * Validate lockfile integrity
 */
export function validateLockfile(lockfile: Lockfile): {
  valid: boolean;
  errors: string[];
} {
  const errors: string[] = [];

  if (!lockfile.version || lockfile.version < 1) {
    errors.push('Invalid lockfile version');
  }

  if (!lockfile.timestamp) {
    errors.push('Missing timestamp');
  }

  if (!lockfile.packages || Object.keys(lockfile.packages).length === 0) {
    errors.push('No packages found in lockfile');
  }

  return {
    valid: errors.length === 0,
    errors
  };
}

export default {
  parseLockfile,
  stringifyLockfile,
  createLockfile,
  mergeLockfiles,
  getDependencies,
  hasPackage,
  validateLockfile
};
