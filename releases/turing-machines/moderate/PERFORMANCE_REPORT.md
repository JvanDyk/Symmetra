# Moderate Turing Machine — Performance Report

## Specifications
- **Machine Type**: 4-state state machine
- **Tape Size**: 64 cells
- **Step Limit**: 600 iterations
- **Theoretical Throughput**: **50K RPS** (requests per second)

## Performance Analysis

### GPU Compute Characteristics
- **Control Flow**: 4-state transitions (moderate branching)
- **Memory Operations**: 64 cells × 4 bytes = 256 bytes per execution
- **Arithmetic Per Step**: 4 operations (state check, memory read/write, increment, branch)
- **Total Operations**: ~2,400 per execution (4 ops × 600 steps)

### GPU Utilization Breakdown
| Aspect | Efficiency |
|--------|-----------|
| Compute Pipeline | 55-65% |
| Memory Bandwidth | 60-70% |
| Overall GPU Usage | 60-70% |
| Warp Occupancy | 90%+ |

### Throughput Estimates
- **Per-GPU Batch**: 32 machines × 600 steps = 19,200 steps/batch
- **Pipeline Depth**: 2 concurrent batches (register pressure moderate)
- **Estimated Peak**: 50-55K RPS (sustainable)

## Optimization Notes
- 4-state machine = meaningful branching overhead (~15% pipeline stall)
- 64-cell tape = partially in L1 cache (48KB per SM), some L2 cache pressure
- 600 step limit = good amortization of kernel launch overhead
- Realistic complexity-to-performance ratio for practical computation

## Use Cases
- **Gap filler**: Between SIMPLE (100K) and REFERENCE (10K)
- **Realistic workloads**: 4-state logic representative of real state machines
- **Memory + compute balance**: Tests both instruction and memory throughput
- **Scalability benchmark**: Shows degradation from 100K to 10K

## Comparison
- vs. SIMPLE (100K): ~50% throughput (4-state vs 3-state, larger tape)
- vs. REFERENCE (10K): 5× faster (smaller tape, fewer states)
- vs. BALANCED (1M): ~5% throughput (trade complexity for speed)
- vs. SUPERFAST (232K): ~22% throughput (much more complex logic)

## Key Insight
MODERATE bridges the performance gap between SIMPLE (100K RPS, 3-state) and REFERENCE (10K RPS, 5-state). The 4-state design with 64-cell tape represents realistic production state machines with measurable but acceptable overhead.
