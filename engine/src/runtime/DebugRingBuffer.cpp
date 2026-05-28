#include "DebugRingBuffer.h"
#include "GpuAllocator.h"
#include <cuda_runtime.h>
#include <cstring>
#include <algorithm>
#include <sstream>

namespace symm {

CUdeviceptr DebugRingBuffer::allocate() {
    CUdeviceptr ptr = GpuAllocator::instance().heapAlloc(BUFFER_SIZE);

    // Initialize: head and tail both at 0 (indices into the data region)
    uint32_t initial[2] = {0, 0};
    cudaMemcpy((void*)ptr, initial, HEADER_SIZE, cudaMemcpyHostToDevice);
    cudaDeviceSynchronize();

    return ptr;
}

std::vector<std::string> DebugRingBuffer::readLogs(CUdeviceptr bufferPtr) {
    std::vector<std::string> logs;

    // Read head and tail pointers
    uint32_t hostHeader[2] = {0, 0};
    cudaMemcpy(hostHeader, (void*)bufferPtr, HEADER_SIZE, cudaMemcpyDeviceToHost);
    cudaDeviceSynchronize();

    uint32_t head = hostHeader[0];
    uint32_t tail = hostHeader[1];

    // Read the entire data region
    std::vector<char> hostData(DATA_SIZE);
    cudaMemcpy(hostData.data(), (void*)(bufferPtr + HEADER_SIZE), DATA_SIZE, cudaMemcpyDeviceToHost);
    cudaDeviceSynchronize();

    // Parse entries from tail to head
    // Entries have format: [4 bytes: length] [length bytes: message]
    uint32_t pos = tail;
    while (pos != head && pos < DATA_SIZE) {
        // Read the 4-byte length prefix
        if (pos + 4 > DATA_SIZE) break;  // Not enough space for length

        uint32_t msgLen = *((uint32_t*)(hostData.data() + pos));
        pos += 4;

        // Sanity check: message length should be reasonable
        if (msgLen == 0 || msgLen > DATA_SIZE - pos) break;

        // Read the message
        std::string msg(hostData.data() + pos, msgLen);
        logs.push_back(msg);
        pos += msgLen;

        // Account for wrap-around in ring buffer logic
        if (pos >= DATA_SIZE) pos = 0;
        if (pos == head) break;
    }

    return logs;
}

void DebugRingBuffer::clearBuffer(CUdeviceptr bufferPtr) {
    uint32_t zeros[2] = {0, 0};
    cudaMemcpy((void*)bufferPtr, zeros, HEADER_SIZE, cudaMemcpyHostToDevice);
    cudaDeviceSynchronize();
}

size_t DebugRingBuffer::getUsage(CUdeviceptr bufferPtr) {
    uint32_t hostHeader[2] = {0, 0};
    cudaMemcpy(hostHeader, (void*)bufferPtr, HEADER_SIZE, cudaMemcpyDeviceToHost);
    cudaDeviceSynchronize();

    uint32_t head = hostHeader[0];
    uint32_t tail = hostHeader[1];

    if (head >= tail) {
        return head - tail;
    } else {
        // Wrapped around
        return DATA_SIZE - tail + head;
    }
}

} // namespace symm
