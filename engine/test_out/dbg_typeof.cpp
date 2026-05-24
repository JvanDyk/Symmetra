#include "src/compiler/Codegen.h"
#include "src/parser/SymmParser.h"
#include <iostream>
int main() {
    symm::SymmParser p;
    auto mod = p.parse(
        "app.get('/t', (ctx) => {\n"
        "  const t = typeof ctx.query.n;\n"
        "  ctx.json({ t });\n"
        "});"
    );
    symm::ServerOptions o; o.port=8080; o.verbose=false;
    symm::Codegen cg(o);
    auto src = cg.emit(mod);
    auto pos = src.find("typeof");
    auto pos2 = src.find("\"string\"");
    auto pos3 = src.find("\"number\"");
    if (pos != std::string::npos) std::cout << "HAS typeof at " << pos << "\n";
    if (pos2 != std::string::npos) std::cout << "HAS \"string\" at " << pos2 << "\n";
    if (pos3 != std::string::npos) std::cout << "HAS \"number\" at " << pos3 << "\n";
    // Print the handler part
    auto hstart = src.find("symm_handler_0");
    if (hstart != std::string::npos) std::cout << src.substr(hstart, 400) << "\n";
    return 0;
}
