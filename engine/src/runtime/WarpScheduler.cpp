#include "WarpScheduler.h"
#include <mutex>

namespace symm {

// One cudaGetDeviceProperties call caches all three derived values.
namespace {
struct DevProps {
    int smCount              = 1;
    int maxThreadsPerMP      = 2048;
    int maxParallelism       = 2048;
};

const DevProps& devProps() {
    static DevProps p;
    static std::once_flag flag;
    std::call_once(flag, [] {
        cudaDeviceProp prop{};
        if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
            p.smCount         = prop.multiProcessorCount;
            p.maxThreadsPerMP = prop.maxThreadsPerMultiProcessor;
            p.maxParallelism  = prop.multiProcessorCount
                              * prop.maxThreadsPerMultiProcessor;
        }
    });
    return p;
}
} // anonymous namespace

int WarpScheduler::smCount()         { return devProps().smCount; }
int WarpScheduler::maxParallelism()  { return devProps().maxParallelism; }
int WarpScheduler::maxThreadsPerMP() { return devProps().maxThreadsPerMP; }

} // namespace symm
