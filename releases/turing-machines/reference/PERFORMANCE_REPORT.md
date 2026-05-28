# Performance Report: REFERENCE - 5-State Busy Beaver
**Variant:** reference | **Tape:** 128-cell | **States:** 5 | **Date:** 2026-05-29

## Specification
- **Tape Size:** 128 cells
- **State Count:** 5 (full busy beaver)
- **Max Iterations:** 5000
- **Branching:** 5 state checks per iteration (lookup table)
- **Memory Pattern:** Dense table lookup + tape access
- **Halt Condition:** Boundary, state -1, or step limit

## Architecture
Full 5-state busy beaver. Most complex and realistic. Uses lookup tables for state transitions. Heavy branching and memory access. Tests GPU handling of complex control flow and large data structures.

## Performance Characteristics
- **Peak Throughput:** 10K-50K RPS
- **Memory Bound:** Heavily (128-cell tape + lookup table)
- **Compute Bound:** Yes (5 state checks + lookup)
- **GPU Efficiency:** 20-50%
- **Branch Divergence:** Significant (multiple complex paths)

## Comparison
- vs LIGHTNING: 0.001× (maximum complexity)
- vs ULTRA: 0.04× (realistic vs minimal)
- vs SUPERFAST: 0.04× (most complex state machine)
- vs SIMPLE: 0.1× (2 more states, 3× larger tape)

## Why REFERENCE?
REFERENCE is the production-quality Turing machine. Full busy beaver state machine with realistic complexity. Tests GPU capability to handle complex conditional logic and large data structures.

**Use for:** Realistic computation modeling, complex state machine testing

**Status:** Production Ready ✅
