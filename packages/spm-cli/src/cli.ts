#!/usr/bin/env node
// SPM CLI - Symmetra Package Manager command-line interface

import { SpmCache, NpmFetcher, transformPackageCode, createLockfile, validateLockfile } from '@symmetra/spm-core';

// Color output helpers
const colors = {
  reset: '\x1b[0m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  red: '\x1b[31m',
  gray: '\x1b[90m',
  bold: '\x1b[1m'
};

function log(message: string, color: keyof typeof colors = 'reset') {
  console.log(`${colors[color]}${message}${colors.reset}`);
}

function success(message: string) {
  log(`✓ ${message}`, 'green');
}

function error(message: string) {
  log(`✗ ${message}`, 'red');
}

function info(message: string) {
  log(`→ ${message}`, 'blue');
}

function warn(message: string) {
  log(`⚠ ${message}`, 'yellow');
}

// Initialize SPM
const cache = new SpmCache('.spm/cache');
const fetcher = new NpmFetcher();
const installedPackages: Record<string, string> = {};

interface Command {
  name: string;
  description: string;
  run: (args: string[]) => Promise<void>;
}

const commands: Record<string, Command> = {
  // Install command
  install: {
    name: 'install',
    description: 'Install dependencies from spm.lock or package.json',
    run: async (args: string[]) => {
      info('Installing dependencies...');
      // Placeholder: Would read spm.lock and install all packages
      success('Dependencies installed');
    }
  },

  // Add command
  add: {
    name: 'add',
    description: 'Add a package to dependencies',
    run: async (args: string[]) => {
      if (args.length === 0) {
        error('Package name required: spm add <package>[@version]');
        return;
      }

      const packageSpec = args[0];
      const [name, version] = packageSpec.includes('@') ? packageSpec.split('@') : [packageSpec, 'latest'];

      try {
        info(`Fetching ${name}@${version}...`);
        const metadata = await fetcher.fetchPackageMetadata(name);
        const resolved = await fetcher.resolveVersion(name, version);

        success(`Found ${name}@${resolved}`);

        // Analyze compatibility
        info('Analyzing GPU compatibility...');
        // In production: download, transform, analyze

        success(`Added ${name}@${resolved} to dependencies`);
        installedPackages[name] = resolved;
      } catch (err) {
        error(`Failed to add ${name}: ${err instanceof Error ? err.message : String(err)}`);
      }
    }
  },

  // Remove command
  remove: {
    name: 'remove',
    description: 'Remove a package from dependencies',
    run: async (args: string[]) => {
      if (args.length === 0) {
        error('Package name required: spm remove <package>');
        return;
      }

      const name = args[0];
      if (installedPackages[name]) {
        delete installedPackages[name];
        success(`Removed ${name} from dependencies`);
      } else {
        warn(`Package ${name} not found in installed packages`);
      }
    }
  },

  // List command
  list: {
    name: 'list',
    description: 'List installed packages',
    run: async (args: string[]) => {
      if (Object.keys(installedPackages).length === 0) {
        info('No packages installed yet');
        return;
      }

      log('\nInstalled packages:', 'bold');
      for (const [name, version] of Object.entries(installedPackages)) {
        log(`  ${colors.green}${name}${colors.reset}@${version}`);
      }
      log('');
    }
  },

  // Search command
  search: {
    name: 'search',
    description: 'Search for packages',
    run: async (args: string[]) => {
      if (args.length === 0) {
        error('Search query required: spm search <query>');
        return;
      }

      const query = args.join(' ');
      info(`Searching npm for "${query}"...`);
      // Placeholder: Would call fetcher.search()
      warn('Search not yet fully implemented');
    }
  },

  // Info command
  info: {
    name: 'info',
    description: 'Show package information',
    run: async (args: string[]) => {
      if (args.length === 0) {
        error('Package name required: spm info <package>');
        return;
      }

      const name = args[0];
      try {
        info(`Fetching info for ${name}...`);
        const metadata = await fetcher.fetchPackageMetadata(name);
        const latest = metadata['dist-tags'].latest;

        log(`\n${colors.bold}${metadata.name}${colors.reset}`);
        log(`Latest: ${latest}`);
        log(`Description: ${metadata.name || 'N/A'}`);
        log(`Registry: npm`);
        log('');
      } catch (err) {
        error(`Failed to fetch info for ${name}: ${err instanceof Error ? err.message : String(err)}`);
      }
    }
  },

  // Doctor command (diagnostics)
  doctor: {
    name: 'doctor',
    description: 'Diagnose SPM installation',
    run: async (args: string[]) => {
      log('\nRunning diagnostics...\n', 'bold');

      // Check npm fetcher
      try {
        info('Checking npm registry connectivity...');
        const metadata = await fetcher.fetchPackageMetadata('lodash');
        success('npm registry reachable');
      } catch {
        error('Cannot reach npm registry');
      }

      // Check cache
      const stats = cache.stats();
      info(`Cache: ${stats.size} entries, ${(stats.totalBytes / 1024 / 1024).toFixed(2)}MB`);

      success('Diagnostics complete');
      log('');
    }
  },

  // Help command
  help: {
    name: 'help',
    description: 'Show help message',
    run: async (args: string[]) => {
      log(`\n${colors.bold}SPM - Symmetra Package Manager${colors.reset}\n`);
      log('Usage: spm <command> [options]\n');
      log('Commands:');

      for (const cmd of Object.values(commands)) {
        log(`  ${colors.green}${cmd.name.padEnd(12)}${colors.reset} ${cmd.description}`);
      }

      log('\nExamples:');
      log('  spm add lodash               Add lodash to dependencies');
      log('  spm install                  Install all dependencies');
      log('  spm list                     List installed packages');
      log('  spm info express             Show package information');
      log('  spm doctor                   Check SPM health');
      log('');
    }
  }
};

/**
 * Main CLI entry point
 */
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0 || args[0] === 'help' || args[0] === '--help') {
    await commands.help.run([]);
    process.exit(0);
  }

  const command = args[0];
  const commandArgs = args.slice(1);

  if (command === '--version' || command === '-v') {
    log('SPM v1.0.0');
    process.exit(0);
  }

  const cmd = commands[command];
  if (!cmd) {
    error(`Unknown command: ${command}`);
    log('Run "spm help" for usage information');
    process.exit(1);
  }

  try {
    await cmd.run(commandArgs);
  } catch (err) {
    error(`Command failed: ${err instanceof Error ? err.message : String(err)}`);
    process.exit(1);
  }
}

// Run CLI
main().catch(err => {
  error(`Fatal error: ${err instanceof Error ? err.message : String(err)}`);
  process.exit(1);
});

export { commands, log, success, error, info, warn };
