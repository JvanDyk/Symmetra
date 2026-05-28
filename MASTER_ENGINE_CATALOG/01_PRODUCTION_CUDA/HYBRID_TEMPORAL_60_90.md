# HYBRID_TEMPORAL_60_90

**Rank/Status:** Production #10  
**Family:** 01_PRODUCTION_CUDA  
**Platform:** GPU RTX 5070 single thread  
**Turing-Complete:** YES  

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | **2.298 GH/s** |
| **Test Scale** | EXTREME 100B ops |

---

## Algorithm

Rule 60 even gens Rule 90 odd gens 16x unroll

---

## Source File

`ENGINES/HYBRID_TEMPORAL_60_90.cu`

---

## Notes

BREAKTHROUGH 30.3% faster than Rule 110 Only 5% overhead vs non-TC Rule 60

---

*See [../../MASTER_ENGINE_CATALOG.md](../../MASTER_ENGINE_CATALOG.md) for full catalog.*
