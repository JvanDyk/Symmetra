cmake_minimum_required(VERSION 3.20)
project(symmetra LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

# ── Global compiler options (must be set before add_executable) ─────────────────
if(MSVC)
    # Disable /WX (treat warnings as errors) to allow C2026 string literal issues
    string(REGEX REPLACE " /WX( |$)" " " CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/wd2026 /wd4430)  # Suppress C2026 and C4430 warnings
    message(STATUS "MSVC: Disabled /WX and suppressed C2026, C4430")
endif()

# ── CUDA ──────────────────────────────────────────────────────────────────────
# CUDA Version Flexibility: Supports CUDA 12.x and 13.x
# If VS2022 reports CUDA props missing, props file will use whatever version is installed
find_package(CUDAToolkit REQUIRED)
# CMAKE_CUDA_ARCHITECTURES only affects host-side compilation; kernels ship as
# encrypted PTX (virtual arch compute_75) and are JITed by the driver at load.
set(CMAKE_CUDA_ARCHITECTURES 75 CACHE STRING "CUDA architectures")

# ── OpenSSL (TLS 1.3 Production Support) ───────────────────────────────────────
# For battle-tested TLS implementation (replaces custom MVP-grade crypto)
# Note: If OpenSSL not found, TLS features will be disabled but build succeeds
find_package(OpenSSL)
if(NOT OPENSSL_FOUND)
    message(STATUS "OpenSSL not found - TLS will use fallback (custom crypto)")
    message(STATUS "To enable production TLS: set OPENSSL_ROOT_DIR environment variable")
endif()

# ── Protection build options ──────────────────────────────────────────────────

# Change these per release. Keep SYMM_PTX_KEY and SYMM_OBFKEY private.
# Generate with: python -c "import secrets; print(secrets.token_hex(32))"
set(SYMM_PTX_KEY_BYTES
    "0x53,0x59,0x4d,0x4d,0x65,0x74,0x72,0x61,0x50,0x72,0x6f,0x74,0x65,0x63,0x74,0x69,\
0x6f,0x6e,0x4b,0x65,0x79,0x32,0x30,0x32,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00"
    CACHE STRING "AES-256 key for PTX encryption (32 bytes as 0xNN,... list)")

set(SYMM_PTX_IV_BYTES
    "0x53,0x59,0x4d,0x4d,0x49,0x56,0x30,0x30,0x30,0x31,0x00,0x00"
    CACHE STRING "AES IV for PTX encryption (12 bytes as 0xNN,... list)")

set(SYMM_OBFKEY_VAL "0xA7C3E591B2D04F68ULL"
    CACHE STRING "Compile-time XOR key for string obfuscation")

# ── Sources ───────────────────────────────────────────────────────────────────

set(ENGINE_SOURCES
    src/main.cpp
    src/parser/EsbuildBridge.cpp
    src/parser/SymmParser.cpp
    src/compiler/Codegen.cpp
    src/compiler/PreludeData.cpp  # GPU runtime prelude (split to avoid C2026 limit)
    # Runtime — scheduling, memory, and workers
    src/runtime/NvrtcCompiler.cpp
    src/runtime/GpuServer.cpp
    src/runtime/GpuAllocator.cpp
    src/runtime/WarpScheduler.cpp
    src/runtime/StreamPool.cpp
    src/runtime/ThreadPool.cpp
    src/runtime/GpuWorker.cpp
    src/runtime/BatchScheduler.cpp  # Batch dispatch scheduling
    src/runtime/AsyncBatchLauncher.cpp  # Non-blocking async batch launching
    src/runtime/DebugRingBuffer.cpp  # GPU debug logging
    src/runtime/DateNow.cpp  # Timestamp support
    # High-priority runtime features
    src/runtime/TimerQueue.cpp
    src/runtime/AbortSignal.cpp
    src/runtime/UrlParser.cpp
    src/runtime/GpuStream.cpp
    # Compute backend abstraction layer
    src/runtime/ComputeBackendFactory.cpp
    src/runtime/backends/CudaBackend.cpp
    # Protection
    src/protect/KernelStore.cpp
    src/protect/PtxCrypt.cpp
    src/protect/AntiDebug.cpp
    src/protect/SymmVm.cpp
)

# TLS: OpenSSL 1.3 integration (conditional on OpenSSL availability)
if(OPENSSL_FOUND)
    list(APPEND ENGINE_SOURCES src/runtime/TlsOpenSSL.cpp)
endif()

# CUDA kernel sources are NOT compiled into the binary.
# They are pre-compiled to PTX, AES-256 encrypted, and embedded as byte arrays.
# At runtime they are decrypted and loaded via cuModuleLoadData — no plaintext
# PTX ever touches disk or lives in the binary (cuobjdump finds nothing).

# ── Build-time PTX encryptor tool ─────────────────────────────────────────────

add_executable(encrypt_ptx tools/encrypt_ptx.cpp src/protect/PtxCrypt.cpp)
target_include_directories(encrypt_ptx PRIVATE src/)
target_compile_definitions(encrypt_ptx PRIVATE
    "SYMM_PTX_KEY=${SYMM_PTX_KEY_BYTES}"
    "SYMM_PTX_IV=${SYMM_PTX_IV_BYTES}"
)

# ── Encrypted kernel generation ───────────────────────────────────────────────
# For each .cu kernel:
#   1. nvcc -ptx   → virtual arch compute_75 PTX (portable, driver JITs on load)
#   2. encrypt_ptx → AES-256-CTR encrypted C++ header
# The headers land in ${CMAKE_BINARY_DIR}/generated/ and are #included by
# KernelStore.cpp at compile time.

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

# Use virtual architecture so the PTX runs on any sm_75+ device (sm_120 included).
set(CUDA_PTX_VIRTARCH "compute_75")

# Helper macro: compile one .cu to encrypted header
macro(encrypt_cuda_kernel SRC_REL OUT_STEM ARRAY_NAME)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${SRC_REL}")
    set(_ptx "${GENERATED_DIR}/${OUT_STEM}.ptx")
    set(_hdr "${GENERATED_DIR}/${OUT_STEM}_enc.h")

    add_custom_command(
        OUTPUT "${_ptx}"
        COMMAND "${CMAKE_CUDA_COMPILER}"
            -ptx
            "--generate-code=arch=${CUDA_PTX_VIRTARCH},code=${CUDA_PTX_VIRTARCH}"
            --std=c++17
            "-I${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${_src}"
            -o "${_ptx}"
        DEPENDS "${_src}"
        COMMENT "nvcc -ptx ${OUT_STEM}.cu"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${_hdr}"
        COMMAND "$<TARGET_FILE:encrypt_ptx>" "${_ptx}" "${_hdr}" "${ARRAY_NAME}"
        DEPENDS encrypt_ptx "${_ptx}"
        COMMENT "encrypt ${OUT_STEM}.ptx → ${OUT_STEM}_enc.h"
        VERBATIM
    )

    list(APPEND ENCRYPTED_KERNEL_HDRS "${_hdr}")
endmacro()

encrypt_cuda_kernel(src/kernels/genesis.cu      genesis  SYMM_GENESIS_PTX)
encrypt_cuda_kernel(src/runtime/HttpKernels.cu  http     SYMM_HTTP_PTX)
encrypt_cuda_kernel(src/runtime/IoKernel.cu     io       SYMM_IO_PTX)
encrypt_cuda_kernel(src/kernels/TextKernel.cu   text     SYMM_TEXT_PTX)

add_custom_target(gen_encrypted_kernels DEPENDS ${ENCRYPTED_KERNEL_HDRS})

# ── Main executable ───────────────────────────────────────────────────────────

add_executable(symmetra ${ENGINE_SOURCES})
add_dependencies(symmetra gen_encrypted_kernels)

target_include_directories(symmetra PRIVATE
    src/
    ${CUDAToolkit_INCLUDE_DIRS}
    ${GENERATED_DIR}    # for genesis_enc.h, http_enc.h, io_enc.h
)

target_link_libraries(symmetra PRIVATE
    CUDA::cuda_driver
    CUDA::nvrtc
    CUDA::cudart
)

# Conditionally link OpenSSL if found
if(OPENSSL_FOUND)
    target_link_libraries(symmetra PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
    )
endif()

if(MSVC)
    target_link_libraries(symmetra PRIVATE ws2_32)
    # Note: Codegen.cpp has a 81KB PRELUDE that exceeds MSVC's string literal limit.
    # Use /Zm to increase the compiler's memory (helps with large string literals)
    # and suppress the C2026 warning (the string is necessary for GPU code generation)
    target_compile_options(symmetra PRIVATE
        /Zm400                # Increase compiler memory to 400% to handle large strings
        /wd2026              # C2026: string too big (unavoidable in Codegen.cpp for GPU prelude)
        /wd4430              # C4430: missing type specifier
        /W3                  # Warning level 3
    )
    # Remove /WX (treat warnings as errors) if present in default flags
    string(REPLACE "/WX " "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/WX" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    # For Codegen.cpp specifically, disable warning-as-error
    set_source_files_properties(
        src/compiler/Codegen.cpp
        PROPERTIES
        COMPILE_FLAGS "/wd2026"
    )
endif()

# ── Compile definitions (protection keys) ────────────────────────────────────

# SYMM_PTX_KEY / SYMM_PTX_IV are NOT passed as defines — they contain commas which
# nvcc on Windows misparses as multiple input files. PtxCrypt.h has #ifndef fallbacks
# with the same default values. Override per-release by editing PtxCrypt.h directly.
target_compile_definitions(symmetra PRIVATE
    "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}"
)

# ── LLVM / Clang obfuscation (Layer 1) ───────────────────────────────────────
option(SYMM_OBFUSCATE "Enable LLVM obfuscation passes (requires Clang + Hikari)" ON)

if(SYMM_OBFUSCATE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(symmetra PRIVATE
        -mllvm -fla
        -mllvm -sub
        -mllvm -bcf
        -mllvm -sub_loop=3
        -fstack-protector-all
        -fcf-protection=full
        -D_FORTIFY_SOURCE=2
    )
    message(STATUS "LLVM obfuscation: ENABLED (fla + sub + bcf)")
elseif(SYMM_OBFUSCATE)
    message(WARNING "SYMM_OBFUSCATE requires Clang. Current compiler: ${CMAKE_CXX_COMPILER_ID} (obfuscation disabled)")
    message(STATUS "LLVM obfuscation: OFF (current compiler: ${CMAKE_CXX_COMPILER_ID})")
else()
    message(STATUS "LLVM obfuscation: OFF (pass -DSYMM_OBFUSCATE=ON with Clang to enable)")
endif()

# Strip all symbols from Release binary
if(NOT WIN32)
    target_link_options(symmetra PRIVATE
        $<$<CONFIG:Release>:-Wl,--strip-all>
        $<$<CONFIG:Release>:-Wl,-z,relro>
        $<$<CONFIG:Release>:-Wl,-z,now>
    )
endif()

# MSVC: strip PDB from release
if(WIN32)
    set_target_properties(symmetra PROPERTIES
        LINK_FLAGS_RELEASE "/INCREMENTAL:NO /DEBUG:NONE"
    )
    # sym.pdb excluded from release — no debug info shipped
endif()

set_target_properties(symmetra PROPERTIES
    OUTPUT_NAME "sym"
)

# ── Codegen unit test ─────────────────────────────────────────────────────────
# Runs without CUDA hardware — only exercises the code generator (C++ host side).
# Build: cmake --build . --target codegen_test
# Run:   ctest -R codegen_test --output-on-failure
option(SYMM_BUILD_TESTS "Build codegen unit tests" ON)
if(SYMM_BUILD_TESTS)
    add_executable(codegen_test
        src/compiler/test_codegen.cpp
        src/compiler/Codegen.cpp
        src/compiler/PreludeData.cpp
        src/parser/SymmParser.cpp
        src/parser/EsbuildBridge.cpp
    )
    target_include_directories(codegen_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(codegen_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    # NvrtcCompiler / GpuServer are NOT linked — test only exercises Codegen.cpp
    # We stub the ServerOptions type with a minimal definition via the full header.

    enable_testing()
    add_test(NAME codegen_test COMMAND codegen_test)
    message(STATUS "Codegen tests: ENABLED (run with ctest -R codegen_test)")

    # ── Benchmark tests ───────────────────────────────────────────────────────────
    add_executable(benchmark_test
        src/compiler/test_benchmarks.cpp
        src/compiler/Codegen.cpp
        src/compiler/PreludeData.cpp
        src/parser/SymmParser.cpp
        src/parser/EsbuildBridge.cpp
    )
    target_include_directories(benchmark_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(benchmark_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    add_test(NAME benchmark_test COMMAND benchmark_test)
    message(STATUS "Benchmark tests: ENABLED (run with ctest -R benchmark_test or ./Release/benchmark_test.exe)")

    # ── HTTP Scalability tests ─────────────────────────────────────────────────────
    add_executable(scalability_test
        src/runtime/test_http_scalability.cpp
    )
    target_include_directories(scalability_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(scalability_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    add_test(NAME scalability_test COMMAND scalability_test)
    message(STATUS "Scalability tests: ENABLED (run with ./Release/scalability_test.exe for full HTTP scale testing)")

    # ── Engine Robustness tests ────────────────────────────────────────────────────
    add_executable(robustness_test
        src/runtime/test_robustness.cpp
    )
    target_include_directories(robustness_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(robustness_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    add_test(NAME robustness_test COMMAND robustness_test)
    message(STATUS "Robustness tests: ENABLED (run with ./Release/robustness_test.exe for resource and error handling tests)")

    # ── Comprehensive Performance tests ────────────────────────────────────────────
    add_executable(performance_test
        src/runtime/test_performance_comprehensive.cpp
    )
    target_include_directories(performance_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(performance_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    add_test(NAME performance_test COMMAND performance_test)
    message(STATUS "Performance tests: ENABLED (run with ./Release/performance_test.exe for comprehensive benchmarking)")

    # ── Turing Machine Benchmark (warp divergence patterns) ──────────────────────
    add_executable(turing_machine_benchmark
        src/runtime/turing_machine_benchmark.cpp
    )
    target_include_directories(turing_machine_benchmark PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(turing_machine_benchmark PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    add_test(NAME turing_machine_benchmark COMMAND turing_machine_benchmark)
    message(STATUS "Turing Machine benchmarks: ENABLED (run with ./Release/turing_machine_benchmark.exe)")

    # ── GPU Batch Async Callback Test (CUDA callback performance) ────────────────────
    add_executable(gpu_batch_async_test
        src/runtime/test_gpu_batch_async.cpp
    )
    target_include_directories(gpu_batch_async_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(gpu_batch_async_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(gpu_batch_async_test PRIVATE CUDA::cuda_driver CUDA::cudart)
    add_test(NAME gpu_batch_async_test COMMAND gpu_batch_async_test)
    message(STATUS "GPU Batch Async tests: ENABLED (run with ./Release/gpu_batch_async_test.exe)")

    # ── HTTP Load Test (Max throughput benchmark) ────────────────────────────────
    add_executable(http_load_test
        src/runtime/test_http_load.cpp
    )
    target_include_directories(http_load_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(http_load_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    message(STATUS "HTTP Load tests: ENABLED (run with ./Release/http_load_test.exe [duration] [workers])")

    # ── Real Batching Test (Direct BatchScheduler + concurrent workers) ────────
    add_executable(real_batching_test
        src/runtime/test_real_batching.cpp
        src/runtime/BatchScheduler.cpp
    )
    target_include_directories(real_batching_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(real_batching_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(real_batching_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Real Batching test: ENABLED (run with ./Release/real_batching_test.exe [duration] [workers])")

    # ── Profiled Batching Test (Detailed bottleneck analysis) ─────────────────
    add_executable(profiled_batching_test
        src/runtime/test_profiled_batching.cpp
        src/runtime/BatchScheduler.cpp
    )
    target_include_directories(profiled_batching_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(profiled_batching_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(profiled_batching_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Profiled Batching test: ENABLED (run with ./Release/profiled_batching_test.exe [duration] [workers])")

    # ── Stream Blocking Analysis (Shows AsyncBatchLauncher benefit) ───────────
    add_executable(stream_blocking_test
        src/runtime/test_stream_blocking.cpp
    )
    target_include_directories(stream_blocking_test PRIVATE src/)
    message(STATUS "Stream Blocking test: ENABLED (demonstrates AsyncBatchLauncher advantage)")

    # ── Profiled Dispatch Test (Bottleneck breakdown) ─────────────────────────
    add_executable(profiled_dispatch_test
        src/runtime/test_profiled_dispatch.cpp
        src/runtime/BatchScheduler.cpp
    )
    target_include_directories(profiled_dispatch_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(profiled_dispatch_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(profiled_dispatch_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Profiled Dispatch test: ENABLED (measures scheduler overhead)")

    # ── Sharded Batching Test (Queue contention reduction) ───────────────────────
    add_executable(sharded_batching_test
        src/runtime/test_sharded_batching.cpp
        src/runtime/BatchScheduler.cpp
    )
    target_include_directories(sharded_batching_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(sharded_batching_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(sharded_batching_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Sharded Batching test: ENABLED (tests contention reduction)")

    # ── Direct Launch Test (AsyncBatchLauncher overhead analysis) ────────────────
    add_executable(direct_launch_test
        src/runtime/test_direct_launch.cpp
        src/runtime/AsyncBatchLauncher.cpp
    )
    target_include_directories(direct_launch_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(direct_launch_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(direct_launch_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Direct Launch test: ENABLED (measures AsyncBatchLauncher overhead)")

    # ── Submission Overhead Test (Isolate bottleneck) ───────────────────────────
    add_executable(submission_overhead_test
        src/runtime/test_submission_overhead.cpp
    )
    target_include_directories(submission_overhead_test PRIVATE src/)
    message(STATUS "Submission Overhead test: ENABLED (isolates component overhead)")

    # ── Decentralized Batching Benchmark (OLD vs NEW architecture) ───────────────
    add_executable(decentralized_batching_test
        src/runtime/test_decentralized_batching.cpp
        src/runtime/BatchScheduler.cpp
    )
    target_include_directories(decentralized_batching_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(decentralized_batching_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    target_link_libraries(decentralized_batching_test PRIVATE ${CUDAToolkit_LIBRARIES})
    message(STATUS "Decentralized Batching test: ENABLED (compares old vs new architecture)")

    # ── Parameter Tuning Framework (Find optimal config for 1M+ RPS) ───────────────
    add_executable(tuning_framework_test
        src/runtime/test_tuning_framework.cpp
    )
    target_include_directories(tuning_framework_test PRIVATE src/)
    message(STATUS "Tuning Framework test: ENABLED (systematically optimizes parameters)")

    # ── Max Throughput Test (Pure GPU batch performance) ────────────────────────
    add_executable(max_throughput_test
        src/runtime/test_max_throughput.cpp
    )
    target_include_directories(max_throughput_test PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(max_throughput_test PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    message(STATUS "Max Throughput tests: ENABLED (run with ./Release/max_throughput_test.exe [duration])")

    # ── Production Benchmark (Real workload: fetch → GPU → serialize) ────────────
    add_executable(production_benchmark
        src/runtime/test_production_benchmark.cpp
    )
    target_include_directories(production_benchmark PRIVATE src/ ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(production_benchmark PRIVATE "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}")
    message(STATUS "Production Benchmark: ENABLED (run with ./Release/production_benchmark.exe [duration] [workers])")

    # ── Phase 2: GPU Saturation Validation Test ────────────────────────────────
    add_executable(phase2_saturation_test
        src/runtime/test_phase2_gpu_saturation.cpp
    )
    target_include_directories(phase2_saturation_test PRIVATE src/)
    message(STATUS "Phase 2 GPU Saturation test: ENABLED (validates 32 workers, 256 streams configuration)")

    # ── Phase 3: GPU Kernel Validation (Simulated GPU Workload) ──────────────────
    add_executable(phase3_kernel_validation_test
        src/runtime/test_phase3_gpu_kernel_validation.cpp
    )
    target_include_directories(phase3_kernel_validation_test PRIVATE src/)
    message(STATUS "Phase 3 GPU Kernel Validation test: ENABLED (run with ./Release/phase3_kernel_validation_test.exe [workers] [duration])")

    # ── Phase 4: Real GPU Kernel Execution ────────────────────────────────────────
    add_executable(phase4_real_gpu_execution_test
        src/runtime/test_phase4_real_gpu_execution.cpp
    )
    target_include_directories(phase4_real_gpu_execution_test PRIVATE src/)
    message(STATUS "Phase 4 Real GPU Execution test: ENABLED (run with ./Release/phase4_real_gpu_execution_test.exe [workers] [duration])")

    # ── Phase 5: Sustained Load Testing ────────────────────────────────────────────
    add_executable(phase5_sustained_load_test
        src/runtime/test_phase5_sustained_load.cpp
    )
    target_include_directories(phase5_sustained_load_test PRIVATE src/)
    message(STATUS "Phase 5 Sustained Load test: ENABLED (run with ./Release/phase5_sustained_load_test.exe [workers] [duration_minutes])")

    # ── Phase 6: Real GPU Kernel Execution Validation ──────────────────────────────────
    add_executable(phase6_real_kernel_execution_test
        src/runtime/test_phase6_real_kernel_execution.cpp
    )
    target_include_directories(phase6_real_kernel_execution_test PRIVATE src/)
    message(STATUS "Phase 6 Real Kernel Execution test: ENABLED (run with ./Release/phase6_real_kernel_execution_test.exe [workers] [duration_minutes])")

    # ── Phase 7: Multi-Route Load Distribution Benchmarking ────────────────────────────
    add_executable(phase7_multi_route_test
        src/runtime/test_phase7_multi_route_benchmarking.cpp
    )
    target_include_directories(phase7_multi_route_test PRIVATE src/)
    message(STATUS "Phase 7 Multi-Route Benchmarking test: ENABLED (run with ./Release/phase7_multi_route_test.exe [workers] [duration_minutes])")

    # ── Phase 8: HTTP Server Foundation (Socket I/O + Request Parsing) ─────────────────
    add_executable(phase8_http_server_test
        src/runtime/test_phase8_http_server.cpp
    )
    target_include_directories(phase8_http_server_test PRIVATE src/)
    if(WIN32)
        target_link_libraries(phase8_http_server_test PRIVATE ws2_32)
    endif()
    message(STATUS "Phase 8 HTTP Server Foundation test: ENABLED (run with ./Release/phase8_http_server_test.exe [port] [workers] [duration_minutes])")

    # ── Phase 8: Integrated HTTP Pipeline (Client + Server + GPU) ──────────────────────
    add_executable(phase8_integration_test
        src/runtime/test_phase8_integration.cpp
    )
    target_include_directories(phase8_integration_test PRIVATE src/)
    if(WIN32)
        target_link_libraries(phase8_integration_test PRIVATE ws2_32)
    endif()
    message(STATUS "Phase 8 Integrated HTTP Pipeline test: ENABLED (run with ./Release/phase8_integration_test.exe [port] [workers] [duration_minutes])")
else()
    message(STATUS "Codegen tests: OFF (pass -DSYMM_BUILD_TESTS=ON to enable)")
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS symmetra DESTINATION bin)

message(STATUS "Symmetra Engine v1.0")
message(STATUS "  CUDA ${CUDAToolkit_VERSION}  ·  PTX virtual arch: ${CUDA_PTX_VIRTARCH}")
message(STATUS "  Obfuscation: ${SYMM_OBFUSCATE}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
