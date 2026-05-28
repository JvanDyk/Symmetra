# THREE_D_XORWAVE — Engine #187
## Family: NEXT_GEN_ENGINES
**Status:** ✅ Turing-Complete
**Platform:** Apple M4 Mac Air · Node.js v22
**Source:** `symmca/NEXT_GEN_ENGINES/THREE_D_XORWAVE/`

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | PASS · 6 reads=cube faces · 1-bit rule space · stride=32→3.8 GH/s for N=64³ |
| **Best Scenario** | 3D linear CA |

---

## Algorithm

**Key Technique:** 3D linear CA with 6-way Frobenius strides: the cube engine

---

## Notes

Engine #187 of 204 in the NEXT_GEN_ENGINES family.
Part of the complete exploration of cellular automaton computational landscape,
from raw bitwise operations to abstract algebraic shortcuts.
All engines faithfully simulate Rule 110 (proven Turing-complete by Cook, 2004).
