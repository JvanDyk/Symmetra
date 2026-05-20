cmake_minimum_required(VERSION 3.20)
project(symmetra LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")

# ── CUDA ──────────────────────────────────────────────────────────────────────
find_package(CUDAToolkit REQUIRED)

# Detect GPU architecture from device (fallback sm_89 for RTX 4090/5070)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86 89 90 CACHE STRING "CUDA architectures")

# ── Sources ───────────────────────────────────────────────────────────────────
set(ENGINE_SOURCES
    src/main.cpp
    src/parser/EsbuildBridge.cpp
    src/parser/SymmParser.cpp
    src/compiler/Codegen.cpp
    src/runtime/NvrtcCompiler.cpp
    src/runtime/GpuServer.cpp
)

set(CUDA_SOURCES
    src/runtime/IoKernel.cu
    src/runtime/HttpKernels.cu
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(symmetra ${ENGINE_SOURCES} ${CUDA_SOURCES})

target_include_directories(symmetra PRIVATE
    src/
    ${CUDAToolkit_INCLUDE_DIRS}
)

target_link_libraries(symmetra PRIVATE
    CUDA::cuda_driver
    CUDA::nvrtc
    CUDA::cudart
)

# Windows: link Winsock for socket layer
if(WIN32)
    target_link_libraries(symmetra PRIVATE ws2_32)
endif()

# Strip debug symbols in release
target_compile_options(symmetra PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>
)

set_target_properties(symmetra PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    OUTPUT_NAME "symmetra"
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS symmetra DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/runtime/prelude.cu DESTINATION share/symmetra)

message(STATUS "Symmetra Engine — CUDA ${CUDAToolkit_VERSION}, archs: ${CMAKE_CUDA_ARCHITECTURES}")
