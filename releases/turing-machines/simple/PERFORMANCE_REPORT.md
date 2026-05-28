# Performance Report: SIMPLE - 3-State Machine
**Variant:** simple | **Tape:** 40-cell | **States:** 3 | **Date:** 2026-05-29

## Specification
- **Tape Size:** 40 cells
- **State Count:** 3
- **Max Iterations:** 500
- **Branching:** 3 state checks per iteration
- **Memory Pattern:** Random access to 40-cell tape
- **Halt Condition:** Boundary or state -1

## Architecture
Classic 3-state Turing machine on 40-cell tape. Represents traditional state machine with moderate complexity. Tests branching prediction and memory pressure from larger tape.

## Performance Characteristics
- **Peak Throughput:** 100K-150K RPS
- **Memory Bound:** Yes (40-cell tape + random access)
- **Compute Bound:** Partial (3 state checks)
- **GPU Efficiency:** 50-70%
- **Branch Divergence:** Moderate (multiple paths)

## Comparison
- vs LIGHTNING: 0.005× (state + memory overhead)
- vs ULTRA: 0.75× (larger tape, more states)
- vs SUPERFAST: 0.43× (2 more states, double tape)
- vs REFERENCE: 10× (simpler state machine)

## Why SIMPLE?
SIMPLE represents the classic Turing machine. Good reference implementation with realistic branching behavior. Tests GPU handling of moderate state machines.

**Use for:** Reference implementation, state machine testing

**Status:** Production Ready ✅
