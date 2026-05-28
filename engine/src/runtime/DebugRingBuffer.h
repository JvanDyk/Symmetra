#pragma once
#include <cuda.h>
#include <cstring>
#include <atomic>
#include <string>
#include <vector>

namespace symm {

// GPU-side debug ring buffer for console.log output.
// Allocated once in persistent heap; persists across requests.
//
// Format: circular buffer of entries, each entry is:
//   [4 bytes: message length N] [N bytes: null-terminated message]
//
// Write operations are atomic and wrap around on overflow.
// The buffer is read-safe from CPU side at any time — entries are only appended.
struct DebugRingBuffer {
    static constexpr size_t BUFFER_SIZE = 64 * 1024;  // 64 KB
    static constexpr size_t HEADER_SIZE = 8;  // 4 bytes head + 4 bytes tail
    static constexpr size_t DATA_SIZE   = BUFFER_SIZE - HEADER_SIZE;

    // GPU device-side accessor struct (passed to kernels)
    struct DeviceBuffer {
        uint32_t* head_ptr;     // pointer to head index (in bytes)
        uint32_t* tail_ptr;     // pointer to tail index (in bytes)
        char*     data_ptr;     // start of ring buffer data
    };

    // Allocate a ring buffer in persistent heap and return its VRAM address
    static CUdeviceptr allocate();

    // CPU-side reader: extract all complete log entries from the buffer
    static std::vector<std::string> readLogs(CUdeviceptr bufferPtr);

    // CPU-side: clear the buffer (reset head/tail pointers)
    static void clearBuffer(CUdeviceptr bufferPtr);

    // CPU-side: get current buffer usage in bytes
    static size_t getUsage(CUdeviceptr bufferPtr);
};

} // namespace symm
