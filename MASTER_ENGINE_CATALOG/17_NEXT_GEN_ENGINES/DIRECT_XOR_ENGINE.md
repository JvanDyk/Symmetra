# DIRECT_XOR_ENGINE — Engine #197
## Family: NEXT_GEN_ENGINES
**Status:** ✅ Turing-Complete
**Platform:** Apple M4 Mac Air · Node.js v22
**Source:** `symmca/NEXT_GEN_ENGINES/DIRECT_XOR_ENGINE/`

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | For T≤7: 1 pass vs 3 cascade calls · R90 T=3: 3.8× speedup · all 18 PASS |
| **Best Scenario** | Small T |

---

## Algorithm

**Key Technique:** Single-pass Lucas submask formula: state_T[i] = XOR of state_0[i±offsets] in ONE pass

---

## Notes

Engine #197 of 204 in the NEXT_GEN_ENGINES family.
Part of the complete exploration of cellular automaton computational landscape,
from raw bitwise operations to abstract algebraic shortcuts.
All engines faithfully simulate Rule 110 (proven Turing-complete by Cook, 2004).
