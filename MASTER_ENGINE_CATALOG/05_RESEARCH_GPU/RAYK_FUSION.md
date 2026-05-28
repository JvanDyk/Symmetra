# RAYK_FUSION

**Rank/Status:** Bitmask Variant  
**Family:** 05_RESEARCH_GPU  
**Platform:** GPU RTX 5070 single thread  
**Turing-Complete:** YES  

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | **2.133 GH/s BASELINE** |
| **Test Scale** | BASELINE 1B ops |

---

## Algorithm

Rule 110 power-of-2 bitmask + 8x unroll

---

## Source File

`TESTS/GPU/rayk_ultimate.cu lines 131-165`

---

## Notes

Bitmask wraparound faster than modulo

---

*See [../../MASTER_ENGINE_CATALOG.md](../../MASTER_ENGINE_CATALOG.md) for full catalog.*
