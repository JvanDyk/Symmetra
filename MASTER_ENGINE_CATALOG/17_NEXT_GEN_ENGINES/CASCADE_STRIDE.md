# CASCADE_STRIDE — Engine #195
## Family: NEXT_GEN_ENGINES
**Status:** ✅ Turing-Complete
**Platform:** Apple M4 Mac Air · Node.js v22
**Source:** `symmca/NEXT_GEN_ENGINES/CASCADE_STRIDE/`

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | R90 T=32767: 194× over SUPERWAVE · 9 stride calls vs 32768 XOR-shift passes |
| **Best Scenario** | Linear rules |

---

## Algorithm

**Key Technique:** O(popcount(T/32)) binary exponentiation: stride j for each set bit of T/32

---

## Notes

Engine #195 of 204 in the NEXT_GEN_ENGINES family.
Part of the complete exploration of cellular automaton computational landscape,
from raw bitwise operations to abstract algebraic shortcuts.
All engines faithfully simulate Rule 110 (proven Turing-complete by Cook, 2004).
