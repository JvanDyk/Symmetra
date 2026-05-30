# Turing Machine Engine Naming Standard

**Version**: 1.0  
**Date**: May 2026  
**Status**: Active

---

## Overview

This document establishes the standardized naming convention for all Symmetra Turing Machine (TM) engines. All engines are identified using a Docker-compatible format with semantic versioning. Internal platform details (GPU/CPU/Apple/hardware) are **never** exposed in public-facing names.

---

## Naming Format

```
symmetra:tm-{engine-family}-{variant}:{version}
```

### Rules
1. **Lowercase hyphenated identifiers** — Docker image compatible
2. **No platform suffixes** — Never include `-gpu`, `-cpu`, `-apple`, `-m4`, `-multi-sm`, etc.
3. **No hardware vendor names** — No NVIDIA, Intel, Apple, etc.
4. **No tier names** — Never include `-ultra`, `-lightning`, `-extreme`, `-baseline`
5. **Semantic versioning** — `major.minor.patch` (e.g., `1.0.0`, `1.1.0`, `2.0.0`)
6. **Family name first** — Identifies the algorithm family
7. **Variant is optional** — Only when distinguishing algorithm variants

---

## Naming Convention Examples

### Core Engines

| Display Name | Docker Image | Old Internal Name | Family | Tier |
|---|---|---|---|---|
| RAYK Ultra | `symmetra:tm-rayk-ultra:1.0.0` | RAYK_ULTRA_64_MULTI | RAYK | EXTREME |
| RAYK | `symmetra:tm-rayk:1.0.0` | RAYK_CPU | RAYK | BASELINE |
| Bitwise Rule 60 | `symmetra:tm-bitwise-rule60:1.0.0` | BITWISE_RULE60_GPU | Bitwise | LIGHTNING |
| Bitwise Rule 90 | `symmetra:tm-bitwise-rule90:1.0.0` | BITWISE_RULE90_GPU | Bitwise | LIGHTNING |
| Bitwise Rule 110 | `symmetra:tm-bitwise-rule110:1.0.0` | BITWISE_RULE110_GPU | Bitwise | LIGHTNING |
| Singularity | `symmetra:tm-singularity:1.0.0` | SINGULARITY (GPU/Systolic) | Singularity | LIGHTNING |
| Prism X | `symmetra:tm-prism-x:1.0.0` | PRISM_X (Apple M4) | Prism | FAST |
| Chronos | `symmetra:tm-chronos:1.0.0` | CHRONOS | Chronos | EXTREME |

### Production CUDA Family (01_PRODUCTION_CUDA)

| Display Name | Docker Image | Algorithm | Tier |
|---|---|---|---|
| RAYK Pure 60 | `symmetra:tm-rayk-pure-60:1.0.0` | Single-thread Rule 60 | BASELINE |
| RAYK Adaptive | `symmetra:tm-rayk-adaptive:1.0.0` | Adaptive batching | FAST |
| RAYK Omega | `symmetra:tm-rayk-omega:1.0.0` | Optimized streaming | FAST |
| RAYK Ultra | `symmetra:tm-rayk-ultra:1.0.0` | Multi-SM 64-zero-branch | EXTREME |

### Bitwise Engine Family (02_BITWISE_ENGINES)

| Display Name | Docker Image | Algorithm | Tier |
|---|---|---|---|
| Bitwise Rule 60 | `symmetra:tm-bitwise-rule60:1.0.0` | 32-bit packed Rule 60 | LIGHTNING |
| Bitwise Rule 90 | `symmetra:tm-bitwise-rule90:1.0.0` | 32-bit packed Rule 90 | LIGHTNING |
| Bitwise Rule 110 | `symmetra:tm-bitwise-rule110:1.0.0` | 32-bit packed Rule 110 | LIGHTNING |

### CPU Engine Family (18_CPU_ENGINES)

| Display Name | Docker Image | Algorithm | Tier |
|---|---|---|---|
| RAYK | `symmetra:tm-rayk:1.0.0` | Rule 60 baseline | BASELINE |
| RAYK NextGen | `symmetra:tm-rayk-nextgen:1.0.0` | Optimized CPU variant | FAST |
| RAYK Omega | `symmetra:tm-rayk-omega:1.0.0` | Multi-threaded variant | FAST |
| Prism | `symmetra:tm-prism:1.0.0` | CPU-optimized variant | FAST |
| Prism X | `symmetra:tm-prism-x:1.0.0` | Champion variant (204 family) | FAST |
| Zenith | `symmetra:tm-zenith:1.0.0` | Peak CPU performance | FAST |

---

## Metadata Tags (Not in Name)

Every engine has the following metadata attributes (separate from the identifier):

```json
{
  "identifier": "symmetra:tm-rayk-ultra:1.0.0",
  "displayName": "RAYK Ultra",
  "family": "RAYK",
  "performanceTier": "extreme",
  "algorithmType": "cellular-automaton",
  "algorithmRule": "60",
  "strategy": "64-zero-branch-multi-sm",
  "status": "stable",
  "supportedPlatforms": ["cuda", "multi-gpu"],
  "baselineVersion": "1.0.0",
  "releaseDate": "2026-05-30",
  "throughput": "193.57M requests/sec",
  "latencyP99": "5.2µs"
}
```

### Metadata Fields

| Field | Values | Purpose |
|---|---|---|
| `displayName` | String | User-friendly engine name |
| `performanceTier` | `baseline`, `fast`, `lightning`, `extreme` | Performance category |
| `algorithmType` | `cellular-automaton`, `hybrid`, `systolic`, `cosmic`, etc. | Algorithm family type |
| `algorithmRule` | `60`, `90`, `110`, `hybrid`, `gf2`, etc. | Specific algorithm variant |
| `status` | `stable`, `preview`, `deprecated` | Engine stability |
| `supportedPlatforms` | `cuda`, `cpu`, `multi-gpu`, etc. | Where engine runs |
| `throughput` | String (e.g., "193.57M requests/sec") | Peak throughput from last benchmark |
| `latencyP99` | String (e.g., "5.2µs") | 99th percentile latency |

---

## Usage Examples

### Installation

```bash
# Install an engine
symmetra install tm-rayk-ultra

# Install specific version
symmetra install tm-rayk-ultra:1.0.0

# List all available engines
symmetra engine list
```

### Docker

```bash
# Pull engine image
docker pull symmetra:tm-rayk-ultra:1.0.0

# Run with specific engine
docker run -e SYMMETRA_ENGINE=tm-rayk-ultra:1.0.0 symmetra:latest

# Multi-engine deployment
docker run -e SYMMETRA_ENGINES=tm-rayk-ultra:1.0.0,tm-bitwise-rule60:1.0.0 symmetra:latest
```

### Configuration

```yaml
# In symmetra.config.yml
engine:
  primary: symmetra:tm-rayk-ultra:1.0.0
  fallback: symmetra:tm-bitwise-rule60:1.0.0
  
engineVersions:
  - symmetra:tm-rayk-ultra:1.0.0      # Pinned to stable
  - symmetra:tm-chronos:1.0.0         # Production variant
```

---

## Performance Tiers

### Tier 1: BASELINE (6.6M RPS average)
Single-threaded learning engines for development and testing.

**Engines**:
- `symmetra:tm-rayk-pure-60:1.0.0`
- `symmetra:tm-recursive-xor:1.0.0`
- `symmetra:tm-seed-collapse:1.0.0`

### Tier 2: FAST (44.3M RPS average)
Multi-threaded CPU engines for standard production workloads.

**Engines**:
- `symmetra:tm-prism-cpu:1.0.0`
- `symmetra:tm-nexus:1.0.0`
- `symmetra:tm-rayk-omega:1.0.0`
- `symmetra:tm-rayk-nextgen:1.0.0`

### Tier 3: LIGHTNING (64.7M RPS average)
GPU-accelerated and specialized engines for high-performance APIs.

**Engines**:
- `symmetra:tm-bitwise-rule60:1.0.0`
- `symmetra:tm-bitwise-rule90:1.0.0`
- `symmetra:tm-singularity:1.0.0`
- `symmetra:tm-hybrid-symmetry:1.0.0`

### Tier 4: EXTREME (228M RPS average)
Multi-GPU and peak-performance engines for hyperscale deployments.

**Engines**:
- `symmetra:tm-rayk-ultra:1.0.0`
- `symmetra:tm-chronos:1.0.0`
- `symmetra:tm-symmetry-universal:1.0.0`

---

## Version Management

### Versioning Strategy
- **1.0.0**: Initial release (all engines start here)
- **1.1.0**: Performance optimization (no breaking changes)
- **1.2.0**: New feature (algorithm enhancement, no breaking changes)
- **2.0.0**: Breaking change (different output format, interface change)

### Release Cycle
- **Patch releases** (1.0.0 → 1.0.1): Bug fixes, security patches (weekly)
- **Minor releases** (1.0.0 → 1.1.0): Performance improvements (monthly)
- **Major releases** (1.0.0 → 2.0.0): Algorithm changes (quarterly or as needed)

### Deprecation Policy
- Old versions remain available for 6 months after major release
- Deprecated engines marked with `status: deprecated`
- Migration guides provided for breaking changes

---

## Migration from Old Naming

### Old Format → New Format

**Old**: `RAYK_ULTRA_64_MULTI` (GPU)  
**New**: `symmetra:tm-rayk-ultra:1.0.0`  
**Display**: "RAYK Ultra"

**Old**: `BITWISE_RULE60_GPU`  
**New**: `symmetra:tm-bitwise-rule60:1.0.0`  
**Display**: "Bitwise Rule 60"

**Old**: `RAYK_CPU SINGLE-THREAD`  
**New**: `symmetra:tm-rayk:1.0.0`  
**Display**: "RAYK"

**Old**: `PRISM_X (Apple M4)`  
**New**: `symmetra:tm-prism-x:1.0.0`  
**Display**: "Prism X"

### Backward Compatibility
- Old internal names remain in source code (never exposed to users)
- All public APIs use new naming only
- Configuration files support both old and new names (with warnings)
- No user-facing documentation uses old naming

---

## Implementation Checklist

- [ ] All engine cards display new names only
- [ ] Configuration files support new identifiers
- [ ] Installation commands use new format
- [ ] CLI help text updated
- [ ] Docker image tags follow convention
- [ ] API endpoints return new identifiers
- [ ] Database schema updated (if applicable)
- [ ] Documentation migrated
- [ ] Migration guide published for users
- [ ] QA verification of all 351 engines

---

## Questions & Support

For questions about engine naming or migration:
- GitHub: [github.com/JvanDyk/symmetra/discussions](https://github.com/JvanDyk/symmetra/discussions)
- Docs: [symmetra.io/docs/engine-catalog](https://symmetra.io/docs/engine-catalog)
- Email: support@symmetra.io
