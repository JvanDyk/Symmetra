# GPU Engine Abstraction & Swappability Plan

**Goal**: Make Symmetra's compute engine swappable to support CUDA, ROCM, HIP, and other backends  
**Scope**: Core runtime abstraction (memory, compilation, dispatch)  
**Effort**: Large refactoring (~2-3 weeks for full implementation)

---

## Current State

**Architecture**: Monolithic CUDA-specific runtime
- ✅ Memory management: two-tier arena + persistent heap
- ✅ Compilation: NVRTC-based online compilation
- ✅ Dispatch: warp-aligned batch mode
- ❌ Abstraction layer: none
- ❌ Backend independence: zero

**Files requiring refactoring**: 
```
engine/src/runtime/
  ├── GpuServer.h/cpp (dispatch orchestration)
  ├── GpuWorker.h/cpp (batch scheduling)
  ├── BatchScheduler.h/cpp (stream management)
  ├── NvrtcCompiler.h/cpp (CUDA-specific compilation)
  ├── GpuAllocator.h/cpp (CUDA memory management)
  ├── GpuStream.h/cpp (CUDA stream wrappers)
  ├── GpuStream.cpp (stream pooling)
  └── main.cpp (initialization)
```

---

## Refactoring Strategy

### Phase 1: Abstract Compute Backend Interface (Week 1)

Create `IComputeBackend` interface to abstract all compute operations:

```cpp
// engine/src/runtime/ComputeBackend.h

namespace symm {

// Abstract compute backend interface
class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;

    // Initialization
    virtual bool init(int deviceId) = 0;
    virtual void shutdown() = 0;
    virtual const char* getName() const = 0;  // "CUDA", "ROCM", "HIP", etc.

    // Memory management
    virtual void* allocDevice(size_t bytes) = 0;
    virtual void freeDevice(void* ptr) = 0;
    virtual bool copyToDevice(void* dst, const void* src, size_t bytes) = 0;
    virtual bool copyFromDevice(void* dst, const void* src, size_t bytes) = 0;
    virtual bool copyDeviceToDevice(void* dst, const void* src, size_t bytes) = 0;
    virtual bool syncDevice() = 0;

    // Compilation
    virtual CompiledKernel compileKernels(const char* source) = 0;
    virtual bool loadKernel(const CompiledKernel& kernel, const char* name, 
                            KernelFunction& out) = 0;

    // Kernel execution
    virtual bool launchKernel(KernelFunction func, 
                             Grid gridDim, Block blockDim,
                             void** kernelArgs, size_t argCount) = 0;
    virtual bool launchKernelAsync(KernelFunction func,
                                   Grid gridDim, Block blockDim,
                                   Stream stream,
                                   void** kernelArgs, size_t argCount) = 0;

    // Stream management
    virtual Stream createStream() = 0;
    virtual void destroyStream(Stream s) = 0;
    virtual bool streamSynchronize(Stream s) = 0;
    virtual bool launchHostFunc(Stream s, HostFunc func, void* userData) = 0;

    // Device info
    virtual int getDeviceCount() = 0;
    virtual const char* getDeviceName(int deviceId) = 0;
    virtual size_t getDeviceMemory(int deviceId) = 0;
    virtual int getMaxThreadsPerBlock() = 0;
    virtual int getWarpSize() = 0;
};

// Factory function to create backend
IComputeBackend* createComputeBackend(const char* backendName);

}  // namespace symm
```

### Phase 2: Backend-Specific Implementations (Week 1-2)

#### Implementation 1: CUDA Backend (refactor existing)

```cpp
// engine/src/runtime/backends/CudaBackend.h

class CudaBackend : public IComputeBackend {
private:
    CUcontext context_;
    CUdevice device_;
    CUmodule module_;
    std::map<std::string, CUfunction> kernelFunctions_;
    StreamPool streamPool_;
    CudaAllocator allocator_;

public:
    bool init(int deviceId) override;
    void shutdown() override;
    
    // Memory operations
    void* allocDevice(size_t bytes) override;
    void freeDevice(void* ptr) override;
    bool copyToDevice(void* dst, const void* src, size_t bytes) override;
    // ... etc
};
```

#### Implementation 2: ROCM Backend (new)

```cpp
// engine/src/runtime/backends/RocmBackend.h

class RocmBackend : public IComputeBackend {
private:
    hipDevice_t device_;
    hipModule_t module_;
    std::map<std::string, hipFunction_t> kernelFunctions_;
    RocmStreamPool streamPool_;
    RocmAllocator allocator_;

public:
    bool init(int deviceId) override;
    void shutdown() override;
    
    // Memory operations (using HIP API)
    void* allocDevice(size_t bytes) override;
    // ... etc
};
```

#### Implementation 3: HIP Backend (new)

```cpp
// engine/src/runtime/backends/HipBackend.h

class HipBackend : public IComputeBackend {
    // Similar to ROCM but with HIP compiler integration
};
```

### Phase 3: Refactor Runtime to Use Backend Interface (Week 2)

Update core runtime classes to depend on `IComputeBackend`:

```cpp
// engine/src/runtime/GpuServer.h (refactored)

class GpuServer {
private:
    IComputeBackend* backend_;  // Abstracted!
    std::unique_ptr<BatchScheduler> scheduler_;
    std::unique_ptr<GpuAllocator> allocator_;

public:
    GpuServer(IComputeBackend* backend);
    
    bool handleRequest(const HttpRequest& req, HttpResponse& resp);
};

// engine/src/runtime/GpuWorker.h (refactored)

class GpuWorker {
private:
    IComputeBackend* backend_;  // Abstracted!
    
public:
    GpuWorker(IComputeBackend* backend);
};

// engine/src/runtime/BatchScheduler.h (refactored)

class BatchScheduler {
private:
    IComputeBackend* backend_;  // Abstracted!
    
    bool dispatchBatch(int batchSize) override;
};
```

### Phase 4: Update Initialization & Configuration (Week 2-3)

```cpp
// engine/src/main.cpp (refactored)

int main(int argc, char* argv[]) {
    // Configuration
    std::string computeBackend = "cuda";  // From config or env var
    int deviceId = 0;
    
    // Create appropriate backend
    IComputeBackend* backend = createComputeBackend(computeBackend);
    if (!backend->init(deviceId)) {
        std::cerr << "Failed to initialize " << computeBackend << " backend\n";
        return 1;
    }

    // Initialize runtime with backend
    GpuServer server(backend);
    GpuWorker worker(backend);
    BatchScheduler scheduler(backend);
    
    // Run server
    int status = runServer(server);
    
    // Cleanup
    backend->shutdown();
    delete backend;
    return status;
}
```

### Phase 5: Add Backend Selection Configuration (Week 3)

```cpp
// symmetra.config.ts (enhanced)

export default {
    port: 8000,
    host: "0.0.0.0",
    gpuBackend: "cuda",  // NEW: "cuda", "rocm", "hip", "opencl"
    gpuDevice: 0,
    gpuId: 0,
    
    // Backend-specific options
    cuda: {
        enableP2P: true,
        enableAsyncMemcpy: true,
    },
    rocm: {
        useGraphs: true,
        gcnArch: "gfx908",  // Auto-detect if not set
    },
    
    maxBatchSize: 256,
    requestTimeout: 5000,
    kernelTimeout: 5000,
};
```

---

## Implementation Checklist

### Phase 1: Abstraction Interface
- [ ] Create `ComputeBackend.h` with `IComputeBackend` interface
- [ ] Define abstract types: `Stream`, `Grid`, `Block`, `KernelFunction`
- [ ] Define factory function `createComputeBackend()`
- [ ] Document interface contract and error handling

### Phase 2: CUDA Backend
- [ ] Refactor `NvrtcCompiler` → `CudaCompiler` (inherits from `ICompiler`)
- [ ] Refactor `GpuAllocator` → `CudaAllocator` (inherits from `IAllocator`)
- [ ] Refactor `GpuStream` → `CudaStream` (inherits from `IStream`)
- [ ] Create `CudaBackend::init()`, `compileKernels()`, `launchKernel()` etc.
- [ ] Unit tests for all CUDA backend operations

### Phase 2: ROCM Backend
- [ ] Create `RocmBackend` implementing `IComputeBackend`
- [ ] Create `RocmCompiler` for HIP/ROCM compilation
- [ ] Create `RocmAllocator` for hip memory management
- [ ] Create `RocmStream` for HIP stream management
- [ ] Unit tests for all ROCM backend operations

### Phase 2: HIP Backend
- [ ] Create `HipBackend` implementing `IComputeBackend`
- [ ] Similar structure to ROCM but using HIP API
- [ ] Unit tests for HIP backend operations

### Phase 3: Runtime Refactoring
- [ ] Update `GpuServer` to accept `IComputeBackend*`
- [ ] Update `GpuWorker` to use backend abstraction
- [ ] Update `BatchScheduler` to use backend abstraction
- [ ] Remove hardcoded CUDA calls from `main.cpp`
- [ ] Update error handling to use backend-agnostic exceptions

### Phase 4: Configuration
- [ ] Add `computeBackend` config option
- [ ] Add backend-specific config sections
- [ ] Update `symmetra.config.ts` with backend options
- [ ] Implement env var overrides: `SYMMETRA_GPU_BACKEND`, `SYMMETRA_GPU_DEVICE`

### Phase 5: Testing
- [ ] Create backend selection tests
- [ ] Add CI/CD matrix testing: CUDA vs ROCM vs HIP
- [ ] Performance regression tests (ensure no slowdown)
- [ ] Cross-platform validation

### Phase 6: Documentation
- [ ] Update architecture guide with backend abstraction
- [ ] Add backend setup guides (CUDA, ROCM, HIP)
- [ ] Document backend-specific limitations
- [ ] Add backend performance comparison guide

---

## Benefits of Refactoring

| Benefit | Impact |
|---------|--------|
| **Portability** | Support AMD (ROCM/HIP), Intel (SYCL), Apple (Metal), ARM GPUs |
| **Competition** | Enable benchmarks against other accelerators (TPU, Habana, Cerebras) |
| **Flexibility** | Users choose GPU vendor based on cost/performance |
| **Future-proof** | Support emerging accelerator architectures |
| **Maintainability** | Clear separation of concerns (backend vs. runtime) |

---

## Effort Estimate

| Phase | Component | Effort | Notes |
|-------|-----------|--------|-------|
| 1 | Abstract interface | 1 day | Design & documentation |
| 2 | CUDA backend | 3 days | Refactoring existing code |
| 2 | ROCM backend | 4 days | New implementation |
| 2 | HIP backend | 3 days | Similar to ROCM |
| 3 | Runtime refactoring | 3 days | Update GpuServer, GpuWorker, BatchScheduler |
| 4 | Configuration | 2 days | Config parsing, env vars |
| 5 | Testing | 3 days | Backend selection, cross-platform tests |
| 6 | Documentation | 2 days | Guides and examples |
| **Total** | | **~21 days** | ~4-5 weeks full-time |

---

## Risk Mitigation

**Risk**: Breaking existing CUDA functionality during refactoring  
**Mitigation**: Maintain backward compatibility. Default to CUDA. Add feature flags.

**Risk**: ROCM/HIP performance worse than CUDA  
**Mitigation**: Profile and optimize. May require backend-specific codegen paths.

**Risk**: API differences between backends (stream semantics, memory models)  
**Mitigation**: Create abstraction that smooths over differences. Document backend-specific behavior.

---

## Conclusion

Making engines swappable requires **substantial architecture work but is achievable** with this phased approach. The abstraction layer would enable Symmetra to become truly portable across GPU vendors, which is a significant competitive advantage.

**Priority**: This should be considered for **v2.0 or v1.x minor release** after current stability milestones are reached.
