cmake_minimum_required(VERSION 3.20)
project(symmetra_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

set(ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")

# Find threads (required for TimerQueue, ThreadPool tests)
find_package(Threads REQUIRED)

# Shared include path for all unit tests
include_directories(${ENGINE_SRC})

# ── Helper macro ──────────────────────────────────────────────────────────────
macro(add_unit_test NAME SRC_TEST ENGINE_SRCS)
    add_executable(${NAME} unit/${SRC_TEST} ${ENGINE_SRCS})
    target_link_libraries(${NAME} PRIVATE Threads::Threads)
    add_test(NAME ${NAME} COMMAND ${NAME})
endmacro()

enable_testing()

# ── UrlParser ─────────────────────────────────────────────────────────────────
add_unit_test(test_url_parser
    test_url_parser.cpp
    "${ENGINE_SRC}/runtime/UrlParser.cpp"
)

# ── TimerQueue ────────────────────────────────────────────────────────────────
add_unit_test(test_timer_queue
    test_timer_queue.cpp
    "${ENGINE_SRC}/runtime/TimerQueue.cpp"
)

# ── ThreadPool ────────────────────────────────────────────────────────────────
add_unit_test(test_thread_pool
    test_thread_pool.cpp
    "${ENGINE_SRC}/runtime/ThreadPool.cpp"
)

# ── BatchScheduler (CPU-only — mock GPU launch callback, no CUDA required) ───
add_unit_test(test_batch_scheduler
    test_batch_scheduler.cpp
    "${ENGINE_SRC}/runtime/BatchScheduler.cpp"
)

# ── Run all (CPU-only, no GPU required) ──────────────────────────────────────
# cmake --build build/tests && ctest --test-dir build/tests --output-on-failure
#
# ── CUDA tests (require NVCC + GPU) ─────────────────────────────────────────
# Run from the project root directory:
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_allocator.cpp \
#      ../engine/src/runtime/GpuAllocator.cpp -lcuda -lcudart -o test_allocator
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_vram_pipe.cpp \
#      ../engine/src/runtime/GpuStream.cpp -lcuda -lcudart -lpthread -o test_vram_pipe
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_abort_signal.cpp \
#      ../engine/src/runtime/AbortSignal.cpp -lcuda -lcudart -lpthread -o test_abort_signal
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_stream_pool.cpp \
#      ../engine/src/runtime/StreamPool.cpp -lcuda -lcudart -lpthread -o test_stream_pool
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_http_kernels.cpp \
#      -lcuda -lcudart -lnvrtc -o test_http_kernels
#
# nvcc -std=c++17 -I../engine/src tests/unit/test_text_kernel.cpp \
#      ../engine/src/kernels/TextKernel.cu -lcuda -lcudart -o test_text_kernel
