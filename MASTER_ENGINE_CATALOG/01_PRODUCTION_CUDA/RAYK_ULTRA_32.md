# RAYK_ULTRA_32

**Rank/Status:** Production #5  
**Family:** 01_PRODUCTION_CUDA  
**Platform:** GPU RTX 5070 single thread  
**Turing-Complete:** YES  

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | **3.141 GH/s** |
| **Test Scale** | EXTREME 100B ops |

---

## Algorithm

Rule 60+90 chunked 32x 16+16

---

## Source File

`ENGINES/RAYK_ULTRA_32.cu`

---

## Notes

Chunked loading solves register pressure

---

*See [../../MASTER_ENGINE_CATALOG.md](../../MASTER_ENGINE_CATALOG.md) for full catalog.*
