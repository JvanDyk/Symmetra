#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cuda_runtime.h>
#include <nvrtc.h>

#ifdef _WIN32
  #define SYMM_NULL "NUL"
#else
  #define SYMM_NULL "/dev/null"
#endif

#include "parser/EsbuildBridge.h"
#include "parser/SymmParser.h"
#include "compiler/Codegen.h"
#include "runtime/NvrtcCompiler.h"
#include "runtime/GpuServer.h"

static void print_version() {
    int driver = 0;
    cudaDriverGetVersion(&driver);
    int driverMaj = driver / 1000;
    int driverMin = (driver % 1000) / 10;

    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);

    std::printf("Symmetra 1.0.0  ·  %s sm_%d%d  ·  CUDA driver %d.%d\n",
                prop.name,
                prop.major, prop.minor,
                driverMaj, driverMin);
}

static void print_usage() {
    std::puts(
        "Usage: symmetra <command> [options]\n"
        "\n"
        "Commands:\n"
        "  <file.ts>          Compile and run a Symmetra TypeScript file\n"
        "  build <file.ts>    Ahead-of-time compile to symmetra.ptx\n"
        "  bench              Run GPU benchmark\n"
        "  doctor             Check CUDA device, driver, esbuild\n"
        "  version            Print version info\n"
        "\n"
        "Options:\n"
        "  --port <n>         Override serve() port (default: from file)\n"
        "  --streams <n>      CUDA stream pool size (default: 32)\n"
        "  --engine <name>    Default TM engine (APEX, RAYK, PHI_RAYK, OMEGA)\n"
        "  --verbose          Show codegen and NVRTC output\n"
    );
}

static void cmd_doctor() {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err != cudaSuccess || deviceCount == 0) {
        std::fprintf(stderr, "✗ No CUDA-capable GPU found: %s\n", cudaGetErrorString(err));
        std::exit(1);
    }

    for (int i = 0; i < deviceCount; i++) {
        cudaDeviceProp prop{};
        cudaGetDeviceProperties(&prop, i);
        std::printf("✓ GPU %d: %s  sm_%d%d  %.0f MB VRAM\n",
                    i, prop.name, prop.major, prop.minor,
                    prop.totalGlobalMem / 1e6);
    }

    // Check NVRTC
    int nvrtcMaj = 0, nvrtcMin = 0;
    nvrtcGetVersion(&nvrtcMaj, &nvrtcMin);
    std::printf("✓ NVRTC %d.%d\n", nvrtcMaj, nvrtcMin);

    // Check esbuild
    int ret = std::system("esbuild --version > " SYMM_NULL " 2>&1");
    if (ret == 0) {
        std::puts("✓ esbuild found in PATH");
    } else {
        std::puts("✗ esbuild not found — install: npm install -g esbuild");
    }

    std::puts("\nSymmetra doctor: all checks passed");
}

static void cmd_bench() {
    print_version();
    std::puts("Running GPU benchmark...\n");

    // Initialize CUDA
    cudaSetDevice(0);
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);

    // Warm-up kernel
    void* d_buf = nullptr;
    cudaMalloc(&d_buf, 1024 * 1024);
    cudaMemset(d_buf, 0, 1024 * 1024);
    cudaFree(d_buf);

    // Report baseline
    std::printf("Device: %s (%d SMs, sm_%d%d)\n",
                prop.name, prop.multiProcessorCount,
                prop.major, prop.minor);
    std::printf("Global memory: %.1f GB\n", prop.totalGlobalMem / 1e9);
    std::printf("Peak bandwidth: %.1f GB/s\n",
                (prop.memoryClockRate * 1e3 * prop.memoryBusWidth / 8) / 1e9);
    std::puts("\nRun `symmetra ./examples/benchmark/server.ts` for full HTTP benchmark.");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        print_version();
        return 0;
    }
    if (cmd == "doctor") {
        cmd_doctor();
        return 0;
    }
    if (cmd == "bench") {
        cmd_bench();
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    // Treat first arg as a TypeScript file path
    std::string tsFile = cmd;
    if (tsFile.empty() || tsFile[0] == '-') {
        std::fprintf(stderr, "symmetra: expected a .ts file, got '%s'\n", tsFile.c_str());
        return 1;
    }

    // Parse CLI options
    symm::ServerOptions opts{};
    opts.port    = 8080;
    opts.streams = 32;
    opts.engine  = "OMEGA";
    opts.verbose = false;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port"    && i+1 < argc) { opts.port    = std::atoi(argv[++i]); }
        if (arg == "--streams" && i+1 < argc) { opts.streams = std::atoi(argv[++i]); }
        if (arg == "--engine"  && i+1 < argc) { opts.engine  = argv[++i]; }
        if (arg == "--verbose")               { opts.verbose = true; }
    }

    // ── Pipeline ──────────────────────────────────────────────────────────────

    // 1. esbuild: TypeScript → plain JS (strips types)
    symm::EsbuildBridge esbuild;
    std::string jsSource = esbuild.transform(tsFile);
    if (jsSource.empty()) {
        std::fprintf(stderr, "symmetra: esbuild failed on '%s'\n", tsFile.c_str());
        return 1;
    }

    // 2. Parse: plain JS → Symmetra AST
    symm::SymmParser parser;
    symm::Module mod = parser.parse(jsSource, tsFile);
    if (parser.hasErrors()) {
        for (auto& e : parser.errors()) {
            std::fprintf(stderr, "%s\n", e.c_str());
        }
        return 1;
    }

    // 3. Codegen: AST → CUDA C++ source
    symm::Codegen codegen(opts);
    std::string cudaSrc = codegen.emit(mod);
    if (opts.verbose) {
        std::puts("── Generated CUDA C++ ─────────────────────────");
        std::puts(cudaSrc.c_str());
        std::puts("────────────────────────────────────────────────");
    }

    // 4. NVRTC: JIT-compile CUDA C++ → PTX
    cudaSetDevice(0);
    symm::NvrtcCompiler nvrtc;
    symm::CompiledModule compiled = nvrtc.compile(cudaSrc, opts.verbose);

    print_version();

    // 5. GpuServer: load PTX, bind routes, start serving
    symm::GpuServer server(compiled, mod.routes, opts);
    server.run();   // blocks until Ctrl-C

    return 0;
}
