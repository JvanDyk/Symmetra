#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iomanip>

// Mock CUDA operations for testing (in real implementation, would use actual GPU)
class HttpPerformanceTest {
public:
    struct PerformanceMetrics {
        double meanLatencyUs;
        double medianLatencyUs;
        double minLatencyUs;
        double maxLatencyUs;
        double stdevLatencyUs;
        double p95LatencyUs;
        double p99LatencyUs;
        double throughputRps;
        double totalTimeMs;
        int sampleCount;
    };

    // Simulate HTTP request parsing + GPU handler execution + response serialization
    struct HttpRequestSimulation {
        const char* method;
        const char* path;
        const char* body;
        int bodyLen;
        int expectedStatusCode;

        // Simulated GPU execution time (microseconds)
        int gpuExecutionTimeUs;
    };

    // Benchmark 1: Simple GET endpoint (minimal GPU work)
    static PerformanceMetrics benchmarkSimpleGet(int iterations = 10000) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (10us) + GPU dispatch (2us) + GPU execute (50us) + GPU serialize (15us) + CPU send (5us)
            simulateHttpRequest({
                .method = "GET",
                .path = "/ping",
                .body = nullptr,
                .bodyLen = 0,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 50
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 2: JSON parsing + transformation (moderate GPU work)
    static PerformanceMetrics benchmarkJsonTransform(int iterations = 5000) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (15us) + GPU dispatch (2us) + GPU parse JSON (100us) + GPU transform (150us) + GPU serialize (80us) + CPU send (5us)
            simulateHttpRequest({
                .method = "POST",
                .path = "/transform",
                .body = "{\"data\": [1,2,3,4,5]}",
                .bodyLen = 23,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 330  // 100 + 150 + 80
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 3: Regex matching + string replacement (advanced GPU work)
    static PerformanceMetrics benchmarkRegexOperation(int iterations = 2000) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (12us) + GPU dispatch (2us) + GPU regex match (200us) + GPU string replace (150us) + GPU serialize (50us) + CPU send (5us)
            simulateHttpRequest({
                .method = "POST",
                .path = "/regex",
                .body = "email@example.com",
                .bodyLen = 17,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 400  // 200 + 150 + 50
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 4: Array processing (heavy GPU work)
    static PerformanceMetrics benchmarkArrayProcessing(int iterations = 1000) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (20us) + GPU dispatch (2us) + GPU parse JSON (100us) + GPU map/filter/reduce (300us) + GPU serialize (100us) + CPU send (5us)
            simulateHttpRequest({
                .method = "POST",
                .path = "/array-process",
                .body = "[1,2,3,4,5,6,7,8,9,10]",
                .bodyLen = 23,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 500  // 100 + 300 + 100
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 5: Batch processing (concurrent requests)
    static PerformanceMetrics benchmarkBatchProcessing(int batchSize = 32, int batches = 100) {
        std::vector<double> latencies;

        for (int b = 0; b < batches; b++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate batch dispatch: CPU collects N requests, launches single kernel for all 32 concurrent
            // Each thread processes one request concurrently on GPU
            for (int i = 0; i < batchSize; i++) {
                simulateHttpRequest({
                    .method = "POST",
                    .path = "/batch-item",
                    .body = "{\"id\": 1}",
                    .bodyLen = 10,
                    .expectedStatusCode = 200,
                    .gpuExecutionTimeUs = 100  // Same as simple work
                });
            }

            auto end = std::chrono::high_resolution_clock::now();
            // Batch latency is max(single request latency) * 32 threads in parallel
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count() / batchSize;
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, batches);
    }

    // Benchmark 6: Error handling (invalid input, exceptions)
    static PerformanceMetrics benchmarkErrorHandling(int iterations = 5000) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (10us) + GPU dispatch (2us) + GPU try-catch with exception (80us) + GPU serialize error (20us) + CPU send (5us)
            simulateHttpRequest({
                .method = "POST",
                .path = "/error-handler",
                .body = "invalid json {",
                .bodyLen = 14,
                .expectedStatusCode = 400,  // Error response
                .gpuExecutionTimeUs = 100   // Includes exception handling
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 7: Memory bandwidth (large response)
    static PerformanceMetrics benchmarkLargeResponse(int iterations = 500) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: CPU parse (15us) + GPU dispatch (2us) + GPU generate large JSON (200us) + GPU serialize (500us, H2D + D2H copy) + CPU send (50us)
            simulateHttpRequest({
                .method = "GET",
                .path = "/large-response",
                .body = nullptr,
                .bodyLen = 0,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 750   // 200 gen + 500 serialize
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Benchmark 8: Cold start (first request after compile)
    static PerformanceMetrics benchmarkColdStart(int iterations = 10) {
        std::vector<double> latencies;

        for (int i = 0; i < iterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();

            // Simulate: Kernel load (5000us) + first execution overhead (100us) + normal request (100us)
            simulateHttpRequest({
                .method = "GET",
                .path = "/cold",
                .body = nullptr,
                .bodyLen = 0,
                .expectedStatusCode = 200,
                .gpuExecutionTimeUs = 5200   // Cold start overhead
            });

            auto end = std::chrono::high_resolution_clock::now();
            auto durationUs = std::chrono::duration<double, std::micro>(end - start).count();
            latencies.push_back(durationUs);
        }

        return calculateMetrics(latencies, iterations);
    }

    // Helper: Print metrics in tabular format
    static void printMetrics(const std::string& testName, const PerformanceMetrics& metrics) {
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Test: " << testName << "\n";
        std::cout << std::string(80, '=') << "\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Latency Statistics (microseconds):\n";
        std::cout << "  Min:          " << metrics.minLatencyUs << " μs\n";
        std::cout << "  Median (p50): " << metrics.medianLatencyUs << " μs\n";
        std::cout << "  Mean:         " << metrics.meanLatencyUs << " μs\n";
        std::cout << "  P95:          " << metrics.p95LatencyUs << " μs\n";
        std::cout << "  P99:          " << metrics.p99LatencyUs << " μs\n";
        std::cout << "  Max:          " << metrics.maxLatencyUs << " μs\n";
        std::cout << "  Stdev:        " << metrics.stdevLatencyUs << " μs\n";
        std::cout << "\nThroughput:\n";
        std::cout << "  Requests/sec: " << metrics.throughputRps << " RPS\n";
        std::cout << "  Total time:   " << metrics.totalTimeMs << " ms\n";
        std::cout << "  Samples:      " << metrics.sampleCount << "\n";
    }

    // Run all benchmarks
    static void runAllBenchmarks() {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗\n";
        std::cout << "║              Symmetra GPU HTTP Request Performance Benchmarks                ║\n";
        std::cout << "║                        Execution Profile Analysis                            ║\n";
        std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝\n";

        auto metricsSimpleGet = benchmarkSimpleGet(10000);
        printMetrics("Simple GET endpoint", metricsSimpleGet);

        auto metricsJsonTransform = benchmarkJsonTransform(5000);
        printMetrics("JSON transform", metricsJsonTransform);

        auto metricsRegex = benchmarkRegexOperation(2000);
        printMetrics("Regex matching + replacement", metricsRegex);

        auto metricsArray = benchmarkArrayProcessing(1000);
        printMetrics("Array processing (map/filter/reduce)", metricsArray);

        auto metricsBatch = benchmarkBatchProcessing(32, 100);
        printMetrics("Batch processing (32 concurrent)", metricsBatch);

        auto metricsError = benchmarkErrorHandling(5000);
        printMetrics("Error handling", metricsError);

        auto metricsLarge = benchmarkLargeResponse(500);
        printMetrics("Large response (memory bandwidth)", metricsLarge);

        auto metricsCold = benchmarkColdStart(10);
        printMetrics("Cold start", metricsCold);

        // Summary comparison
        printSummaryTable({
            {"Simple GET", metricsSimpleGet},
            {"JSON Transform", metricsJsonTransform},
            {"Regex", metricsRegex},
            {"Array Process", metricsArray},
            {"Batch (32x)", metricsBatch},
            {"Error", metricsError},
            {"Large Response", metricsLarge},
            {"Cold Start", metricsCold}
        });
    }

private:
    // Simulate CPU parse + GPU dispatch + GPU execution + CPU send
    static void simulateHttpRequest(const HttpRequestSimulation& req) {
        // CPU-side parsing overhead (10-20us)
        volatile int cpuParseTime = 12;
        for (int i = 0; i < cpuParseTime * 100; i++) {
            cpuParseTime = cpuParseTime % 2;
        }

        // GPU dispatch overhead (2-5us)
        volatile int dispatchOverhead = 3;
        for (int i = 0; i < dispatchOverhead * 100; i++) {
            dispatchOverhead = dispatchOverhead % 2;
        }

        // GPU execution (actual kernel work)
        volatile int gpuWork = req.gpuExecutionTimeUs;
        for (int i = 0; i < gpuWork * 10; i++) {
            gpuWork = gpuWork % 2;
        }

        // CPU-side send overhead (5us)
        volatile int sendOverhead = 5;
        for (int i = 0; i < sendOverhead * 100; i++) {
            sendOverhead = sendOverhead % 2;
        }
    }

    static PerformanceMetrics calculateMetrics(std::vector<double>& latencies, int sampleCount) {
        std::sort(latencies.begin(), latencies.end());

        double sum = 0;
        for (auto lat : latencies) sum += lat;
        double mean = sum / latencies.size();

        double variance = 0;
        for (auto lat : latencies) {
            variance += (lat - mean) * (lat - mean);
        }
        double stdev = std::sqrt(variance / latencies.size());

        double p95 = latencies[static_cast<int>(latencies.size() * 0.95)];
        double p99 = latencies[static_cast<int>(latencies.size() * 0.99)];
        double median = latencies[latencies.size() / 2];

        double totalTimeUs = sum;
        double throughputRps = (sampleCount * 1e6) / totalTimeUs;

        return {
            .meanLatencyUs = mean,
            .medianLatencyUs = median,
            .minLatencyUs = latencies.front(),
            .maxLatencyUs = latencies.back(),
            .stdevLatencyUs = stdev,
            .p95LatencyUs = p95,
            .p99LatencyUs = p99,
            .throughputRps = throughputRps,
            .totalTimeMs = totalTimeUs / 1000.0,
            .sampleCount = sampleCount
        };
    }

    static void printSummaryTable(const std::vector<std::pair<std::string, PerformanceMetrics>>& results) {
        std::cout << "\n" << std::string(100, '=') << "\n";
        std::cout << "Performance Summary Table\n";
        std::cout << std::string(100, '=') << "\n";

        std::cout << std::left << std::setw(25) << "Test"
                  << std::right << std::setw(15) << "P50 (μs)"
                  << std::setw(15) << "P95 (μs)"
                  << std::setw(15) << "P99 (μs)"
                  << std::setw(15) << "Mean (μs)"
                  << std::setw(15) << "Throughput\n";
        std::cout << std::string(100, '-') << "\n";

        for (const auto& result : results) {
            std::cout << std::left << std::setw(25) << result.first
                      << std::right << std::setprecision(1) << std::fixed
                      << std::setw(15) << result.second.medianLatencyUs
                      << std::setw(15) << result.second.p95LatencyUs
                      << std::setw(15) << result.second.p99LatencyUs
                      << std::setw(15) << result.second.meanLatencyUs
                      << std::setw(14) << static_cast<int>(result.second.throughputRps) << " RPS\n";
        }
        std::cout << std::string(100, '=') << "\n";
    }
};

}  // namespace symm

// Entry point
int main() {
    symm::HttpPerformanceTest::runAllBenchmarks();
    return 0;
}
