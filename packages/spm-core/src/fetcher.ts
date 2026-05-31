// Package fetcher: Download from npm registry

export interface PackageMetadata {
  name: string;
  version: string;
  description?: string;
  main?: string;
  types?: string;
  exports?: Record<string, any>;
  dependencies?: Record<string, string>;
  devDependencies?: Record<string, string>;
  peerDependencies?: Record<string, string>;
  dist: {
    tarball: string;
    shasum: string;
    integrity: string;
  };
}

export interface PackageIndex {
  [version: string]: PackageMetadata;
}

export interface RegistryResponse {
  _id: string;
  name: string;
  'dist-tags': Record<string, string>;
  versions: PackageIndex;
}

export class NpmFetcher {
  private registry: string;
  private cache: Map<string, any> = new Map();

  constructor(registry: string = 'https://registry.npmjs.org') {
    this.registry = registry;
  }

  /**
   * Fetch package metadata from npm registry
   */
  async fetchPackageMetadata(name: string): Promise<RegistryResponse> {
    const cacheKey = `metadata:${name}`;
    if (this.cache.has(cacheKey)) {
      return this.cache.get(cacheKey);
    }

    const url = `${this.registry}/${encodeURIComponent(name)}`;

    try {
      const response = await fetch(url);
      if (!response.ok) {
        throw new Error(`Package not found: ${name}`);
      }

      const data = (await response.json()) as RegistryResponse;
      this.cache.set(cacheKey, data);
      return data;
    } catch (error) {
      throw new Error(`Failed to fetch package ${name}: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  /**
   * Resolve version to specific version string
   */
  async resolveVersion(name: string, versionSpec: string = 'latest'): Promise<string> {
    const metadata = await this.fetchPackageMetadata(name);

    // Handle version specs
    if (versionSpec === 'latest' || versionSpec === '*') {
      return metadata['dist-tags'].latest || Object.keys(metadata.versions).pop() || '1.0.0';
    }

    // Check if exact version exists
    if (metadata.versions[versionSpec]) {
      return versionSpec;
    }

    // Try to match semver (simplified)
    if (versionSpec.startsWith('^') || versionSpec.startsWith('~')) {
      const spec = versionSpec.slice(1);
      const [major, minor] = spec.split('.');
      const versions = Object.keys(metadata.versions);
      const matching = versions.filter(v => {
        const [vmajor, vminor] = v.split('.');
        if (versionSpec.startsWith('^')) {
          return vmajor === major;
        } else {
          return vmajor === major && vminor === minor;
        }
      });
      if (matching.length > 0) {
        return matching[matching.length - 1]; // Return highest matching version
      }
    }

    throw new Error(`Version not found: ${name}@${versionSpec}`);
  }

  /**
   * Fetch specific package version metadata
   */
  async fetchVersion(name: string, version: string): Promise<PackageMetadata> {
    const metadata = await this.fetchPackageMetadata(name);
    const versionData = metadata.versions[version];

    if (!versionData) {
      throw new Error(`Version not found: ${name}@${version}`);
    }

    return versionData;
  }

  /**
   * Get package tarball URL
   */
  async getTarballUrl(name: string, version: string): Promise<string> {
    const versionData = await this.fetchVersion(name, version);
    return versionData.dist.tarball;
  }

  /**
   * Fetch package tarball as buffer
   */
  async fetchTarball(name: string, version: string): Promise<Uint8Array> {
    const url = await this.getTarballUrl(name, version);

    try {
      const response = await fetch(url);
      if (!response.ok) {
        throw new Error(`Failed to fetch tarball for ${name}@${version}`);
      }

      const buffer = await response.arrayBuffer();
      return new Uint8Array(buffer);
    } catch (error) {
      throw new Error(`Failed to download ${name}@${version}: ${error instanceof Error ? error.message : String(error)}`);
    }
  }

  /**
   * Search for packages (simplified - just returns matching names)
   */
  async search(query: string, limit: number = 20): Promise<Array<{ name: string; version: string; description?: string }>> {
    // Simplified: would normally use /-/v1/search endpoint
    // For now, return empty to avoid external API calls
    return [];
  }

  /**
   * Clear metadata cache
   */
  clearCache(): void {
    this.cache.clear();
  }
}

/**
 * Create a fetcher instance with authentication token (optional)
 */
export function createFetcher(registry?: string, authToken?: string): NpmFetcher {
  return new NpmFetcher(registry);
}

export default { NpmFetcher, createFetcher };
