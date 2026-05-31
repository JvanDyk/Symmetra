// Main SPM core exports

export { transformPackageCode, analyzePackageCompatibility, validateTransformedCode, COMPAT_MAP, INCOMPATIBLE_MODULES, type TransformOptions, type TransformResult } from './transformer';
export { NpmFetcher, createFetcher, type PackageMetadata, type RegistryResponse } from './fetcher';
export { SpmCache, PersistentCache, createCache } from './cache';
export {
  parseLockfile,
  stringifyLockfile,
  createLockfile,
  mergeLockfiles,
  getDependencies,
  hasPackage,
  validateLockfile,
  type Lockfile,
  type LockfileEntry
} from './lockfile';
