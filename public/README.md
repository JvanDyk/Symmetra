# Symmetra Public Website

Professional, production-ready website for the Symmetra GPU computing platform.

## Overview

The Symmetra public website showcases TypeScript-to-GPU compilation with:

- **Comprehensive engine catalog** (10+ production-ready Turing machines)
- **Real-world benchmarks** (measured on actual hardware)
- **Clear performance tiers** (BASELINE → FAST → LIGHTNING → EXTREME)
- **Professional naming** (Docker-compatible `symmetra:tm-*` format)
- **Migration guide** (for updating to new standards)

## Key Pages

### index.html - Landing Page
Complete overview covering:
- Hero: 711K requests/sec, 1.4µs P99, zero GC
- Why Symmetra (GPU advantages)
- Architecture (8-stage compilation pipeline)
- Performance tiers and benchmarks
- Installation, code examples, FAQ, glossary

### engines.html - Engine Catalog
Interactive 4-tier showcase:
- **BASELINE**: 6.6M RPS avg (learning engines)
- **FAST**: 44.3M RPS avg (multi-threaded CPU)
- **LIGHTNING**: 64.7M RPS avg (GPU-accelerated)
- **EXTREME**: 228M RPS avg (multi-GPU)

### migration-guide.html - Migration Reference
Guide for transitioning to new naming:
- Old-to-new name mappings
- Code examples for all environments
- Backward compatibility timeline
- FAQ and support resources

## Naming Convention

### Format
```
symmetra:tm-{engine-name}:{version}
```

### Examples
```
symmetra:tm-rayk-ultra:1.0.0        (193M requests/sec)
symmetra:tm-bitwise-rule60:1.0.0    (86M requests/sec)
symmetra:tm-prism-x:1.0.0           (52M requests/sec)
symmetra:tm-rayk:1.0.0              (41M requests/sec baseline)
```

## What Changed

### New
✅ Standardized engine naming (Docker-compatible)  
✅ Interactive 4-tier catalog page  
✅ Comprehensive migration guide  
✅ Professional design with accessibility  
✅ Version management infrastructure  

### Removed
❌ Hardware-specific engine names (no `-gpu`, `-cpu`)  
❌ Platform references (no RTX 5070, Apple M4)  
❌ Confusing tier naming  

## Performance Highlights

| Metric | Value |
|--------|-------|
| Peak Throughput | 393M requests/sec |
| Single GPU | 193.57M requests/sec |
| P99 Latency | 5.2µs |
| GC Pauses | 0 (zero) |

## Browser Support

- Chrome/Edge 90+
- Firefox 88+
- Safari 14+
- Mobile (responsive)

## Structure

```
public/
├── index.html              # Landing page (~3400 lines)
├── engines.html            # Engine catalog (~350 lines)
├── migration-guide.html    # Migration guide (~450 lines)
├── install.sh             # Linux installer
├── install.ps1            # Windows installer
└── README.md              # This file
```

## Deployment

Static site - deploy to any CDN:

```bash
# AWS S3
aws s3 sync public/ s3://symmetra/

# Netlify
netlify deploy --prod --dir public

# Vercel
vercel --prod
```

## Version

**v1.0.0** - May 2026

See `ENGINE_NAMING_STANDARD.md` for complete technical reference.
