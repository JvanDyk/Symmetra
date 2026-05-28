# RAYK_PLASMA_16x

**Rank/Status:** Historical Origin Baseline  
**Family:** 05_RESEARCH_GPU  
**Platform:** GPU RTX 5070 single thread  
**Turing-Complete:** YES Cook 2004  

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | **3.353 GH/s BASELINE | 1.606 GH/s EXTREME | 0.975 GH/s fresh** |
| **Test Scale** | BASELINE 100K x 10K | EXTREME 5M x 20K |

---

## Algorithm

Rule 110 16x unroll + __ldg cache hints

---

## Source File

`TESTS/GPU/rayk_ultimate.cu lines 67-129`

---

## Notes

THE ORIGINAL BASELINE All engines measured against this

---

*See [../../MASTER_ENGINE_CATALOG.md](../../MASTER_ENGINE_CATALOG.md) for full catalog.*
