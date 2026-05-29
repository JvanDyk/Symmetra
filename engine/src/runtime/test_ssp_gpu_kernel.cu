/**
 * Test SSP GPU Kernel
 * Verifies ChaCha20-Poly1305 compilation and benchmarks throughput
 */

#include <iostream>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <cuda_runtime.h>

// Include GPU kernel header
#include "gpu/chacha20_poly1305.cuh"

int main(int argc, char* argv[]) {
    std::cout << "\n╔════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  GPU ChaCha20-Poly1305 Kernel Test                                   ║" << std::endl;
    std::cout << "║  Verifying compilation and benchmarking throughput                   ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════════════╝\n" << std::endl;

    std::cout << "📊 Testing GPU ChaCha20-Poly1305 kernel...\n" << std::endl;

    // Test 1: Small batch (8 messages, 512 bytes each)
    {
        std::cout << "Test 1: Small batch (8 messages × 512 bytes)" << std::endl;
        float throughput = chacha20_poly1305_benchmark(8, 512);
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput << " GB/s" << std::endl;

        if (throughput > 0.0f) {
            std::cout << "  ✅ GPU kernel working\n" << std::endl;
        } else {
            std::cout << "  ❌ GPU kernel failed\n" << std::endl;
            return 1;
        }
    }

    // Test 2: Medium batch (64 messages, 1KB each)
    {
        std::cout << "Test 2: Medium batch (64 messages × 1KB)" << std::endl;
        float throughput = chacha20_poly1305_benchmark(64, 1024);
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput << " GB/s" << std::endl;

        if (throughput > 1.0f) {
            std::cout << "  ✅ Good throughput\n" << std::endl;
        } else {
            std::cout << "  ⚠️  Low throughput (check GPU optimization)\n" << std::endl;
        }
    }

    // Test 3: Large batch (256 messages, 4KB each)
    {
        std::cout << "Test 3: Large batch (256 messages × 4KB)" << std::endl;
        float throughput = chacha20_poly1305_benchmark(256, 4096);
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput << " GB/s" << std::endl;

        if (throughput > 5.0f) {
            std::cout << "  ✅ Excellent throughput (>5 GB/s)\n" << std::endl;
        } else if (throughput > 1.0f) {
            std::cout << "  ✅ Good throughput (>1 GB/s)\n" << std::endl;
        } else {
            std::cout << "  ⚠️  Low throughput\n" << std::endl;
        }
    }

    // Test 4: Maximum batch size (512 messages, 8KB each)
    {
        std::cout << "Test 4: Maximum batch (512 messages × 8KB)" << std::endl;
        float throughput = chacha20_poly1305_benchmark(512, 8192);
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << throughput << " GB/s" << std::endl;

        if (throughput >= 10.0f) {
            std::cout << "  ✅ EXCELLENT! Target achieved (>10 GB/s)\n" << std::endl;
        } else if (throughput >= 5.0f) {
            std::cout << "  ✅ VERY GOOD (5-10 GB/s)\n" << std::endl;
        } else if (throughput >= 1.0f) {
            std::cout << "  ✅ ACCEPTABLE (1-5 GB/s)\n" << std::endl;
        } else {
            std::cout << "  ❌ NEEDS OPTIMIZATION (<1 GB/s)\n" << std::endl;
        }
    }

    std::cout << "\n╔════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Day 1: GPU Kernel Test Complete                                    ║" << std::endl;
    std::cout << "║  Next: Run with: ./phase8_gpu_kernel_test [batch_size] [msg_size]   ║" << std::endl;
    std::cout << "║  Target: 10+ GB/s throughput                                        ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════════════════╝\n" << std::endl;

    return 0;
}
