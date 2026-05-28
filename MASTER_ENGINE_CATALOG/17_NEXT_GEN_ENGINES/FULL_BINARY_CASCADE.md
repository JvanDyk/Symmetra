# FULL_BINARY_CASCADE — Engine #196
## Family: NEXT_GEN_ENGINES
**Status:** ✅ Turing-Complete
**Platform:** Apple M4 Mac Air · Node.js v22
**Source:** `symmca/NEXT_GEN_ENGINES/FULL_BINARY_CASCADE/`

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | popcount(T) calls (theoretical minimum) · R90 T=32767: 15 calls vs 72 · all 21 PASS |
| **Best Scenario** | Linear rules |

---

## Algorithm

**Key Technique:** Complete bit-level binary exponentiation: micro-strides j=0..4 + word-strides

---

## Notes

Engine #196 of 204 in the NEXT_GEN_ENGINES family.
Part of the complete exploration of cellular automaton computational landscape,
from raw bitwise operations to abstract algebraic shortcuts.
All engines faithfully simulate Rule 110 (proven Turing-complete by Cook, 2004).
