#include <stdio.h>
#include <chrono>
#include <cuda_runtime.h>

// LIGHTNING: Pure arithmetic
__global__ void kernel_lightning(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int result = 0;
        for (int i = 0; i < 100; i++) {
            result = result + i;
            result = result * 2;
            result = result - i;
        }
        results[idx] = result;
    }
}

// HYPER: 1-state, 15-cell
__global__ void kernel_hyper(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[15] = {0};
        int state = 0, pos = 7, steps = 0;
        while (steps < 300 && state != -1) {
            if (pos < 0 || pos >= 15) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 15; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// EXTREME: 1-state, 12-cell
__global__ void kernel_extreme(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[12] = {0};
        int state = 0, pos = 6, steps = 0;
        while (steps < 400 && state != -1) {
            if (pos < 0 || pos >= 12) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 12; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// ULTRA: 10-cell flip
__global__ void kernel_ultra(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[10] = {0};
        for (int i = 0; i < 10; i++) tape[i] = tape[i] == 0 ? 1 : 0;
        int ones = 0;
        for (int i = 0; i < 10; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// BALANCED: 2-state, 25-cell
__global__ void kernel_balanced(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[25] = {0};
        int state = 0, pos = 12, steps = 0;
        while (steps < 450 && state != -1) {
            if (pos < 0 || pos >= 25) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 1; }
                else { tape[pos] = 0; pos--; state = -1; }
            } else if (state == 1) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 0; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 25; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// SUPERFAST: 1-state, 20-cell
__global__ void kernel_superfast(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[20] = {0};
        int state = 0, pos = 10, steps = 0;
        while (steps < 600 && state != -1) {
            if (pos < 0 || pos >= 20) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 20; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// SIMPLE: 3-state, 40-cell
__global__ void kernel_simple(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[40] = {0};
        int state = 0, pos = 20, steps = 0;
        while (steps < 500 && state != -1) {
            if (pos < 0 || pos >= 40) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 1; }
                else { tape[pos] = 0; pos--; state = 2; }
            } else if (state == 1) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 0; }
                else { tape[pos] = 1; pos--; state = 0; }
            } else if (state == 2) {
                if (symbol == 0) { tape[pos] = 0; pos++; state = 1; }
                else { tape[pos] = 0; pos--; state = 1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 40; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// MODERATE: 4-state, 64-cell
__global__ void kernel_moderate(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[64] = {0};
        int state = 0, pos = 32, steps = 0;
        while (steps < 600 && state != -1) {
            if (pos < 0 || pos >= 64) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 1; }
                else { tape[pos] = 0; pos--; state = 2; }
            } else if (state == 1) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 0; }
                else { tape[pos] = 1; pos--; state = 3; }
            } else if (state == 2) {
                if (symbol == 0) { tape[pos] = 0; pos++; state = 3; }
                else { tape[pos] = 0; pos--; state = 1; }
            } else if (state == 3) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 2; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 64; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

// REFERENCE: 5-state, 128-cell
__global__ void kernel_reference(int* results, int count) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < count) {
        int tape[128] = {0};
        int state = 0, pos = 64, steps = 0;
        while (steps < 1000 && state != -1) {
            if (pos < 0 || pos >= 128) { state = -1; break; }
            int symbol = tape[pos];
            if (state == 0) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 1; }
                else { tape[pos] = 0; pos--; state = 2; }
            } else if (state == 1) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 0; }
                else { tape[pos] = 1; pos--; state = 3; }
            } else if (state == 2) {
                if (symbol == 0) { tape[pos] = 0; pos++; state = 3; }
                else { tape[pos] = 0; pos--; state = 4; }
            } else if (state == 3) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 2; }
                else { tape[pos] = 0; pos--; state = 1; }
            } else if (state == 4) {
                if (symbol == 0) { tape[pos] = 1; pos++; state = 3; }
                else { tape[pos] = 0; pos--; state = -1; }
            }
            steps++;
        }
        int ones = 0;
        for (int i = 0; i < 128; i++) if (tape[i] == 1) ones++;
        results[idx] = ones;
    }
}

void benchmark(const char* name, void (*kernel)(int*, int), int requests, int duration_sec) {
    printf("\nBenchmarking %s...\n", name);

    int* d_results;
    cudaMalloc(&d_results, requests * sizeof(int));

    auto start = std::chrono::high_resolution_clock::now();
    int iterations = 0;

    while (true) {
        kernel<<<(requests + 255) / 256, 256>>>(d_results, requests);
        cudaDeviceSynchronize();
        iterations++;

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
        if (elapsed >= duration_sec) break;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double total_sec = total_ms / 1000.0;

    long long total_requests = (long long)requests * iterations;
    double rps = total_requests / total_sec;

    printf("  %s: %.0f RPS (%lld requests in %.2fs)\n", name, rps, total_requests, total_sec);

    cudaFree(d_results);
}

int main() {
    printf("Turing Machine GPU Benchmark\n");
    printf("GPU Variant: RTX 5070, CUDA 13.2\n");
    printf("Test Duration: 10 seconds per variant\n");
    printf("Batch Size: 10000 requests\n\n");

    int requests = 10000;
    int duration = 10;

    benchmark("LIGHTNING (pure arithmetic)",
              (void(*)(int*,int))kernel_lightning, requests, duration);
    benchmark("HYPER (1-state, 15-cell)",
              (void(*)(int*,int))kernel_hyper, requests, duration);
    benchmark("EXTREME (1-state, 12-cell)",
              (void(*)(int*,int))kernel_extreme, requests, duration);
    benchmark("ULTRA (10-cell flip)",
              (void(*)(int*,int))kernel_ultra, requests, duration);
    benchmark("BALANCED (2-state, 25-cell)",
              (void(*)(int*,int))kernel_balanced, requests, duration);
    benchmark("SUPERFAST (1-state, 20-cell)",
              (void(*)(int*,int))kernel_superfast, requests, duration);
    benchmark("SIMPLE (3-state, 40-cell)",
              (void(*)(int*,int))kernel_simple, requests, duration);
    benchmark("MODERATE (4-state, 64-cell)",
              (void(*)(int*,int))kernel_moderate, requests, duration);
    benchmark("REFERENCE (5-state, 128-cell)",
              (void(*)(int*,int))kernel_reference, requests, duration);

    printf("\nBenchmark complete.\n");
    return 0;
}
