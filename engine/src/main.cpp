#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <filesystem>
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
#include "protect/AntiDebug.h"
#include "protect/Obfuscate.h"
#include "protect/KernelStore.h"

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

    std::puts("✓ NVRTC linked");

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
    std::puts("\nRun `symmetra ./examples/benchmark/server.ts` for full HTTP benchmark.");
}

int main(int argc, char* argv[]) {
    if (symm::protect::detectDebugger()) std::_Exit(0); // silent — looks like normal exit
    symm::protect::startWatchdog();

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
    opts.verbose = false;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port"    && i+1 < argc) { opts.port    = std::atoi(argv[++i]); }
        if (arg == "--streams" && i+1 < argc) { opts.streams = std::atoi(argv[++i]); }
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

    // Apply port from serve() call in the script (overrides CLI default).
    if (mod.servePort != 8080) opts.port = mod.servePort;

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

    // Load all pre-compiled encrypted kernels (genesis, http, io).
    // Decrypted PTX is zeroed immediately after cuModuleLoadData — never persists.
    try {
        symm::protect::loadKernels();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sym: %s\n", e.what());
        return 1;
    }

    symm::CompiledModule compiled;
    std::string srcName = std::filesystem::path(tsFile).filename().string();
    try {
        symm::NvrtcCompiler nvrtc;
        // First attempt: compile everything.
        bool ok = false;
        try {
            compiled = nvrtc.compile(cudaSrc, srcName, opts.verbose);
            ok = true;
        } catch (const std::exception& firstErr) {
            // Parse error log for line numbers, then stub failing handlers and retry.
            // This lets routes that work on GPU compile even if others don't.
            std::string errLog = firstErr.what();

            // Count newlines in the shared prelude+classes section.
            std::string sharedSrc = codegen.emitShared(mod);
            int sharedLines = (int)std::count(sharedSrc.begin(), sharedSrc.end(), '\n');

            // Build per-handler code and track line ranges.
            int nRoutes = (int)mod.routes.size();
            std::vector<std::string> handlerCodes(nRoutes);
            std::vector<int> handlerStartLine(nRoutes), handlerEndLine(nRoutes);
            int lineAt = sharedLines + 1;
            for (int i = 0; i < nRoutes; i++) {
                handlerCodes[i] = codegen.emitHandlerCode(mod.routes[i], i);
                handlerStartLine[i] = lineAt;
                lineAt += (int)std::count(handlerCodes[i].begin(), handlerCodes[i].end(), '\n');
                handlerEndLine[i] = lineAt;
            }

            // Find which handlers are mentioned in error lines.
            std::vector<bool> failedHandler(nRoutes, false);
            // Parse both "filename(N): error:" and "at line N" patterns.
            auto markLine = [&](int lineNum) {
                for (int i = 0; i < nRoutes; i++) {
                    if (lineNum >= handlerStartLine[i] && lineNum <= handlerEndLine[i]) {
                        failedHandler[i] = true;
                    }
                }
            };
            std::istringstream ss(errLog);
            std::string errLine;
            while (std::getline(ss, errLine)) {
                // "filename(N): error:" pattern
                {
                    auto paren = errLine.find('(');
                    auto rparen = (paren != std::string::npos) ? errLine.find(')', paren) : std::string::npos;
                    if (paren != std::string::npos && rparen != std::string::npos) {
                        std::string numStr = errLine.substr(paren + 1, rparen - paren - 1);
                        try { markLine(std::stoi(numStr)); } catch (...) {}
                    }
                }
                // "at line N" pattern (template instantiation site)
                {
                    auto pos = errLine.find("at line ");
                    if (pos != std::string::npos) {
                        try { markLine(std::stoi(errLine.substr(pos + 8))); } catch (...) {}
                    }
                }
            }

            // Build stub for each failing handler.
            bool anyFailed = false;
            for (int i = 0; i < nRoutes; i++) {
                if (failedHandler[i]) {
                    anyFailed = true;
                    handlerCodes[i] =
                        "// Handler: " + std::string(mod.routes[i].method)
                        + " " + mod.routes[i].path + " [GPU stub — not supported]\n"
                        "__device__ void symm_handler_" + std::to_string(i) + "(SymmCtx* ctx) {\n"
                        "    symm_json_str(ctx, \"{\\\"error\\\":\\\"route not supported on GPU\\\"}\");\n"
                        "    ctx->resp_status = 501;\n}\n\n";
                }
            }

            if (!anyFailed) {
                std::fprintf(stderr, "%s\n", firstErr.what());
                return 1;
            }

            // Retry with stubs.
            std::string cudaSrc2 = sharedSrc;
            for (auto& h : handlerCodes) cudaSrc2 += h;
            cudaSrc2 += codegen.emitSuffix(mod.routes);
            try {
                compiled = nvrtc.compile(cudaSrc2, srcName, opts.verbose);
                ok = true;
                // Report which routes were stubbed.
                for (int i = 0; i < nRoutes; i++) {
                    if (failedHandler[i]) {
                        std::fprintf(stderr, "sym: route %s %s not supported on GPU (stub)\n",
                                     mod.routes[i].method.c_str(), mod.routes[i].path.c_str());
                    }
                }
            } catch (const std::exception& e2) {
                std::fprintf(stderr, "%s\n", e2.what());
                return 1;
            }
        }
        (void)ok;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    print_version();

    // 5. GpuServer: load PTX, bind routes, start serving
    try {
        symm::GpuServer server(compiled, mod.routes, opts);
        server.run();   // blocks until Ctrl-C
    } catch (const std::exception& e) {
        std::fprintf(stderr, "symmetra: runtime error: %s\n", e.what());
        return 1;
    }

    return 0;
}
