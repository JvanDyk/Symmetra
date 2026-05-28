#include <stdio.h>

__global__ void test_kernel() {
    printf("GPU works!\n");
}

extern "C" int run_test() {
    test_kernel<<<1, 1>>>();
    cudaDeviceSynchronize();
    return 0;
}
