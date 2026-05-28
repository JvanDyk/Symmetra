# RAYK_ABSOLUTE_128

**Rank/Status:** Vectorization Experiment  
**Family:** 05_RESEARCH_GPU  
**Platform:** GPU RTX 5070 single thread  
**Turing-Complete:** YES  

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | **3.277 GH/s BASELINE | 1.0 GH/s EXTREME** |
| **Test Scale** | BASELINE 1B ops |

---

## Algorithm

Rule 110 128-bit vectorization ulonglong2

---

## Source File

`TESTS/GPU/rayk_ultimate.cu lines 35-61`

---

## Notes

128-bit experiment slower at extreme due to boundary complexity

---

*See [../../MASTER_ENGINE_CATALOG.md](../../MASTER_ENGINE_CATALOG.md) for full catalog.*
