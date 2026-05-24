#pragma once
#include <string>
#include <set>
#include "parser/SymmParser.h"
#include "runtime/GpuServer.h"

namespace symm {

// Walks the Symmetra AST and emits CUDA C++ source code.
// The emitted code is JIT-compiled by NVRTC in NvrtcCompiler.
//
// Key mappings:
//   async (ctx) => { ... }    →  __device__ void handler_N(SymmCtx* ctx)
//   data.map(x => expr)       →  __global__ void map_kernel_N(float*, float*, int)
//   Math.sin / Math.cos ...   →  sinf / cosf (CUDA intrinsics)
//   ctx.json(val)             →  symm_json_str(ctx, ...)
//   gpu.fingerprint(key)      →  symm_fingerprint(key, len)
class Codegen {
public:
    explicit Codegen(const ServerOptions& opts);

    // Emit complete CUDA C++ source from the parsed module.
    std::string emit(const Module& mod);

    // Emit the prelude + classes/functions (shared across all handlers).
    std::string emitShared(const Module& mod);

    // Emit the CUDA body for a single handler (used by retry logic in main.cpp).
    std::string emitHandlerCode(const RouteReg& route, int idx);

    // Emit dispatch + route table suffix (must come after all handlers).
    std::string emitSuffix(const std::vector<RouteReg>& routes);

private:
    const ServerOptions& _opts;
    int   _kernelIdx  = 0;
    int   _handlerIdx = 0;
    std::string _out;

    void emitPrelude();
    void emitHandler(const RouteReg& route, int idx);
    void emitDispatch(const std::vector<RouteReg>& routes);
    void emitRouteTable(const std::vector<RouteReg>& routes);

    // Expression codegen — returns C++ expression string
    std::string emitExpr(const NodePtr& node);
    std::string emitBinary(const BinaryExpr& b);
    std::string emitCall(const CallExpr& c);
    std::string emitMember(const MemberExpr& m);
    std::string emitMapKernel(const MapCall& m, const std::string& inputVar);
    std::string emitLiteral(const Literal& l);

    // Statement codegen — appends to _out
    void emitStmt(const NodePtr& node, int indent);
    void emitBlock(const Block& b, int indent);
    void emitClass(const ClassDecl& cls, int depth);
    void emitTemplateLit(const TemplateLit& t, int depth, const std::string& bufVar);

    // Helpers
    std::string indent(int n) { return std::string(n * 4, ' '); }
    std::string freshKernel() { return "symm_map_k" + std::to_string(_kernelIdx++); }
    std::string freshTemp()   { return "_t" + std::to_string(_tmpIdx++); }
    std::string mathRemap(const std::string& name);

    int _tmpIdx = 0;
    std::set<std::string> _ctxBodyVars;  // _doN vars that came from ctx.json() body reads

    // Route context — set before emitting each handler; used for param extraction.
    std::string _currentRoutePath;

    // Extract the zero-based segment index of a named param in a route pattern.
    // Returns -1 if not found.
    // e.g. "/users/:id/posts/:pid" → paramSegIndex("id") == 1
    static int paramSegIndex(const std::string& pattern, const std::string& paramName) {
        int seg = 0;
        size_t i = (pattern[0] == '/') ? 1 : 0;
        while (i <= pattern.size()) {
            size_t slash = pattern.find('/', i);
            if (slash == std::string::npos) slash = pattern.size();
            std::string tok = pattern.substr(i, slash - i);
            if (!tok.empty() && tok[0] == ':' && tok.substr(1) == paramName) return seg;
            seg++;
            i = slash + 1;
        }
        return -1;
    }
};

} // namespace symm
