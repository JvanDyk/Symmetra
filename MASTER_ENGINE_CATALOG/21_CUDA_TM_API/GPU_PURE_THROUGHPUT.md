# GPU_PURE_THROUGHPUT — CUDA TM API Engine
## Family: CUDA_TM_API
**Status:** ✅ Turing-Complete
**Platform:** NVIDIA RTX 5070 · CUDA (sm_89, 48 SMs)
**Source:** `symmca/CUDA_TM_API/cuda/gpu_pure_throughput_tester.cu`

---

## Performance

| Metric | Value |
|--------|-------|
| **Throughput** | Pure throughput |
| **GPU** | NVIDIA RTX 5070 (48 SMs, 9728 CUDA cores) |

---

## Description

CUDA pure throughput tester — maximum raw GPU bandwidth test

---

## Notes

Part of the symmca CUDA_TM_API family — GPU-accelerated REST API servers for
Turing machine / cellular automaton computation. These engines expose CA evolution
as HTTP endpoints, enabling cloud-scale distributed computation.
Best GPU single-thread: ~3.78 GH/s (CUDA); multi-SM: **193.57 GH/s** (RAYK_ULTRA_64_MULTI).
