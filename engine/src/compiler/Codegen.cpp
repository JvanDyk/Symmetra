#include "Codegen.h"
#include <map>
#include <stdexcept>

namespace symm {

// Maps JS Math.* names to CUDA single-precision intrinsics
static const std::map<std::string, std::string> MATH_REMAP = {
    {"sin","sinf"},   {"cos","cosf"},   {"tan","tanf"},
    {"asin","asinf"}, {"acos","acosf"}, {"atan","atanf"}, {"atan2","atan2f"},
    {"sqrt","sqrtf"}, {"cbrt","cbrtf"}, {"abs","fabsf"},  {"fabs","fabsf"},
    {"ceil","ceilf"}, {"floor","floorf"},{"round","roundf"},{"trunc","truncf"},
    {"exp","expf"},   {"exp2","exp2f"}, {"log","logf"},   {"log2","log2f"},
    {"log10","log10f"},{"pow","powf"},  {"hypot","hypotf"},{"fmod","fmodf"},
    {"min","fminf"},  {"max","fmaxf"},  {"PI","3.14159265358979323846f"},
    {"E","2.71828182845904523536f"},
};

static const char* PRELUDE = R"(
// ── Symmetra GPU Runtime Prelude ──────────────────────────────────────────────
#include <math.h>
#include <stdint.h>
#include <string.h>

// Request and response structures (in VRAM)
struct SymmCtx {
    // Request fields
    const char* method;       // "GET", "POST", ...
    const char* path;         // URL path (null-terminated in VRAM)
    const char* query;        // Query string
    const char* body;         // Raw body bytes
    int         body_len;
    // Response fields (written by handler)
    char*       resp_buf;     // Response body output buffer
    int         resp_len;
    int         resp_status;  // HTTP status code
    // GPU fingerprint from TM engine
    uint64_t    fingerprint;
};

// Route descriptor (registered at startup)
struct SymmRoute {
    const char* method;
    const char* path;
    int         handler_id;
};

// ── GPU-side utility functions ─────────────────────────────────────────────────

__device__ int symm_strlen(const char* s) {
    int n = 0;
    while (s && s[n]) ++n;
    return n;
}

__device__ void symm_strcpy(char* dst, const char* src, int maxlen) {
    int i = 0;
    while (src && src[i] && i < maxlen - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Write a JSON string response into ctx->resp_buf
__device__ void symm_json_str(SymmCtx* ctx, const char* json) {
    int len = symm_strlen(json);
    symm_strcpy(ctx->resp_buf, json, 65536);
    ctx->resp_len    = len;
    ctx->resp_status = 200;
}

// Write a float array as JSON array
__device__ void symm_json_f32arr(SymmCtx* ctx, const float* arr, int n) {
    char* p = ctx->resp_buf;
    int cap = 65536;
    int off = 0;
    p[off++] = '[';
    for (int i = 0; i < n && off < cap - 20; i++) {
        if (i > 0) p[off++] = ',';
        // Simple float-to-string (limited precision for GPU)
        float v = arr[i];
        int iv = (int)v;
        int frac = (int)((v - iv) * 1000000);
        if (frac < 0) frac = -frac;
        // itoa-like
        if (iv < 0) { p[off++] = '-'; iv = -iv; }
        char tmp[20]; int tlen = 0;
        if (iv == 0) tmp[tlen++] = '0';
        while (iv > 0) { tmp[tlen++] = '0' + (iv % 10); iv /= 10; }
        for (int j = tlen-1; j >= 0; j--) p[off++] = tmp[j];
        if (frac > 0) {
            p[off++] = '.';
            char ft[10]; int fl = 0;
            int ff = frac;
            while (ff > 0) { ft[fl++] = '0' + (ff % 10); ff /= 10; }
            for (int j = fl-1; j >= 0; j--) p[off++] = ft[j];
        }
    }
    p[off++] = ']';
    p[off]   = '\0';
    ctx->resp_len    = off;
    ctx->resp_status = 200;
}

// Write a simple key-value JSON object with a string field
__device__ void symm_json_obj_str(SymmCtx* ctx, const char* key, const char* val) {
    char* p = ctx->resp_buf;
    int off = 0;
    p[off++] = '{'; p[off++] = '"';
    for (int i = 0; key[i]; i++) p[off++] = key[i];
    p[off++] = '"'; p[off++] = ':'; p[off++] = '"';
    for (int i = 0; val[i]; i++) p[off++] = val[i];
    p[off++] = '"'; p[off++] = '}'; p[off] = '\0';
    ctx->resp_len    = off;
    ctx->resp_status = 200;
}

// APEX fingerprint (RAYK 512x — called as device function via function pointer)
__device__ uint64_t symm_fingerprint(const char* data, int len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}
// ─────────────────────────────────────────────────────────────────────────────
)";

Codegen::Codegen(const ServerOptions& opts) : _opts(opts) {}

std::string Codegen::emit(const Module& mod) {
    _out.clear();
    _kernelIdx  = 0;
    _handlerIdx = 0;

    _out += PRELUDE;
    _out += "\n";

    // Emit a __global__ handler kernel for each registered route
    for (int i = 0; i < (int)mod.routes.size(); i++) {
        emitHandler(mod.routes[i], i);
    }

    // Emit the dispatcher kernel (one per request on GPU)
    emitDispatch(mod.routes);

    // Emit the route matching table (used by GpuRouter on CPU side)
    emitRouteTable(mod.routes);

    return _out;
}

// ── Handler emission ──────────────────────────────────────────────────────────

void Codegen::emitHandler(const RouteReg& route, int idx) {
    _out += "// Handler: " + route.method + " " + route.path + "\n";
    _out += "__device__ void symm_handler_" + std::to_string(idx) + "(SymmCtx* ctx) {\n";

    if (route.handler && route.handler->kind == NodeKind::FuncDecl) {
        auto& fn = std::get<FuncDecl>(route.handler->data);
        // The handler receives a SymmCtx* — its first param is 'ctx'
        if (fn.body && fn.body->kind == NodeKind::Block) {
            emitBlock(std::get<Block>(fn.body->data), 1);
        }
    }

    _out += "}\n\n";
}

void Codegen::emitDispatch(const std::vector<RouteReg>& routes) {
    _out += "// Dispatch kernel: routes each request to the appropriate handler\n";
    _out += "__global__ void symm_dispatch(SymmCtx* ctx, int route_id, int n_requests) {\n";
    _out += "    int tid = blockIdx.x * blockDim.x + threadIdx.x;\n";
    _out += "    if (tid >= n_requests) return;\n";
    _out += "    SymmCtx* c = &ctx[tid];\n";
    _out += "    switch (route_id) {\n";
    for (int i = 0; i < (int)routes.size(); i++) {
        _out += "        case " + std::to_string(i) + ": symm_handler_" + std::to_string(i) + "(c); break;\n";
    }
    _out += "        default:\n";
    _out += "            symm_json_obj_str(c, \"error\", \"404 Not Found\");\n";
    _out += "            c->resp_status = 404;\n";
    _out += "    }\n";
    _out += "}\n\n";
}

void Codegen::emitRouteTable(const std::vector<RouteReg>& routes) {
    _out += "// Route count (read by host to build GpuRouter table)\n";
    _out += "__device__ __constant__ int symm_route_count = " + std::to_string(routes.size()) + ";\n\n";
}

// ── Statement emission ────────────────────────────────────────────────────────

void Codegen::emitStmt(const NodePtr& node, int depth) {
    if (!node) return;
    std::string ind = indent(depth);

    switch (node->kind) {
        case NodeKind::ReturnStmt: {
            auto& r = std::get<ReturnStmt>(node->data);
            if (r.expr) {
                // return ctx.json(...) is handled as a side-effect call
                if (r.expr->kind == NodeKind::CallExpr) {
                    auto expr = emitExpr(r.expr);
                    _out += ind + expr + ";\n";
                } else {
                    _out += ind + "return " + emitExpr(r.expr) + ";\n";
                }
            } else {
                _out += ind + "return;\n";
            }
            break;
        }
        case NodeKind::ConstDecl:
        case NodeKind::ExprStmt: {
            std::string expr;
            if (node->kind == NodeKind::ConstDecl) {
                auto& d = std::get<ConstDecl>(node->data);
                // Detect map kernel — needs special treatment
                if (d.init && d.init->kind == NodeKind::MapCall) {
                    auto& mc = std::get<MapCall>(d.init->data);
                    std::string arrayVar = emitExpr(mc.array);
                    std::string kname = emitMapKernel(mc, arrayVar);
                    _out += ind + "// Map kernel: " + kname + "\n";
                    _out += ind + "float* " + d.name + " = (float*)ctx->resp_buf;\n";
                    _out += ind + "// NOTE: map kernel launched by host after handler returns\n";
                } else {
                    std::string val = d.init ? emitExpr(d.init) : "0";
                    _out += ind + "auto " + d.name + " = " + val + ";\n";
                }
            } else {
                auto& es = std::get<ExprStmt>(node->data);
                expr = emitExpr(es.expr);
                _out += ind + expr + ";\n";
            }
            break;
        }
        case NodeKind::IfStmt: {
            auto& is = std::get<IfStmt>(node->data);
            _out += ind + "if (" + emitExpr(is.cond) + ") ";
            if (is.then && is.then->kind == NodeKind::Block) {
                _out += "{\n";
                emitBlock(std::get<Block>(is.then->data), depth + 1);
                _out += ind + "}";
            } else {
                _out += "\n";
                emitStmt(is.then, depth + 1);
            }
            if (is.else_) {
                _out += " else {\n";
                emitStmt(is.else_, depth + 1);
                _out += ind + "}";
            }
            _out += "\n";
            break;
        }
        case NodeKind::Block:
            emitBlock(std::get<Block>(node->data), depth);
            break;
        default:
            _out += ind + emitExpr(node) + ";\n";
            break;
    }
}

void Codegen::emitBlock(const Block& b, int depth) {
    for (auto& s : b.stmts) emitStmt(s, depth);
}

// ── Expression emission ───────────────────────────────────────────────────────

std::string Codegen::emitExpr(const NodePtr& node) {
    if (!node) return "/*null*/";

    switch (node->kind) {
        case NodeKind::Literal:
            return emitLiteral(std::get<Literal>(node->data));

        case NodeKind::Ident: {
            auto& id = std::get<Ident>(node->data);
            // Remap Math object → nothing (handled in MemberExpr)
            return id.name;
        }

        case NodeKind::BinaryExpr:
            return emitBinary(std::get<BinaryExpr>(node->data));

        case NodeKind::UnaryExpr: {
            auto& u = std::get<UnaryExpr>(node->data);
            return u.op + emitExpr(u.expr);
        }

        case NodeKind::AwaitExpr: {
            // await in Symmetra = synchronize on GPU stream (handled by host)
            // Inside a handler, await ctx.body() just returns ctx->body
            auto& aw = std::get<AwaitExpr>(node->data);
            return emitExpr(aw.expr);  // strip await — GPU is synchronous
        }

        case NodeKind::MemberExpr:
            return emitMember(std::get<MemberExpr>(node->data));

        case NodeKind::CallExpr:
            return emitCall(std::get<CallExpr>(node->data));

        case NodeKind::MapCall: {
            auto& mc = std::get<MapCall>(node->data);
            return emitMapKernel(mc, emitExpr(mc.array));
        }

        case NodeKind::NewExpr: {
            auto& ne = std::get<NewExpr>(node->data);
            std::string args;
            for (int i = 0; i < (int)ne.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(ne.args[i]);
            }
            return ne.className + "(" + args + ")";
        }

        case NodeKind::ArrayExpr: {
            auto& ae = std::get<ArrayExpr>(node->data);
            std::string s = "{";
            for (int i = 0; i < (int)ae.elements.size(); i++) {
                if (i) s += ", ";
                s += emitExpr(ae.elements[i]);
            }
            return s + "}";
        }

        case NodeKind::ObjectExpr: {
            // Object literals in GPU code → inline struct init or JSON write
            auto& oe = std::get<ObjectExpr>(node->data);
            // Emit as comment + JSON string for simple cases
            std::string json = "{";
            for (int i = 0; i < (int)oe.props.size(); i++) {
                if (i) json += ",";
                json += "\\\"" + oe.props[i].first + "\\\":" + emitExpr(oe.props[i].second);
            }
            json += "}";
            return "\"" + json + "\"";
        }

        default:
            return "/*unsupported*/0";
    }
}

std::string Codegen::emitLiteral(const Literal& l) {
    switch (l.kind) {
        case Literal::Int:   return l.raw;
        case Literal::Float: return l.raw + "f";
        case Literal::Str:   return "\"" + l.raw + "\"";
        case Literal::Bool:  return l.raw;  // true/false are valid in CUDA C++
        case Literal::Null:  return "nullptr";
    }
    return "0";
}

std::string Codegen::emitBinary(const BinaryExpr& b) {
    std::string op = b.op;
    // JS strict equality → C++ equality
    if (op == "===" || op == "==") op = "==";
    if (op == "!==" || op == "!=") op = "!=";
    return "(" + emitExpr(b.left) + " " + op + " " + emitExpr(b.right) + ")";
}

std::string Codegen::emitMember(const MemberExpr& m) {
    std::string obj = emitExpr(m.object);

    // Math.X → CUDA intrinsic
    if (obj == "Math") {
        auto it = MATH_REMAP.find(m.prop);
        if (it != MATH_REMAP.end()) return it->second;
        return m.prop + "f";
    }

    // ctx.json(val) → symm_json_str (handled in CallExpr)
    // ctx.params.X → ctx->params[X] (simplified — returns query param)
    if (obj == "ctx" && m.prop == "params") return "ctx->query";
    if (obj == "ctx" && m.prop == "body")   return "ctx->body";
    if (obj == "ctx" && m.prop == "json")   return "symm_json_str";

    return obj + "->" + m.prop;
}

std::string Codegen::emitCall(const CallExpr& c) {
    // Special case: ctx.json({...}) or ctx.json(val)
    if (c.callee && c.callee->kind == NodeKind::MemberExpr) {
        auto& mem = std::get<MemberExpr>(c.callee->data);
        std::string obj  = emitExpr(mem.object);
        std::string prop = mem.prop;

        // ctx.json(val) → symm_json_str(ctx, val)
        if (obj == "ctx" && prop == "json") {
            if (!c.args.empty()) {
                return "symm_json_str(ctx, " + emitExpr(c.args[0]) + ")";
            }
            return "symm_json_str(ctx, \"\")";
        }

        // ctx.body<Float32Array>() → (float*)ctx->body, ctx->body_len
        if (obj == "ctx" && prop == "body") {
            return "(float*)ctx->body";
        }

        // gpu.fingerprint(key) → symm_fingerprint(key, len)
        if (obj == "gpu" && prop == "fingerprint") {
            std::string arg = c.args.empty() ? "ctx->path" : emitExpr(c.args[0]);
            return "symm_fingerprint(" + arg + ", symm_strlen(" + arg + "))";
        }

        // Math.sin(x) etc. — callee is Math.sin, args is [x]
        if (obj == "Math") {
            auto it = MATH_REMAP.find(prop);
            std::string fn = (it != MATH_REMAP.end()) ? it->second : prop + "f";
            std::string args;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(c.args[i]);
            }
            return fn + "(" + args + ")";
        }

        // Generic method call
        std::string args;
        for (int i = 0; i < (int)c.args.size(); i++) {
            if (i) args += ", ";
            args += emitExpr(c.args[i]);
        }
        return obj + "->" + prop + "(" + args + ")";
    }

    // Generic function call
    std::string fn = emitExpr(c.callee);
    std::string args;
    for (int i = 0; i < (int)c.args.size(); i++) {
        if (i) args += ", ";
        args += emitExpr(c.args[i]);
    }
    return fn + "(" + args + ")";
}

// Emit a named __global__ map kernel and return its name.
// The kernel is appended to _out.
std::string Codegen::emitMapKernel(const MapCall& mc, const std::string& inputVar) {
    std::string kname = freshKernel();

    // Extract lambda parameter and body
    std::string paramName = "x";
    std::string bodyExpr;

    if (mc.fn && mc.fn->kind == NodeKind::FuncDecl) {
        auto& fn = std::get<FuncDecl>(mc.fn->data);
        if (!fn.params.empty()) paramName = fn.params[0];
        if (fn.body) bodyExpr = emitExpr(fn.body);
    }

    // Emit the __global__ kernel
    _out += "// Map kernel for: " + inputVar + ".map(" + paramName + " => ...)\n";
    _out += "__global__ void " + kname + "(const float* in, float* out, int n) {\n";
    _out += "    int tid = blockIdx.x * blockDim.x + threadIdx.x;\n";
    _out += "    if (tid >= n) return;\n";
    _out += "    float " + paramName + " = in[tid];\n";
    _out += "    out[tid] = " + bodyExpr + ";\n";
    _out += "}\n\n";

    return kname;
}

std::string Codegen::mathRemap(const std::string& name) {
    auto it = MATH_REMAP.find(name);
    return it != MATH_REMAP.end() ? it->second : name;
}

} // namespace symm
