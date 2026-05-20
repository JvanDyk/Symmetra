#include "EsbuildBridge.h"
#include <cstdio>
#include <cstdlib>
#include <array>
#include <stdexcept>
#include <filesystem>

#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
  #define NULL_DEV "NUL"
#else
  #define POPEN  popen
  #define PCLOSE pclose
  #define NULL_DEV "/dev/null"
#endif

namespace symm {

std::string EsbuildBridge::findEsbuild() {
    // 1. Check next to the symmetra binary (bundled esbuild)
    std::filesystem::path self;
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    self = buf;
#else
    self = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto bundled = self.parent_path() / "esbuild"
#ifdef _WIN32
        += ".exe"
#endif
        ;
    if (std::filesystem::exists(bundled)) {
        return bundled.string();
    }

    // 2. Fall back to PATH
#ifdef _WIN32
    int r = std::system("where esbuild > NUL 2>&1");
#else
    int r = std::system("which esbuild > /dev/null 2>&1");
#endif
    if (r == 0) return "esbuild";

    return "";
}

std::string EsbuildBridge::transform(const std::string& tsFilePath) {
    std::string esbuild = findEsbuild();
    if (esbuild.empty()) {
        std::fprintf(stderr,
            "symmetra: esbuild not found.\n"
            "  Install: npm install -g esbuild\n"
            "  Or place the esbuild binary next to the symmetra executable.\n");
        return {};
    }

    // Build the command:
    // esbuild <file> --bundle=false --platform=neutral --target=es2022 --format=esm
    // esbuild writes transformed JS to stdout.
    std::string cmd = "\"" + esbuild + "\" "
        + "\"" + tsFilePath + "\" "
        + "--bundle=false "
        + "--platform=neutral "
        + "--target=es2022 "
        + "--format=esm "
        + "2>" + std::string(NULL_DEV);

    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) {
        std::fprintf(stderr, "symmetra: failed to launch esbuild\n");
        return {};
    }

    std::string result;
    std::array<char, 8192> chunk{};
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe)) {
        result += chunk.data();
    }

    int status = PCLOSE(pipe);
    if (status != 0) {
        std::fprintf(stderr, "symmetra: esbuild exited with error (status %d)\n", status);
        std::fprintf(stderr, "  Check syntax errors in: %s\n", tsFilePath.c_str());
        return {};
    }

    return result;
}

} // namespace symm
