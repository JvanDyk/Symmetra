#include "src/compiler/Codegen.h"
#include "src/parser/SymmParser.h"
#include <iostream>
#include <string>
int main() {
    symm::SymmParser p;
    auto mod = p.parse(
        "app.get(\"/t\", (ctx) => {\n"
        "  const t = typeof ctx.query.n;\n"
        "  ctx.json({ t });\n"
        "});"
    );
    symm::ServerOptions o; o.port=8080; o.verbose=false;
    symm::Codegen cg(o);
    auto src = cg.emit(mod);
    // Find and print the handler
    size_t h = src.find("symm_handler_0");
    if (h != std::string::npos) {
        std::cout << "=== HANDLER ===\n" << src.substr(h, 500) << "\n===END===\n";
    }
    // Check for patterns
    auto f1 = src.find("\"string\"");
    auto f2 = src.find("auto t");
    auto f3 = src.find("typeof");
    std::cout << "\"string\" at: " << (f1 == std::string::npos ? -1LL : (long long)f1) << "\n";
    std::cout << "auto t at: " << (f2 == std::string::npos ? -1LL : (long long)f2) << "\n";
    std::cout << "typeof at: " << (f3 == std::string::npos ? -1LL : (long long)f3) << "\n";
    return 0;
}
