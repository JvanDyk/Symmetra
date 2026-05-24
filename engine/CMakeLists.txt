cmake_minimum_required(VERSION 3.20)
project(symmetra LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

# ── CUDA ──────────────────────────────────────────────────────────────────────
find_package(CUDAToolkit REQUIRED)
# CMAKE_CUDA_ARCHITECTURES only affects host-side compilation; kernels ship as
# encrypted PTX (virtual arch compute_75) and are JITed by the driver at load.
set(CMAKE_CUDA_ARCHITECTURES 75 CACHE STRING "CUDA architectures")

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
    # Runtime — scheduling, memory, and workers
    src/runtime/NvrtcCompiler.cpp
    src/runtime/GpuServer.cpp
    src/runtime/GpuAllocator.cpp
    src/runtime/WarpScheduler.cpp
    src/runtime/StreamPool.cpp
    src/runtime/ThreadPool.cpp
    src/runtime/GpuWorker.cpp
    # High-priority runtime features
    src/runtime/TimerQueue.cpp
    src/runtime/AbortSignal.cpp
    src/runtime/UrlParser.cpp
    src/runtime/GpuStream.cpp
    # Protection
    src/protect/KernelStore.cpp
    src/protect/PtxCrypt.cpp
    src/protect/AntiDebug.cpp
    src/protect/SymmVm.cpp
)

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

if(WIN32)
    target_link_libraries(symmetra PRIVATE ws2_32)
endif()

# ── Compile definitions (protection keys) ────────────────────────────────────

# SYMM_PTX_KEY / SYMM_PTX_IV are NOT passed as defines — they contain commas which
# nvcc on Windows misparses as multiple input files. PtxCrypt.h has #ifndef fallbacks
# with the same default values. Override per-release by editing PtxCrypt.h directly.
target_compile_definitions(symmetra PRIVATE
    "SYMM_OBFKEY=${SYMM_OBFKEY_VAL}"
)

# ── LLVM / Clang obfuscation (Layer 1) ───────────────────────────────────────
option(SYMM_OBFUSCATE "Enable LLVM obfuscation passes (requires Clang + Hikari)" OFF)

if(SYMM_OBFUSCATE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(WARNING "SYMM_OBFUSCATE requires Clang. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
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
option(SYMM_BUILD_TESTS "Build codegen unit tests" OFF)
if(SYMM_BUILD_TESTS)
    add_executable(codegen_test
        src/compiler/test_codegen.cpp
        src/compiler/Codegen.cpp
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
else()
    message(STATUS "Codegen tests: OFF (pass -DSYMM_BUILD_TESTS=ON to enable)")
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS symmetra DESTINATION bin)

message(STATUS "Symmetra Engine v1.0")
message(STATUS "  CUDA ${CUDAToolkit_VERSION}  ·  PTX virtual arch: ${CUDA_PTX_VIRTARCH}")
message(STATUS "  Obfuscation: ${SYMM_OBFUSCATE}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
