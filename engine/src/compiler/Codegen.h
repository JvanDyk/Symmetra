#pragma once
#include <string>
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
//   ctx.json(val)             →  symm_json_write(ctx, ...)
//   gpu.fingerprint(key)      →  symm_rayk_fingerprint(key)
class Codegen {
public:
    explicit Codegen(const ServerOptions& opts);

    // Emit complete CUDA C++ source from the parsed module.
    std::string emit(const Module& mod);

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

    // Helpers
    std::string indent(int n) { return std::string(n * 4, ' '); }
    std::string freshKernel() { return "symm_map_k" + std::to_string(_kernelIdx++); }
    std::string mathRemap(const std::string& name);
};

} // namespace symm
