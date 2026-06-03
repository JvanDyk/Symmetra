#pragma once
#include <string>
#include <vector>

// Minimal stubs for compilation

struct Module {
    std::string name;
};

class SymmParser {
public:
    Module parse(const std::string& code) {
        Module m;
        m.name = code;
        return m;
    }
};

struct CodegenOptions {
    bool optimize = true;
};

inline CodegenOptions defaultOpts() {
    return CodegenOptions();
}
