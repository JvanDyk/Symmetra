#include <iostream>
#include "engine/src/runtime/LockFreeQueue.h"
#include "engine/src/runtime/BatchScheduler.h"

using namespace symm;

int main() {
    std::cout << "Testing LockFreeQueue<int>..." << std::endl;

    LockFreeQueue<int, 1024> q;

    // Test enqueue
    std::cout << "Enqueuing 10 items..." << std::endl;
    for (int i = 0; i < 10; i++) {
        if (!q.tryEnqueue(i)) {
            std::cout << "  ERROR: Failed to enqueue " << i << std::endl;
            return 1;
        }
    }
    std::cout << "  OK. Queue size: " << q.size() << std::endl;

    // Test dequeue
    std::cout << "Dequeuing items..." << std::endl;
    for (int i = 0; i < 10; i++) {
        int val;
        if (!q.tryDequeue(val)) {
            std::cout << "  ERROR: Failed to dequeue at position " << i << std::endl;
            return 1;
        }
        if (val != i) {
            std::cout << "  ERROR: Got " << val << ", expected " << i << std::endl;
            return 1;
        }
        std::cout << "  " << val << " OK" << std::endl;
    }

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
