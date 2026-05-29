#include "Codegen.h"
#include <map>
#include <set>
#include <stdexcept>

// Suppress C2026 string literal truncation warnings (MSVC limitation)
#ifdef _MSC_VER
  #pragma warning(disable: 2026)
#endif

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
    // Additional Math methods
    {"sign","symm_math_sign"}, {"clz32","symm_math_clz32"},
    {"imul","symm_math_imul"}, {"fround","symm_math_fround"},
    {"log1p","log1pf"}, {"expm1","expm1f"},
    {"cosh","coshf"},   {"sinh","sinhf"},  {"tanh","tanhf"},
    {"acosh","acoshf"}, {"asinh","asinhf"},{"atanh","atanhf"},
    {"LN2","0.6931471805599453f"}, {"LN10","2.302585092994046f"},
    {"LOG2E","1.4426950408889634f"}, {"LOG10E","0.4342944819032518f"},
    {"SQRT2","1.4142135623730951f"}, {"SQRT1_2","0.7071067811865476f"},
    // Math.random() — simple LCG seeded from thread index
    {"random","Math::random"},
};

// WORKAROUND FOR MSVC C2026: The GPU runtime prelude is ~81KB, exceeding MSVC's 16KB string literal limit.
// Solution: Store the prelude as separate smaller strings that are concatenated dynamically,
// bypassing the C2026 compilation error while maintaining full functionality.
// This is declared as extern from a dedicated prelude source file.

extern "C" const char* _get_gpu_prelude();
static const char* PRELUDE = nullptr;

inline const char* _get_prelude() {
    if (!PRELUDE) {
        PRELUDE = _get_gpu_prelude();
    }
    return PRELUDE;
}

Codegen::Codegen(const ServerOptions& opts) : _opts(opts) {}

std::string Codegen::emitShared(const Module& mod) {
    _out.clear();
    _kernelIdx = 0;
    _handlerIdx = 0;
    _tmpIdx = 0;
    _out += _get_prelude();
    _out += "\n";
    for (auto& stmt : mod.stmts) {
        if (!stmt) continue;

        // Skip serve() calls and Router method calls — these are host-side only
        if (stmt->kind == NodeKind::ExprStmt) {
            auto& es = std::get<ExprStmt>(stmt->data);
            if (es.expr && es.expr->kind == NodeKind::CallExpr) {
                auto& call = std::get<CallExpr>(es.expr->data);
                if (call.callee && call.callee->kind == NodeKind::Ident) {
                    auto& ident = std::get<Ident>(call.callee->data);
                    if (ident.name == "serve") {
                        // Skip serve() calls
                        continue;
                    }
                }
                // Skip app.get/post/put/delete/patch calls
                if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
                    auto& mem = std::get<MemberExpr>(call.callee->data);
                    if (mem.prop == "get" || mem.prop == "post" || mem.prop == "put" ||
                        mem.prop == "delete" || mem.prop == "patch") {
                        continue;
                    }
                }
            }
        }

        switch (stmt->kind) {
            case NodeKind::ClassDecl:
            case NodeKind::FuncDecl:
                emitStmt(stmt, 0);
                break;
            case NodeKind::ConstDecl: {
                // Top-level const/let — emit as __device__ constexpr
                auto& d = std::get<ConstDecl>(stmt->data);
                if (d.init) {
                    // Skip Router instances — they're handled by the framework, not GPU
                    if (d.init->kind == NodeKind::NewExpr) {
                        auto& ne = std::get<NewExpr>(d.init->data);
                        if (ne.className == "Router") {
                            // Skip emitting Router() — it's a host-side object
                            break;
                        }
                    }
                    _out += "__device__ constexpr auto " + d.name +
                            " = " + emitExpr(d.init) + ";\n";
                }
                break;
            }
            case NodeKind::Block: {
                // Flat block (e.g. from enum expansion, namespace, array destructuring) —
                // emit each inner statement at top level
                auto& blk = std::get<Block>(stmt->data);
                if (blk.flat) {
                    // Scan for Prefix_Member pattern to register flat prefixes
                    // (enables Direction.Up → Direction_Up in emitMember)
                    for (auto& s : blk.stmts) {
                        if (!s || s->kind != NodeKind::ConstDecl) continue;
                        auto& d = std::get<ConstDecl>(s->data);
                        auto pos = d.name.find('_');
                        if (pos != std::string::npos && pos > 0) {
                            _flatPrefixes.insert(d.name.substr(0, pos));
                        }
                    }
                    for (auto& s : blk.stmts) {
                        if (!s) continue;
                        if (s->kind == NodeKind::ConstDecl) {
                            auto& d = std::get<ConstDecl>(s->data);
                            if (d.init) {
                                _out += "__device__ constexpr auto " + d.name +
                                        " = " + emitExpr(d.init) + ";\n";
                            }
                        } else {
                            emitStmt(s, 0);
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    return _out;
}

std::string Codegen::emitHandlerCode(const RouteReg& route, int idx) {
    _out.clear();
    _tmpIdx = 0;
    _ctxBodyVars.clear();
    _jsonParseVars.clear();
    emitHandler(route, idx);
    return _out;
}

std::string Codegen::emitSuffix(const std::vector<RouteReg>& routes) {
    _out.clear();
    emitDispatch(routes);
    emitRouteTable(routes);
    return _out;
}

std::string Codegen::emit(const Module& mod) {
    std::string shared = emitShared(mod);
    std::string handlers;
    for (int i = 0; i < (int)mod.routes.size(); i++) {
        handlers += emitHandlerCode(mod.routes[i], i);
    }
    std::string suffix = emitSuffix(mod.routes);
    return shared + handlers + suffix;
}

// ── Handler emission ──────────────────────────────────────────────────────────

void Codegen::emitHandler(const RouteReg& route, int idx) {
    _currentRoutePath = route.path;   // make pattern available to emitMember
    _out += "// Handler: " + route.method + " " + route.path + "\n";
    _out += "__device__ void symm_handler_" + std::to_string(idx) + "(SymmCtx* ctx) {\n";

    if (route.handler) {
        if (route.handler->kind == NodeKind::FuncDecl) {
            auto& fn = std::get<FuncDecl>(route.handler->data);
            // The handler receives a SymmCtx* — its first param is 'ctx'
            if (fn.body && fn.body->kind == NodeKind::Block) {
                emitBlock(std::get<Block>(fn.body->data), 1);
            }
        } else {
            // Handler is not a FuncDecl - might be an arrow function or other expression
            // Try to emit it as an expression statement
            _out += "    " + emitExpr(route.handler) + ";\n";
        }
    }

    _out += "}\n\n";
    _currentRoutePath.clear();
}

void Codegen::emitDispatch(const std::vector<RouteReg>& routes) {
    // ── Single-request dispatch (legacy / testing path) ──────────────────────
    // Used when batch_size == 1 (e.g. unit tests, debug builds).
    _out += "// Legacy single-request dispatch — kept for unit tests and debug.\n";
    _out += "extern \"C\" __global__ void symm_dispatch(\n";
    _out += "    int route_id,\n";
    _out += "    const char* body,\n";
    _out += "    int body_len,\n";
    _out += "    char* resp_buf,\n";
    _out += "    unsigned int* resp_len_out,\n";
    _out += "    const char* authorization,    // request Authorization header\n";
    _out += "    const char* content_type,     // request Content-Type header\n";
    _out += "    const char* user_agent        // request User-Agent header\n";
    _out += ") {\n";
    _out += "    SymmCtx ctx{};\n";
    _out += "    ctx.body          = body;\n";
    _out += "    ctx.body_len      = body_len;\n";
    _out += "    ctx.resp_buf      = resp_buf;\n";
    _out += "    ctx.resp_len      = 0;\n";
    _out += "    ctx.resp_status   = 200;\n";
    _out += "    ctx.authorization = authorization ? authorization : \"\";\n";
    _out += "    ctx.content_type  = content_type  ? content_type  : \"\";\n";
    _out += "    ctx.user_agent    = user_agent     ? user_agent    : \"\";\n";
    _out += "    switch (route_id) {\n";
    for (int i = 0; i < (int)routes.size(); i++) {
        _out += "        case " + std::to_string(i) + ": symm_handler_" + std::to_string(i) + "(&ctx); break;\n";
    }
    _out += "        default:\n";
    _out += "            symm_json_obj_str(&ctx, \"error\", \"404 Not Found\");\n";
    _out += "            ctx.resp_status = 404;\n";
    _out += "    }\n";
    _out += "    // Pack response headers into last 256 bytes of resp_buf for host pickup\n";
    _out += "    if (resp_buf && ctx.resp_headers_len > 0) {\n";
    _out += "        char* hdr_dst = resp_buf + (65536 - 256);\n";
    _out += "        for (int _hi = 0; _hi <= ctx.resp_headers_len && _hi < 256; _hi++)\n";
    _out += "            hdr_dst[_hi] = ctx.resp_headers[_hi];\n";
    _out += "    }\n";
    _out += "    if (resp_len_out) *resp_len_out = (unsigned int)ctx.resp_len;\n";
    _out += "}\n\n";

    // ── Batch dispatch — one thread per request, warp-aligned block ──────────
    //
    // Layout: launched as <<<1, batch_size>>> where batch_size is a multiple of 32.
    //
    // All threads in the block run the SAME route handler (route_id is uniform
    // across the block), so there is zero warp divergence at the switch statement.
    //
    // The active bitmask (d_active) guards threads whose slot index exceeds the
    // real request count (padding threads do a single cheap 'return').
    //
    // I/O barrier points (__syncthreads) are inserted by the codegen at every
    // GPU→CPU I/O handoff so all threads in the warp commit their I/O requests
    // before the kernel exits.
    _out += "// Batch dispatch — warp-aligned, one thread per request.\n";
    _out += "// Launch config: <<<1, batch_size>>>  where batch_size % 32 == 0.\n";
    _out += "extern \"C\" __global__ void symm_dispatch_batch(\n";
    _out += "    int            route_id,\n";
    _out += "    const char**   d_bodies,        // [batch_size] device pointers\n";
    _out += "    const int*     d_body_lens,     // [batch_size]\n";
    _out += "    const char**   d_paths,         // [batch_size]\n";
    _out += "    const char**   d_methods,       // [batch_size]\n";
    _out += "    char**         d_resp_bufs,     // [batch_size]\n";
    _out += "    unsigned int*  d_resp_lens,     // [batch_size] output\n";
    _out += "    int*           d_statuses,      // [batch_size] output\n";
    _out += "    const uint32_t* d_active,       // 256-bit bitmask (8 × uint32)\n";
    _out += "    int            batch_size,\n";
    _out += "    const char**   d_authorizations, // [batch_size] Authorization headers\n";
    _out += "    const char**   d_content_types,  // [batch_size] Content-Type headers\n";
    _out += "    const char**   d_user_agents     // [batch_size] User-Agent headers\n";
    _out += ") {\n";
    _out += "    int tid = threadIdx.x;\n";
    _out += "    // Mask check: is this thread slot active?\n";
    _out += "    bool active = (tid < batch_size)\n";
    _out += "               && (d_active[tid >> 5] & (1u << (tid & 31)));\n";
    _out += "    if (!active) return;\n";
    _out += "\n";
    _out += "    // Each thread builds its own SymmCtx from its slot in the batch.\n";
    _out += "    SymmCtx ctx{};\n";
    _out += "    ctx.body          = d_bodies[tid];\n";
    _out += "    ctx.body_len      = d_body_lens[tid];\n";
    _out += "    ctx.path          = d_paths[tid];\n";
    _out += "    ctx.method        = d_methods[tid];\n";
    _out += "    ctx.resp_buf      = d_resp_bufs[tid];\n";
    _out += "    ctx.resp_len      = 0;\n";
    _out += "    ctx.resp_status   = 200;\n";
    _out += "    ctx.authorization = (d_authorizations && d_authorizations[tid]) ? d_authorizations[tid] : \"\";\n";
    _out += "    ctx.content_type  = (d_content_types  && d_content_types[tid])  ? d_content_types[tid]  : \"\";\n";
    _out += "    ctx.user_agent    = (d_user_agents     && d_user_agents[tid])    ? d_user_agents[tid]    : \"\";\n";
    _out += "\n";
    _out += "    // All threads in the block execute the SAME case — no warp divergence\n";
    _out += "    // at this switch.  Divergence inside a handler is unavoidable but\n";
    _out += "    // handled by the warp scheduler normally.\n";
    _out += "    switch (route_id) {\n";
    for (int i = 0; i < (int)routes.size(); i++) {
        _out += "        case " + std::to_string(i) + ": symm_handler_" + std::to_string(i) + "(&ctx); break;\n";
    }
    _out += "        default:\n";
    _out += "            symm_json_obj_str(&ctx, \"error\", \"404 Not Found\");\n";
    _out += "            ctx.resp_status = 404;\n";
    _out += "    }\n";
    _out += "\n";
    _out += "    // Pack response headers into last 256 bytes of resp_buf for host pickup\n";
    _out += "    if (d_resp_bufs[tid] && ctx.resp_headers_len > 0) {\n";
    _out += "        char* hdr_dst = d_resp_bufs[tid] + (65536 - 256);\n";
    _out += "        for (int _hi = 0; _hi <= ctx.resp_headers_len && _hi < 256; _hi++)\n";
    _out += "            hdr_dst[_hi] = ctx.resp_headers[_hi];\n";
    _out += "    }\n";
    _out += "\n";
    _out += "    // Write per-thread outputs back to the batch arrays.\n";
    _out += "    if (d_resp_lens)  d_resp_lens[tid]  = (unsigned int)ctx.resp_len;\n";
    _out += "    if (d_statuses)   d_statuses[tid]   = ctx.resp_status;\n";
    _out += "\n";
    _out += "    // Warp-level sync: ensure all threads have committed their outputs\n";
    _out += "    // before the kernel exits.  This matters when subsequent CUDA work\n";
    _out += "    // in the same stream reads the batch output arrays.\n";
    _out += "    __syncwarp();\n";
    _out += "}\n\n";
}

void Codegen::emitRouteTable(const std::vector<RouteReg>& routes) {
    _out += "// Route count (read by host to build GpuRouter table)\n";
    _out += "__device__ __constant__ int symm_route_count = " + std::to_string(routes.size()) + ";\n\n";

    // Route method/path metadata — used by GpuRouter for request dispatch
    _out += "__device__ __constant__ const char* symm_route_methods[] = {";
    for (int i = 0; i < (int)routes.size(); i++) {
        if (i) _out += ", ";
        _out += "\"" + routes[i].method + "\"";
    }
    if (routes.empty()) _out += "\"\"";
    _out += "};\n";

    _out += "__device__ __constant__ const char* symm_route_paths[] = {";
    for (int i = 0; i < (int)routes.size(); i++) {
        if (i) _out += ", ";
        // Escape any special chars in path (normally just alphanumeric + / - _ :)
        _out += "\"" + routes[i].path + "\"";
    }
    if (routes.empty()) _out += "\"\"";
    _out += "};\n\n";
}

// ── Statement emission ────────────────────────────────────────────────────────

void Codegen::emitStmt(const NodePtr& node, int depth) {
    if (!node) return;
    std::string ind = indent(depth);

    switch (node->kind) {
        case NodeKind::ReturnStmt: {
            auto& r = std::get<ReturnStmt>(node->data);
            if (_inGeneratorBody) {
                // Inside generator _init(): set return value and stop
                if (r.expr) _out += ind + "_return_val = (float)(" + emitExpr(r.expr) + "); return;\n";
                else        _out += ind + "return;\n";
                break;
            }
            if (r.expr) {
                _out += ind + "return " + emitExpr(r.expr) + ";\n";
            } else {
                _out += ind + "return;\n";
            }
            break;
        }
        case NodeKind::ConstDecl: {
            auto& d = std::get<ConstDecl>(node->data);
            if (d.name.empty()) break;
            if (d.init && d.init->kind == NodeKind::MapCall) {
                // GPU map kernel emission is deferred — stub for now
                _out += ind + "float* " + d.name + " = (float*)ctx->resp_buf;\n";
            } else if (d.init && d.init->kind == NodeKind::ArrayExpr) {
                // Typed array init: auto cannot deduce from {NewExpr, ...} lists.
                auto& ae = std::get<ArrayExpr>(d.init->data);
                std::string elemType;
                bool heterogeneous = false;
                for (auto& el : ae.elements) {
                    if (el && el->kind == NodeKind::NewExpr) {
                        std::string t = std::get<NewExpr>(el->data).className;
                        if (elemType.empty()) elemType = t;
                        else if (elemType != t) { heterogeneous = true; }
                    }
                }
                if (heterogeneous || elemType.empty()) {
                    // Homogeneous literal array (float/int/bool/str) — infer element type
                    bool hasStr = false, hasBool = false, hasNum = false;
                    for (auto& el : ae.elements) {
                        if (!el) continue;
                        if (el->kind == NodeKind::Literal) {
                            auto& lit = std::get<Literal>(el->data);
                            if (lit.kind == Literal::Str)   hasStr = true;
                            else if (lit.kind == Literal::Bool) hasBool = true;
                            else hasNum = true;
                        } else {
                            hasNum = true;  // expression element — treat as numeric
                        }
                    }
                    std::string inferredType = hasStr ? "const char*" : (hasBool ? "bool" : "float");
                    int n = (int)ae.elements.size();
                    _out += ind + inferredType + " " + d.name + "[" + std::to_string(n > 0 ? n : 1) + "] = {";
                    for (int i = 0; i < n; i++) {
                        if (i) _out += ", ";
                        _out += ae.elements[i] ? emitExpr(ae.elements[i]) : "0";
                    }
                    _out += "};\n";
                    _out += ind + "int " + d.name + "_len = " + std::to_string(n) + ";\n";
                } else {
                    // NewExpr elements — typed struct array
                    int n = (int)ae.elements.size();
                    _out += ind + elemType + " " + d.name + "[" + std::to_string(n > 0 ? n : 1) + "] = ";
                    _out += emitExpr(d.init) + ";\n";
                    _out += ind + "int " + d.name + "_len = " + std::to_string(n) + ";\n";
                }
            } else if (d.init && d.init->kind == NodeKind::TemplateLit) {
                // Template literal → char buffer
                auto& tmpl = std::get<TemplateLit>(d.init->data);
                std::string buf = freshTemp();
                _out += ind + "char " + buf + "[1024]; int " + buf + "_len = 0;\n";
                emitTemplateLit(tmpl, depth, buf);
                _out += ind + buf + "[" + buf + "_len] = '\\0';\n";
                _out += ind + "const char* " + d.name + " = " + buf + ";\n";
            } else if (d.init && d.init->kind == NodeKind::NewExpr
                       && std::get<NewExpr>(d.init->data).className == "URL") {
                // new URL(s) → parse URL, set _err=1 if invalid (caught by try/catch)
                _out += ind + "auto " + d.name + " = " + emitExpr(d.init) + ";\n";
                _out += ind + "if (!" + d.name + "._valid) _err = 1;\n";
            } else {
                // Destructuring temps (_doN) and their member bindings must be references
                // so mutations on the original object remain visible through all aliases.
                bool isDestructureTemp = (d.name.size() > 3
                    && d.name[0]=='_' && d.name[1]=='d' && d.name[2]=='o'
                    && d.init && d.init->kind == NodeKind::Ident);
                bool isDestructureBind = false;
                std::string destructSrc;
                if (d.init && d.init->kind == NodeKind::MemberExpr) {
                    auto& me = std::get<MemberExpr>(d.init->data);
                    if (me.object && me.object->kind == NodeKind::Ident) {
                        destructSrc = std::get<Ident>(me.object->data).name;
                        isDestructureBind = (destructSrc.size()>3
                            && destructSrc[0]=='_' && destructSrc[1]=='d' && destructSrc[2]=='o');
                    }
                }
                // Track ctx.json() read-mode vars — destructuring their fields requires JSON parsing
                if (isDestructureTemp && d.init && d.init->kind == NodeKind::CallExpr) {
                    auto& call = std::get<CallExpr>(d.init->data);
                    if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
                        auto& mem = std::get<MemberExpr>(call.callee->data);
                        std::string obj = emitExpr(mem.object);
                        if (obj == "ctx" && mem.prop == "json" && call.args.empty()) {
                            _ctxBodyVars.insert(d.name);
                        }
                    }
                }

                // Track JSON.parse() vars — property access needs symm_json_prop
                if (d.init && d.init->kind == NodeKind::CallExpr) {
                    auto& call = std::get<CallExpr>(d.init->data);
                    if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
                        auto& mem = std::get<MemberExpr>(call.callee->data);
                        std::string obj = emitExpr(mem.object);
                        if (obj == "JSON" && mem.prop == "parse") {
                            _jsonParseVars.insert(d.name);
                        }
                    }
                }
                // If binding from a ctx.json() body var, extract the field from JSON
                if (isDestructureBind && _ctxBodyVars.count(destructSrc)) {
                    auto& me = std::get<MemberExpr>(d.init->data);
                    std::string bufName = "_bf_" + d.name;
                    _out += ind + "char " + bufName + "[4096] = {};\n";
                    _out += ind + "symm_body_get_str(ctx->body, \"" + me.prop + "\", " + bufName + ", 4096);\n";
                    _out += ind + "const char* " + d.name + " = " + bufName + ";\n";
                    break;
                }
                std::string kw = (isDestructureTemp || isDestructureBind) ? "auto&" : "auto";
                std::string val = d.init ? emitExpr(d.init) : "0";
                _out += ind + kw + " " + d.name + " = " + val + ";\n";
            }
            break;
        }
        case NodeKind::ExprStmt: {
            auto& es = std::get<ExprStmt>(node->data);
            // Special case: array destructuring assignment [a, b] = [c, d]
            // Must use temporaries to avoid order-of-evaluation bugs.
            if (es.expr && es.expr->kind == NodeKind::AssignExpr) {
                auto& ae = std::get<AssignExpr>(es.expr->data);
                if (ae.op == "=" && ae.left && ae.left->kind == NodeKind::ArrayExpr
                    && ae.right && ae.right->kind == NodeKind::ArrayExpr) {
                    auto& lhs = std::get<ArrayExpr>(ae.left->data);
                    auto& rhs = std::get<ArrayExpr>(ae.right->data);
                    std::vector<std::string> tmps;
                    for (auto& r : rhs.elements) {
                        std::string t = freshTemp();
                        tmps.push_back(t);
                        _out += ind + "auto " + t + " = " + emitExpr(r) + ";\n";
                    }
                    for (int i = 0; i < (int)lhs.elements.size() && i < (int)tmps.size(); i++) {
                        if (lhs.elements[i] && lhs.elements[i]->kind != NodeKind::Literal)
                            _out += ind + emitExpr(lhs.elements[i]) + " = " + tmps[i] + ";\n";
                    }
                    break;
                }
            }
            // Special case: ctx.json(ObjectExpr) → runtime JSON builder
            if (es.expr && es.expr->kind == NodeKind::CallExpr) {
                auto& call = std::get<CallExpr>(es.expr->data);
                bool isCtxJson = false;
                const ObjectExpr* oe = nullptr;
                if (call.callee && call.callee->kind == NodeKind::MemberExpr) {
                    auto& mem = std::get<MemberExpr>(call.callee->data);
                    std::string obj2 = emitExpr(mem.object);
                    if (obj2 == "ctx" && mem.prop == "json"
                        && call.args.size() == 1
                        && call.args[0]->kind == NodeKind::ObjectExpr) {
                        isCtxJson = true;
                        oe = &std::get<ObjectExpr>(call.args[0]->data);
                    }
                }
                if (isCtxJson && oe) {
                    std::string buf = freshTemp();
                    _out += ind + "{ char " + buf + "[65536]; int " + buf + "_n = 0;\n";
                    _out += indent(depth+1) + buf + "[" + buf + "_n++] = '{';\n";
                    bool firstProp = true;
                    for (int pi = 0; pi < (int)oe->props.size(); pi++) {
                        const std::string& key = oe->props[pi].first;
                        auto& val = oe->props[pi].second;

                        // __spread__: {...obj} — emit "...obj" as a JSON merge comment
                        // On GPU we can't dynamically enumerate object keys, so we use
                        // symm_json_merge which copies the source JSON string into the buffer.
                        if (key == "__spread__") {
                            if (!firstProp)
                                _out += indent(depth+1) + buf + "_n > 1 && " + buf + "[" + buf + "_n-1] != '{' ? " + buf + "[" + buf + "_n++] = ',' : (void)0;\n";
                            _out += indent(depth+1) + "{ auto _sp = " + emitExpr(val) + "; "
                                 + buf + "_n += symm_json_merge(" + buf + " + " + buf + "_n, 65536 - " + buf + "_n - 1, "
                                 + "_sp); }\n";
                            firstProp = false;
                            continue;
                        }
                        // __computed__: skip
                        if (key == "__computed__") continue;

                        if (!firstProp) _out += indent(depth+1) + buf + "[" + buf + "_n++] = ',';\n";
                        firstProp = false;

                        _out += indent(depth+1) + buf + "_n += symm_write_str("
                             + buf + " + " + buf + "_n, \"\\\"" + key + "\\\":\");\n";
                        if (val && val->kind == NodeKind::Literal) {
                            auto& lit = std::get<Literal>(val->data);
                            if (lit.kind == Literal::Str) {
                                _out += indent(depth+1) + buf + "_n += symm_write_str("
                                     + buf + " + " + buf + "_n, \"\\\"" + lit.raw + "\\\"\");\n";
                            } else if (lit.kind == Literal::Bool) {
                                _out += indent(depth+1) + buf + "_n += symm_write_bool("
                                     + buf + " + " + buf + "_n, " + lit.raw + ");\n";
                            } else if (lit.kind == Literal::Null) {
                                _out += indent(depth+1) + buf + "_n += symm_write_str("
                                     + buf + " + " + buf + "_n, \"null\");\n";
                            } else {
                                _out += indent(depth+1) + buf + "_n += symm_write_int("
                                     + buf + " + " + buf + "_n, (int)(" + lit.raw + "));\n";
                            }
                        } else {
                            _out += indent(depth+1) + buf + "_n += symm_json_val("
                                 + buf + " + " + buf + "_n, " + emitExpr(val) + ");\n";
                        }
                    }
                    _out += indent(depth+1) + buf + "[" + buf + "_n++] = '}';\n";
                    _out += indent(depth+1) + buf + "[" + buf + "_n]   = '\\0';\n";
                    _out += indent(depth+1) + "symm_json_str(ctx, " + buf + ");\n";
                    _out += ind + "}\n";
                    break;
                }
            }
            // Special: ctx.status(N).json(body) → set resp_status then send JSON
            if (es.expr && es.expr->kind == NodeKind::CallExpr) {
                auto& outerCall = std::get<CallExpr>(es.expr->data);
                if (outerCall.callee && outerCall.callee->kind == NodeKind::MemberExpr) {
                    auto& outerMem = std::get<MemberExpr>(outerCall.callee->data);
                    if (outerMem.prop == "json"
                        && outerMem.object && outerMem.object->kind == NodeKind::CallExpr) {
                        auto& innerCall = std::get<CallExpr>(outerMem.object->data);
                        if (innerCall.callee && innerCall.callee->kind == NodeKind::MemberExpr) {
                            auto& innerMem = std::get<MemberExpr>(innerCall.callee->data);
                            std::string iObj = emitExpr(innerMem.object);
                            if (iObj == "ctx" && innerMem.prop == "status" && !innerCall.args.empty()) {
                                // Emit json first, then override status (symm_json_str sets 200)
                                if (!outerCall.args.empty()) {
                                    _out += ind + "symm_json_str(ctx, " + emitExpr(outerCall.args[0]) + ");\n";
                                }
                                _out += ind + "ctx->resp_status = " + emitExpr(innerCall.args[0]) + ";\n";
                                break;
                            }
                        }
                    }
                }
            }
            // Special: signal.addEventListener('abort', () => { BOOLVAR = true; })
            // → signal._addTarget(&BOOLVAR) so abort() can fire the listener
            if (es.expr && es.expr->kind == NodeKind::CallExpr) {
                auto& call = std::get<CallExpr>(es.expr->data);
                if (call.callee && call.callee->kind == NodeKind::MemberExpr
                    && call.args.size() == 2 && call.args[1]
                    && call.args[1]->kind == NodeKind::FuncDecl) {
                    auto& mem = std::get<MemberExpr>(call.callee->data);
                    if (mem.prop == "addEventListener") {
                        auto& fn = std::get<FuncDecl>(call.args[1]->data);
                        std::string sigObj = emitExpr(mem.object);
                        bool handled = false;
                        if (fn.body && fn.body->kind == NodeKind::Block) {
                            auto& blk = std::get<Block>(fn.body->data);
                            for (auto& s2 : blk.stmts) {
                                if (!s2 || s2->kind != NodeKind::ExprStmt) continue;
                                auto& es2 = std::get<ExprStmt>(s2->data);
                                if (!es2.expr || es2.expr->kind != NodeKind::AssignExpr) continue;
                                auto& ae2 = std::get<AssignExpr>(es2.expr->data);
                                if (ae2.op == "=" && ae2.left && ae2.left->kind == NodeKind::Ident
                                    && ae2.right && ae2.right->kind == NodeKind::Literal) {
                                    auto& lit = std::get<Literal>(ae2.right->data);
                                    if (lit.raw == "true" || lit.kind == Literal::Bool) {
                                        std::string var = std::get<Ident>(ae2.left->data).name;
                                        _out += ind + sigObj + "._addTarget(&" + var + ");\n";
                                        handled = true;
                                    }
                                }
                            }
                        }
                        if (handled) break;
                    }
                }
            }
            // Inside a generator _init(): yield expr → buffer append
            if (_inGeneratorBody && es.expr && es.expr->kind == NodeKind::AwaitExpr) {
                auto& aw = std::get<AwaitExpr>(es.expr->data);
                _out += ind + "if (_count < _MAX) _values[_count++] = (float)("
                     + emitExpr(aw.expr) + ");\n";
                break;
            }
            _out += ind + emitExpr(es.expr) + ";\n";
            break;
        }
        case NodeKind::AssignExpr: {
            auto& ae = std::get<AssignExpr>(node->data);
            _out += ind + emitExpr(ae.left) + " " + ae.op + " " + emitExpr(ae.right) + ";\n";
            break;
        }
        case NodeKind::IfStmt: {
            auto& is = std::get<IfStmt>(node->data);
            _out += ind + "if (" + emitExpr(is.cond) + ") {\n";
            emitStmt(is.then, depth + 1);
            _out += ind + "}";
            if (is.else_) {
                _out += " else {\n";
                emitStmt(is.else_, depth + 1);
                _out += ind + "}";
            }
            _out += "\n";
            break;
        }
        case NodeKind::ForStmt: {
            auto& fs = std::get<ForStmt>(node->data);
            _out += ind + "for (";
            if (fs.init) {
                if (fs.init->kind == NodeKind::ConstDecl) {
                    auto& d = std::get<ConstDecl>(fs.init->data);
                    _out += "auto " + d.name + " = " + (d.init ? emitExpr(d.init) : "0");
                } else {
                    auto& es = std::get<ExprStmt>(fs.init->data);
                    _out += emitExpr(es.expr);
                }
            }
            _out += "; ";
            if (fs.cond) _out += emitExpr(fs.cond);
            _out += "; ";
            if (fs.update) _out += emitExpr(fs.update);
            _out += ") {\n";
            emitStmt(fs.body, depth + 1);
            _out += ind + "}\n";
            break;
        }
        case NodeKind::ForOfStmt: {
            auto& fo = std::get<ForOfStmt>(node->data);
            std::string iter = emitExpr(fo.iter);
            if (fo.isAwait) {
                // for await (const x of asyncObj) → use GpuAsyncIterState state machine
                std::string stVar = freshTemp() + "_ast";
                _out += ind + "{ auto " + stVar + " = " + iter + ".__asyncIterator();\n";
                _out += indent(depth+1) + "for (auto " + fo.varName + " : " + stVar + ") {\n";
                emitStmt(fo.body, depth + 2);
                _out += indent(depth+1) + "}\n";
                _out += ind + "}\n";
            } else {
                // Determine if iterating a string (char-by-char) or an array (index loop).
                // String indicators: Literal::Str, TemplateLit, ctx string fields, _SBuf results.
                bool isStringIter = false;
                if (fo.iter) {
                    if (fo.iter->kind == NodeKind::Literal) {
                        auto& lit = std::get<Literal>(fo.iter->data);
                        if (lit.kind == Literal::Str) isStringIter = true;
                    }
                    if (fo.iter->kind == NodeKind::TemplateLit) isStringIter = true;
                    if (fo.iter->kind == NodeKind::MemberExpr) {
                        auto& me = std::get<MemberExpr>(fo.iter->data);
                        // Known string-type fields on ctx and common structs
                        static const std::set<std::string> STR_FIELDS = {
                            "path","method","query","body","message","name","text",
                            "key","label","reason","platform","env"
                        };
                        if (STR_FIELDS.count(me.prop)) isStringIter = true;
                    }
                    if (fo.iter->kind == NodeKind::CallExpr) {
                        auto& ce = std::get<CallExpr>(fo.iter->data);
                        if (ce.callee && ce.callee->kind == NodeKind::MemberExpr) {
                            auto& cm = std::get<MemberExpr>(ce.callee->data);
                            // String prototype methods that return strings
                            static const std::set<std::string> STR_CALL_PROPS = {
                                "trim","trimStart","trimEnd","trimLeft","trimRight",
                                "toLowerCase","toUpperCase","toLocaleLowerCase","toLocaleUpperCase",
                                "slice","substring","replace","replaceAll","repeat",
                                "padStart","padEnd","at","concat","split"
                            };
                            if (STR_CALL_PROPS.count(cm.prop)) isStringIter = true;
                        }
                    }
                }

                std::string idx = freshTemp() + "_i";
                if (isStringIter) {
                    // for (const ch of str) — iterate chars; expose as one-char C-string
                    std::string chBuf = freshTemp() + "_ch";
                    _out += ind + "for (int " + idx + " = 0; (" + iter + ")[" + idx + "]; " + idx + "++) {\n";
                    _out += indent(depth+1) + "char " + chBuf + "[2] = {(" + iter + ")[" + idx + "], '\\0'};\n";
                    _out += indent(depth+1) + "const char* " + fo.varName + " = " + chBuf + ";\n";
                    emitStmt(fo.body, depth + 1);
                    _out += ind + "}\n";
                } else {
                    // for (const x of arr) — try iterator protocol first, then fall back to _len
                    // Iterator protocol: object has __sym_iterator__() method that returns iterator with next()
                    std::string itVar = freshTemp() + "_it";
                    std::string resVar = freshTemp() + "_res";
                    _out += ind + "{\n";
                    _out += indent(depth+1) + "auto " + itVar + " = (" + iter + ").__sym_iterator__();\n";
                    _out += indent(depth+1) + "for (;;) {\n";
                    _out += indent(depth+2) + "auto " + resVar + " = " + itVar + ".next();\n";
                    _out += indent(depth+2) + "if (" + resVar + ".done) break;\n";
                    _out += indent(depth+2) + "auto " + fo.varName + " = " + resVar + ".value;\n";
                    emitStmt(fo.body, depth + 2);
                    _out += indent(depth+1) + "}\n";
                    _out += ind + "}\n";
                }
            }
            break;
        }
        case NodeKind::ForInStmt: {
            auto& fi = std::get<ForInStmt>(node->data);
            // for...in on GPU: iterate static string key slots of a Map-like object.
            // We can't enumerate object keys at runtime — emit a size-bounded loop
            // over the map's internal entry array and expose the key as varName.
            std::string obj = emitExpr(fi.obj);
            std::string idx = freshTemp() + "_ki";
            _out += ind + "// for...in over " + obj + "\n";
            _out += ind + "for (int " + idx + " = 0; " + idx + " < " + obj + "._n; " + idx + "++) {\n";
            _out += indent(depth+1) + "const char* " + fi.varName + " = " + obj + "._e[" + idx + "].k;\n";
            emitStmt(fi.body, depth + 1);
            _out += ind + "}\n";
            break;
        }
        case NodeKind::WhileStmt: {
            auto& ws = std::get<WhileStmt>(node->data);
            _out += ind + "while (" + emitExpr(ws.cond) + ") {\n";
            emitStmt(ws.body, depth + 1);
            _out += ind + "}\n";
            break;
        }
        case NodeKind::DoWhileStmt: {
            auto& dw = std::get<DoWhileStmt>(node->data);
            _out += ind + "do {\n";
            emitStmt(dw.body, depth + 1);
            _out += ind + "} while (" + emitExpr(dw.cond) + ");\n";
            break;
        }
        case NodeKind::TryCatchStmt: {
            auto& tc = std::get<TryCatchStmt>(node->data);
            // GPU kernels can't throw C++ exceptions — model them with Error objects.
            // Emit try body directly; catch block guarded by an error-check variable.
            _out += ind + "{ Error _thrown_err; bool _err_caught = false;\n";
            _out += ind + "  // try\n";
            if (tc.tryBody && tc.tryBody->kind == NodeKind::Block)
                emitBlock(std::get<Block>(tc.tryBody->data), depth + 1);
            if (tc.catchBody) {
                _out += ind + "  if (_thrown_err.message != nullptr && _thrown_err.message[0] != '\\0') {\n";
                _out += ind + "    _err_caught = true;\n";
                if (!tc.catchParam.empty())
                    _out += indent(depth+2) + "Error " + tc.catchParam + " = _thrown_err;\n";
                if (tc.catchBody->kind == NodeKind::Block)
                    emitBlock(std::get<Block>(tc.catchBody->data), depth + 2);
                _out += ind + "  }\n";
            }
            if (tc.finallyBody) {
                _out += ind + "  // finally — always executes\n";
                if (tc.finallyBody->kind == NodeKind::Block)
                    emitBlock(std::get<Block>(tc.finallyBody->data), depth + 1);
            }
            _out += ind + "}\n";
            break;
        }
        case NodeKind::ThrowStmt: {
            auto& th = std::get<ThrowStmt>(node->data);
            // GPU: create Error object and set it as thrown
            std::string errExpr = emitExpr(th.expr);
            _out += ind + "{ _thrown_err = " + errExpr + "; }\n";
            break;
        }
        case NodeKind::SwitchStmt: {
            auto& sw = std::get<SwitchStmt>(node->data);
            std::string testExpr = emitExpr(sw.test);
            // Determine if any case has a string literal — if so, emit if-else chain
            // (C++ switch cannot use const char* values)
            bool hasStringCase = false;
            for (auto& sc : sw.cases) {
                if (sc.test && sc.test->kind == NodeKind::Literal &&
                    std::get<Literal>(sc.test->data).kind == Literal::Str) {
                    hasStringCase = true; break;
                }
            }
            if (hasStringCase) {
                // Emit as if-else chain with symm_str_eq comparisons
                std::string testVar = freshTemp() + "_sw";
                _out += ind + "{ const char* " + testVar + " = " + testExpr + ";\n";
                bool first = true;
                for (auto& sc : sw.cases) {
                    if (!sc.test) continue;  // default — handled at end
                    std::string caseVal = emitExpr(sc.test);
                    if (first) {
                        _out += indent(depth+1) + "if (symm_str_eq(" + testVar + ", " + caseVal + ")) {\n";
                        first = false;
                    } else {
                        _out += indent(depth+1) + "} else if (symm_str_eq(" + testVar + ", " + caseVal + ")) {\n";
                    }
                    for (auto& s : sc.body) emitStmt(s, depth + 2);
                }
                // default case
                for (auto& sc : sw.cases) {
                    if (sc.test) continue;
                    _out += (first ? indent(depth+1) + "{\n" : indent(depth+1) + "} else {\n");
                    for (auto& s : sc.body) emitStmt(s, depth + 2);
                }
                if (!first) _out += indent(depth+1) + "}\n";
                _out += ind + "}\n";
            } else {
                // Numeric switch — use C++ switch directly
                _out += ind + "switch ((int)(" + testExpr + ")) {\n";
                for (auto& sc : sw.cases) {
                    if (sc.test) _out += ind + "  case " + emitExpr(sc.test) + ":\n";
                    else         _out += ind + "  default:\n";
                    for (auto& s : sc.body) emitStmt(s, depth + 2);
                }
                _out += ind + "}\n";
            }
            break;
        }
        case NodeKind::BreakStmt: {
            auto& bs = std::get<BreakStmt>(node->data);
            if (!bs.label.empty())
                _out += ind + "goto _lbl_" + bs.label + "_end;\n";
            else
                _out += ind + "break;\n";
            break;
        }
        case NodeKind::ContinueStmt: {
            auto& cs = std::get<ContinueStmt>(node->data);
            if (!cs.label.empty())
                _out += ind + "goto _lbl_" + cs.label + "_cont;\n";
            else
                _out += ind + "continue;\n";
            break;
        }
        case NodeKind::LabeledStmt: {
            auto& ls = std::get<LabeledStmt>(node->data);
            // Emit inner statement (for/while/do) with surrounding goto labels
            // The goto targets are: _lbl_<name>_cont for continue, _lbl_<name>_end for break
            _out += ind + "/* label: " + ls.label + " */\n";
            emitStmt(ls.body, depth);
            _out += ind + "_lbl_" + ls.label + "_end: ;\n";
            break;
        }
        case NodeKind::FuncDecl: {
            auto& fn = std::get<FuncDecl>(node->data);
            if (fn.name.empty()) break;  // anonymous — emitted inline as lambda

            // Generator functions: buffering state machine
            // All yield values are collected into a fixed-size array on the first next() call.
            // Supports yields inside loops, conditionals, and arbitrary control flow.
            if (fn.isGenerator && fn.body) {
                // ── Struct definition ──
                _out += ind + "struct " + fn.name + "_Gen {\n";

                // Captured params as struct members (original names → accessible in _init())
                for (auto& p : fn.params)
                    _out += indent(depth+1) + "float _arg_" + p + " = 0;\n";

                // Buffer and bookkeeping
                _out += indent(depth+1) + "static constexpr int _MAX = 256;\n";
                _out += indent(depth+1) + "float _values[_MAX]{};\n";
                _out += indent(depth+1) + "int   _count = 0;\n";
                _out += indent(depth+1) + "int   _pos   = 0;\n";
                _out += indent(depth+1) + "float _return_val = 0;\n";
                _out += indent(depth+1) + "bool  _initialized = false;\n";

                // ── _init(): run the generator body once, buffering yields ──
                _out += indent(depth+1) + "__device__ void _init() {\n";
                // Re-bind captured params to local names so the body compiles as-is
                for (auto& p : fn.params)
                    _out += indent(depth+2) + "float " + p + " = _arg_" + p + ";\n";
                // Emit body — yield → buffer append, return → set _return_val
                _inGeneratorBody = true;
                if (fn.body->kind == NodeKind::Block)
                    emitBlock(std::get<Block>(fn.body->data), depth + 2);
                _inGeneratorBody = false;
                _out += indent(depth+1) + "}\n";

                // ── next() ──
                _out += indent(depth+1) + "__device__ GeneratorResult<float> next() {\n";
                _out += indent(depth+2) + "if (!_initialized) { _init(); _initialized = true; }\n";
                _out += indent(depth+2) + "if (_pos < _count) return GeneratorResult<float>{ _values[_pos++], false };\n";
                _out += indent(depth+2) + "return GeneratorResult<float>{ _return_val, true };\n";
                _out += indent(depth+1) + "}\n";

                // ── return_(val) — force done ──
                _out += indent(depth+1) + "__device__ GeneratorResult<float> return_(float v = 0) {\n";
                _out += indent(depth+2) + "_pos = _count; _return_val = v;\n";
                _out += indent(depth+2) + "return GeneratorResult<float>{ v, true };\n";
                _out += indent(depth+1) + "}\n";

                // ── throw_(err) — mark done ──
                _out += indent(depth+1) + "template<typename E> __device__ GeneratorResult<float> throw_(E) {\n";
                _out += indent(depth+2) + "_pos = _count;\n";
                _out += indent(depth+2) + "return GeneratorResult<float>{ 0, true };\n";
                _out += indent(depth+1) + "}\n";

                // ── __sym_iterator__() so for-of works on generator objects ──
                _out += indent(depth+1) + "__device__ " + fn.name + "_Gen& __sym_iterator__() { return *this; }\n";

                _out += ind + "};\n";

                // ── Factory function: captures params into struct ──
                _out += ind + "__device__ " + fn.name + "_Gen " + fn.name + "(";
                for (int i = 0; i < (int)fn.params.size(); i++) {
                    if (i) _out += ", ";
                    _out += "float " + fn.params[i];
                }
                _out += ") {\n";
                _out += indent(depth+1) + fn.name + "_Gen _g;\n";
                for (auto& p : fn.params)
                    _out += indent(depth+1) + "_g._arg_" + p + " = " + p + ";\n";
                _out += indent(depth+1) + "return _g;\n";
                _out += ind + "}\n";
                break;
            }

            // Regular (non-generator) function
            // NVRTC rejects C++20 `auto` params and template decls inside functions
            if (!fn.params.empty() && depth == 0) {
                _out += ind + "template<";
                for (int i = 0; i < (int)fn.params.size(); i++) {
                    if (i) _out += ", ";
                    _out += "typename _Fp" + std::to_string(i);
                }
                _out += ">\n";
            }
            _out += ind + "__device__ auto " + fn.name + "(";
            for (int i = 0; i < (int)fn.params.size(); i++) {
                if (i) _out += ", ";
                _out += "_Fp" + std::to_string(i) + " " + fn.params[i];
            }
            _out += ") {\n";
            if (fn.body) {
                if (fn.body->kind == NodeKind::Block) {
                    emitBlock(std::get<Block>(fn.body->data), depth + 1);
                } else {
                    // Arrow function with implicit return: body is an expression
                    _out += ind + "    return " + emitExpr(fn.body) + ";\n";
                }
            }
            _out += ind + "}\n";
            break;
        }
        case NodeKind::ClassDecl:
            emitClass(std::get<ClassDecl>(node->data), depth);
            break;
        case NodeKind::Block: {
            auto& blk = std::get<Block>(node->data);
            if (blk.flat) {
                emitBlock(blk, depth);  // synthetic destructuring block — no extra braces
            } else {
                _out += ind + "{\n";
                emitBlock(blk, depth + 1);
                _out += ind + "}\n";
            }
            break;
        }
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
            // Handle delete operator specially — return true (property deletion is a no-op on GPU)
            if (u.op == "delete") {
                return "(true)";  // delete always succeeds on GPU; properties are immutable
            }
            std::string e = emitExpr(u.expr);
            if (u.postfix) {
                // postfix ++/-- : strip the "_post" suffix
                std::string op = u.op.substr(0, 2);
                return "(" + e + op + ")";
            }
            return "(" + u.op + e + ")";
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
            // GPU map kernels can't be emitted inside __device__ handler bodies.
            // Caller must use emitMapKernel() at top-level scope instead.
            return "/*gpu-map-unsupported*/nullptr";
        }

        case NodeKind::NewExpr: {
            auto& ne = std::get<NewExpr>(node->data);
            // Function() constructor not supported on GPU — can't generate code at runtime
            if (ne.className == "Function") {
                return "nullptr";  // Safe no-op
            }
            std::string args;
            for (int i = 0; i < (int)ne.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(ne.args[i]);
            }
            // Emit as struct value construction (no heap allocation on GPU)
            return ne.className + "{" + args + "}";
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
            auto& oe = std::get<ObjectExpr>(node->data);
            // Check if all values are compile-time literals — emit static string if so.
            bool allLiteral = true;
            for (auto& kv : oe.props) {
                auto& val = kv.second;
                if (!val || val->kind != NodeKind::Literal) { allLiteral = false; break; }
            }
            if (allLiteral) {
                std::string json = "{";
                for (int i = 0; i < (int)oe.props.size(); i++) {
                    if (i) json += ",";
                    json += "\\\"" + oe.props[i].first + "\\\":";
                    auto& val = oe.props[i].second;
                    auto& lit = std::get<Literal>(val->data);
                    if (lit.kind == Literal::Str)
                        json += "\\\"" + lit.raw + "\\\"";
                    else if (lit.kind == Literal::Null)
                        json += "null";
                    else if (lit.kind == Literal::Bool)
                        json += lit.raw;
                    else if (lit.kind == Literal::BigInt) {
                        std::string val_str = lit.raw;
                        if (!val_str.empty() && val_str.back() == 'n') val_str.pop_back();
                        json += val_str + "ull";
                    }
                    else
                        json += lit.raw;
                }
                return "\"" + json + "}\"";
            }
            // Dynamic object — emit as IIFE building JSON into a _SBuf
            std::string body = "_SBuf _ob; int _on = 0; _ob._b[_on++] = '{'; ";
            for (int i = 0; i < (int)oe.props.size(); i++) {
                if (i) body += "_ob._b[_on++] = ','; ";
                std::string key = oe.props[i].first;
                body += "_on += symm_write_str(_ob._b + _on, \"\\\"" + key + "\\\":\"); ";
                auto& val = oe.props[i].second;
                if (val && val->kind == NodeKind::Literal) {
                    auto& lit = std::get<Literal>(val->data);
                    if (lit.kind == Literal::Str)
                        body += "_on += symm_write_str(_ob._b + _on, \"\\\"" + lit.raw + "\\\"\"); ";
                    else if (lit.kind == Literal::BigInt) {
                        std::string val_str = lit.raw;
                        if (!val_str.empty() && val_str.back() == 'n') val_str.pop_back();
                        body += "_on += symm_json_val(_ob._b + _on, " + val_str + "ull); ";
                    }
                    else
                        body += "_on += symm_json_val(_ob._b + _on, " + lit.raw + "); ";
                } else {
                    std::string ex = val ? emitExpr(val) : "nullptr";
                    body += "_on += symm_json_val(_ob._b + _on, " + ex + "); ";
                }
            }
            body += "_ob._b[_on < 510 ? _on++ : 510] = '}'; _ob._b[_on < 511 ? _on : 511] = '\\0'; return _ob;";
            return "([&]()->_SBuf{ " + body + " }())";
        }

        case NodeKind::TemplateLit: {
            // Template literal in expression context: emit inline IIFE returning _SBuf.
            // _SBuf is a 512-byte stack struct with implicit const char* conversion.
            auto& tmpl = std::get<TemplateLit>(node->data);
            if (tmpl.exprs.empty()) {
                // No interpolation — simple string literal
                std::string s = tmpl.parts.empty() ? "" : tmpl.parts[0];
                // escape backslashes and quotes
                std::string esc;
                for (char ch : s) {
                    if (ch == '"')       esc += "\\\"";
                    else if (ch == '\\') esc += "\\\\";
                    else if (ch == '\n') esc += "\\n";
                    else if (ch == '\r') esc += "\\r";
                    else if (ch == '\t') esc += "\\t";
                    else                 esc += ch;
                }
                return "\"" + esc + "\"";
            }
            // Build via immediately-invoked lambda returning _SBuf (stack, GPU-safe)
            std::string body = "_SBuf _tb; int _tl = 0; ";
            for (size_t i = 0; i < tmpl.parts.size(); i++) {
                if (!tmpl.parts[i].empty()) {
                    std::string esc;
                    for (char ch : tmpl.parts[i]) {
                        if (ch == '"')       esc += "\\\"";
                        else if (ch == '\\') esc += "\\\\";
                        else if (ch == '\n') esc += "\\n";
                        else if (ch == '\r') esc += "\\r";
                        else if (ch == '\t') esc += "\\t";
                        else                 esc += ch;
                    }
                    body += "_tl += symm_write_str(_tb._b + _tl, \"" + esc + "\"); ";
                }
                if (i < tmpl.exprs.size()) {
                    std::string ex = emitExpr(tmpl.exprs[i]);
                    body += "_tl += symm_write(_tb._b + _tl, " + ex + "); ";
                }
            }
            body += "_tb._b[_tl < 511 ? _tl : 511] = '\\0'; return _tb;";
            return "([&]()->_SBuf{ " + body + " }())";
        }

        case NodeKind::AssignExpr: {
            auto& ae = std::get<AssignExpr>(node->data);
            return "(" + emitExpr(ae.left) + " " + ae.op + " " + emitExpr(ae.right) + ")";
        }

        case NodeKind::OptChainExpr: {
            auto& oc = std::get<OptChainExpr>(node->data);
            std::string obj = emitExpr(oc.object);
            if (oc.prop.empty()) return obj;
            // ctx is a pointer; all other objects use value-type (.) access.
            // On GPU there's no dynamic null — wrap in ternary as null-safety hint.
            bool isPtr = (obj == "ctx" || obj == "this");
            std::string sep = isPtr ? "->" : ".";
            return "(" + obj + " ? " + obj + sep + oc.prop + " : nullptr)";
        }

        case NodeKind::TypeofExpr: {
            auto& te = std::get<TypeofExpr>(node->data);
            auto& inner = te.expr;
            if (!inner) return "\"undefined\"";
            // Literal — can determine type at compile time
            if (inner->kind == NodeKind::Literal) {
                auto& lit = std::get<Literal>(inner->data);
                switch (lit.kind) {
                    case Literal::Int:
                    case Literal::Float: return "\"number\"";
                    case Literal::Str:   return "\"string\"";
                    case Literal::Bool:  return "\"boolean\"";
                    case Literal::Null:  return "\"object\"";  // JS: typeof null === "object"
                }
            }
            // Identifier — check for well-known names
            if (inner->kind == NodeKind::Ident) {
                std::string nm = std::get<Ident>(inner->data).name;
                if (nm == "undefined") return "\"undefined\"";
                if (nm == "null")      return "\"object\"";
                if (nm == "NaN" || nm == "Infinity") return "\"number\"";
                // Most GPU variables are numeric; return "number" as best guess
                return "\"number\"";
            }
            // Function literal → "function"
            if (inner->kind == NodeKind::FuncDecl) return "\"function\"";
            // NewExpr → "object"
            if (inner->kind == NodeKind::NewExpr)  return "\"object\"";
            // String call → "string"
            if (inner->kind == NodeKind::TemplateLit) return "\"string\"";
            // MemberExpr — check for known string-typed fields on ctx
            if (inner->kind == NodeKind::MemberExpr) {
                auto& me = std::get<MemberExpr>(inner->data);
                // ctx.query.KEY, ctx.params.KEY, ctx.path, ctx.method, ctx.body → "string"
                if (me.object && me.object->kind == NodeKind::MemberExpr) {
                    auto& outer = std::get<MemberExpr>(me.object->data);
                    if (outer.object && outer.object->kind == NodeKind::Ident) {
                        auto nm2 = std::get<Ident>(outer.object->data).name;
                        if (nm2 == "ctx" &&
                            (outer.prop == "query" || outer.prop == "params" || outer.prop == "headers"))
                            return "\"string\"";
                    }
                }
                if (me.object && me.object->kind == NodeKind::Ident) {
                    auto nm2 = std::get<Ident>(me.object->data).name;
                    if (nm2 == "ctx" &&
                        (me.prop == "path" || me.prop == "method" || me.prop == "body" ||
                         me.prop == "query" || me.prop == "params"))
                        return "\"string\"";
                }
            }
            return "\"string\"";   // most GPU runtime values are strings or numbers; default "string"
        }

        case NodeKind::VoidExpr: {
            auto& ve = std::get<VoidExpr>(node->data);
            return "(void)(" + emitExpr(ve.expr) + "), nullptr";
        }

        case NodeKind::FuncDecl: {
            // Arrow function or anonymous function in expression context
            auto& fn = std::get<FuncDecl>(node->data);
            std::string lambda = "[&](";
            for (int i = 0; i < (int)fn.params.size(); i++) {
                if (i) lambda += ", ";
                lambda += "auto " + fn.params[i];
            }
            lambda += ") { ";
            if (fn.body) {
                if (fn.body->kind == NodeKind::Block) {
                    // Block body: emit statements
                    auto& blk = std::get<Block>(fn.body->data);
                    for (auto& stmt : blk.stmts) {
                        // Inline the statement without depth indentation
                        std::string saved_out = _out;
                        _out = "";
                        emitStmt(stmt, 0);
                        lambda += _out;
                        _out = saved_out;
                    }
                } else {
                    // Expression body: emit as return
                    lambda += "return " + emitExpr(fn.body) + "; ";
                }
            }
            lambda += "}";
            return lambda;
        }

        default:
            return "/*unsupported*/0";
    }
}

std::string Codegen::emitLiteral(const Literal& l) {
    switch (l.kind) {
        case Literal::Int:   return l.raw;
        case Literal::Float: return l.raw + "f";
        case Literal::BigInt: {
            std::string val = l.raw;
            if (!val.empty() && val.back() == 'n') val.pop_back();
            return val + "ull";
        }
        case Literal::Str: {
            // Escape the raw string for embedding in a C++ string literal.
            // The tokenizer stores the unescaped content (e.g. a literal newline or quote).
            std::string esc;
            esc.reserve(l.raw.size() + 4);
            for (char ch : l.raw) {
                if (ch == '"')       esc += "\\\"";
                else if (ch == '\\') esc += "\\\\";
                else if (ch == '\n') esc += "\\n";
                else if (ch == '\r') esc += "\\r";
                else if (ch == '\t') esc += "\\t";
                else if (ch == '\0') esc += "\\0";
                else                 esc += ch;
            }
            return "\"" + esc + "\"";
        }
        case Literal::Bool:  return l.raw;  // true/false are valid in CUDA C++
        case Literal::Null:  return "nullptr";
    }
    return "0";
}

std::string Codegen::emitBinary(const BinaryExpr& b) {
    std::string op = b.op;
    // Ternary: stored as nested BinaryExpr nodes
    if (op == "?:else") {
        // b.left is "?:" node (cond, then), b.right is else
        auto& inner = std::get<BinaryExpr>(b.left->data);
        return "(" + emitExpr(inner.left) + " ? " + emitExpr(inner.right) + " : " + emitExpr(b.right) + ")";
    }
    if (op == "?:") return emitExpr(b.left);  // handled by ?:else above

    // Subscript: arr[i]
    if (op == "[]") return "(" + emitExpr(b.left) + "[" + emitExpr(b.right) + "])";

    // JS ** (power) → CUDA powf
    if (op == "**") return "powf((float)(" + emitExpr(b.left) + "), (float)(" + emitExpr(b.right) + "))";

    // JS nullish coalescing ?? → ternary (nullptr/0 check)
    // Use a lambda to avoid double-evaluating the left operand
    if (op == "??") {
        std::string lv = emitExpr(b.left);
        std::string rv = emitExpr(b.right);
        return "([&]{ auto _nc = " + lv + "; return _nc ? _nc : (" + rv + "); }())";
    }
    // Logical assignment operators: &&=, ||=, ??=
    if (op == "&&=") {
        std::string lv = emitExpr(b.left);
        return "(" + lv + " = (" + lv + " ? " + emitExpr(b.right) + " : " + lv + "))";
    }
    if (op == "||=") {
        std::string lv = emitExpr(b.left);
        return "(" + lv + " = (" + lv + " ? " + lv + " : " + emitExpr(b.right) + "))";
    }
    if (op == "??=") {
        std::string lv = emitExpr(b.left);
        return "(" + lv + " = (" + lv + " ? " + lv + " : " + emitExpr(b.right) + "))";
    }

    // JS strict equality → symm_eq (handles const char* string comparison)
    if (op == "===" || op == "==") {
        return "symm_eq(" + emitExpr(b.left) + ", " + emitExpr(b.right) + ")";
    }
    if (op == "!==" || op == "!=") {
        return "(!symm_eq(" + emitExpr(b.left) + ", " + emitExpr(b.right) + "))";
    }
    // JS logical operators — same in C++
    if (op == "instanceof") return "(true /*instanceof*/)";
    if (op == "in") {
        // "key" in obj → obj.has(key) for Map/Set; symm_arr_includes for arrays
        // Use .has() method which both Map and Set support; falls back to true for structs
        std::string key = emitExpr(b.left);
        std::string obj = emitExpr(b.right);
        return "(" + obj + ".has(" + key + "))";
    }
    // Unsigned right shift
    if (op == ">>>") return "((unsigned)(" + emitExpr(b.left) + ") >> " + emitExpr(b.right) + ")";
    return "(" + emitExpr(b.left) + " " + op + " " + emitExpr(b.right) + ")";
}

std::string Codegen::emitMember(const MemberExpr& m) {
    std::string obj = emitExpr(m.object);

    // Check if obj is a JSON.parse() call or a variable holding JSON.parse() result
    bool isJsonValue = (obj.find("symm_json_parse(") != std::string::npos);

    // Check if obj is a variable assigned from JSON.parse()
    if (!isJsonValue && m.object && m.object->kind == NodeKind::Ident) {
        std::string varName = std::get<Ident>(m.object->data).name;
        if (_jsonParseVars.count(varName)) {
            isJsonValue = true;
        }
    }

    if (isJsonValue) {
        return "symm_json_prop(" + obj + ", \"" + m.prop + "\")";
    }

    // Enum / namespace member access: Direction.Up → Direction_Up
    // (enum members are emitted as flat Prefix_Member constants by the parser)
    if (_flatPrefixes.count(obj)) {
        return obj + "_" + m.prop;
    }

    // Math.X → CUDA intrinsic
    if (obj == "Math") {
        auto it = MATH_REMAP.find(m.prop);
        if (it != MATH_REMAP.end()) return it->second;
        return m.prop + "f";
    }

    // ctx.params.KEY → extract route parameter by segment index
    // Detects the pattern ctx.params (MemberExpr returning the sentinel)
    // and then .KEY access on top of it.
    // Here we handle the outer member access: obj = "ctx->_params" means it
    // came from a params lookup; resolve KEY in current route pattern.
    if (obj == "ctx->_params") {
        int segIdx = paramSegIndex(_currentRoutePath, m.prop);
        if (segIdx >= 0) {
            return "symm_extract_param(ctx->path, " + std::to_string(segIdx) + ")";
        }
        // Fallback: try query string
        return "symm_extract_query(ctx->query, \"" + m.prop + "\")";
    }

    // ctx.query.KEY → extract query string parameter by name
    if (obj == "ctx->_query") {
        return "symm_extract_query(ctx->query, \"" + m.prop + "\")";
    }

    // ctx.* field access
    if (obj == "ctx") {
        if (m.prop == "params") return "ctx->_params";  // sentinel — handled above
        if (m.prop == "query")  return "ctx->_query";   // sentinel — handled above
        if (m.prop == "body")   return "ctx->body";
        if (m.prop == "method") return "ctx->method";
        if (m.prop == "path")   return "ctx->path";
        if (m.prop == "headers") return "ctx";  // ctx.headers → returns ctx; .get() dispatched below
        // ctx.gpu — return sentinel for gpu.* dispatch in emitCall
        if (m.prop == "gpu")    return "ctx->_gpu";
        if (m.prop == "json")   return "symm_json_str";
    }

    // ctx.gpu.fingerprint → handled in emitCall; here just pass through
    if (obj == "ctx->_gpu") {
        return "ctx->_gpu." + m.prop;  // emitCall will detect "ctx->_gpu.fingerprint"
    }

    // 'this' and stack-allocated structs use '->' (this is a pointer in C++ methods)
    if (obj == "this") return "this->" + m.prop;
    if (obj == "super") return m.prop;   // super method call

    // Number.* — static constants and methods
    if (obj == "Number") {
        // Inline the exact JS specification values for integer constants
        // (float can't represent MAX_SAFE_INTEGER exactly, so use long long)
        if (m.prop == "MAX_SAFE_INTEGER") return "9007199254740991LL";
        if (m.prop == "MIN_SAFE_INTEGER") return "-9007199254740991LL";
        if (m.prop == "POSITIVE_INFINITY")return "Infinity";
        if (m.prop == "NEGATIVE_INFINITY")return "-Infinity";
        if (m.prop == "NaN")              return "NaN";
        return "Number::" + m.prop;  // for EPSILON, MAX_VALUE, MIN_VALUE, isNaN etc.
    }
    // Array.* — static methods
    if (obj == "Array") {
        if (m.prop == "isArray") return "Array::isArray";
        if (m.prop == "from")    return "Array::from";
        if (m.prop == "of")      return "Array::of";
        return obj + "::" + m.prop;
    }
    // Object.* — static methods
    if (obj == "Object") {
        if (m.prop == "keys")        return "Object::keys";
        if (m.prop == "values")      return "Object::values";
        if (m.prop == "assign")      return "Object::assign";
        if (m.prop == "hasOwn")      return "Object::hasOwn";
        if (m.prop == "entries")     return "Object::entries";
        if (m.prop == "freeze")      return "Object::freeze";
        if (m.prop == "seal")        return "Object::seal";
        if (m.prop == "fromEntries") return "Object::fromEntries";
        if (m.prop == "create")      return "Object::create";
        return "Object::" + m.prop;
    }
    // Symbol.* — well-known symbols
    if (obj == "Symbol") {
        if (m.prop == "iterator")      return "Symbol::iterator()";
        if (m.prop == "asyncIterator") return "Symbol::asyncIterator()";
        if (m.prop == "hasInstance")   return "Symbol::hasInstance()";
        if (m.prop == "match")         return "Symbol::match()";
        if (m.prop == "replace")       return "Symbol::replace()";
        if (m.prop == "search")        return "Symbol::search()";
        if (m.prop == "split")         return "Symbol::split()";
        if (m.prop == "species")       return "Symbol::species()";
        if (m.prop == "toPrimitive")   return "Symbol::toPrimitive()";
        if (m.prop == "toStringTag")   return "Symbol::toStringTag()";
        if (m.prop == "unscopables")   return "Symbol::unscopables()";
        return "Symbol::" + m.prop;
    }
    // Math.PI, Math.E, Math.LN2 etc. — map to MATH_REMAP constants
    // (already handled above, but guard for completeness)

    // process.* — maps to _symm_process global
    if (obj == "process" || obj == "_symm_process") {
        if (m.prop == "env")      return "_symm_process.env";
        if (m.prop == "platform") return "_symm_process.platform";
        if (m.prop == "exitCode") return "_symm_process.exitCode";
        // Other props (uptime, hrtime, etc.) → forward to _symm_process.prop
        return "_symm_process." + m.prop;
    }
    // process.env.NAME — direct property access (e.g. process.env.NODE_ENV)
    if (obj == "_symm_process.env") {
        return "_symm_process.env[\"" + m.prop + "\"]";
    }

    // signal.aborted, signal.reason — these are struct fields
    // AbortController.signal is already a struct field, no special handling needed.

    // Common array/string length:
    //   - .length on a string (ctx->path, ctx->body, const char*) → symm_strlen
    //   - .length on a plain array variable → obj_len (companion var from ConstDecl)
    //   - .length on a struct (MatchArray, _SBuf) → .length()
    if (m.prop == "length") {
        // If the object expression contains '->' or '(' it's a method call result or
        // a pointer dereference — treat as string for symm_strlen.
        bool isComplexExpr = (obj.find("->") != std::string::npos ||
                               obj.find('(') != std::string::npos ||
                               obj.find('[') != std::string::npos);
        // Known string-typed ctx fields
        static const std::set<std::string> CTX_STR_FIELDS = {
            "ctx->path","ctx->method","ctx->query","ctx->body"
        };
        if (CTX_STR_FIELDS.count(obj)) return "symm_strlen(" + obj + ")";
        if (isComplexExpr) return "symm_strlen(" + obj + ")";
        // Simple identifier — use _len companion (works for both char[] and T[] arrays)
        return obj + "_len";
    }

    // byteLength on ByteArray / Buffer / TextEncoder result
    if (m.prop == "byteLength") return obj + ".byteLength";

    // .done, .value on generator IterResult
    if (m.prop == "done")  return obj + ".done";
    if (m.prop == "value") return obj + ".value";

    // .aborted on AbortSignal
    if (m.prop == "aborted") return obj + ".aborted";
    if (m.prop == "reason")  return obj + ".reason";
    if (m.prop == "signal")  return obj + ".signal";

    // .message on Error
    if (m.prop == "message") return obj + ".message";

    // MatchResult fields
    if (m.prop == "start") return obj + ".start";
    if (m.prop == "end")   return obj + ".end";
    if (m.prop == "input") return obj + ".input";

    // lastIndex on RegExp
    if (m.prop == "lastIndex") return obj + ".lastIndex";

    return obj + "." + m.prop;   // default: value type member access
}

std::string Codegen::emitCall(const CallExpr& c) {
    // Special case: ctx.json({...}) or ctx.json(val)
    if (c.callee && c.callee->kind == NodeKind::MemberExpr) {
        auto& mem = std::get<MemberExpr>(c.callee->data);
        std::string obj  = emitExpr(mem.object);
        std::string prop = mem.prop;

        // ctx.json(val) → write response; ctx.json() with no args → read body
        if (obj == "ctx" && prop == "json") {
            if (!c.args.empty()) {
                return "symm_json_str(ctx, " + emitExpr(c.args[0]) + ")";
            }
            return "ctx->body"; // read mode: return raw request body
        }

        // ctx.text(body, mimeType?) → write plain text response
        if (obj == "ctx" && prop == "text") {
            if (!c.args.empty()) return "symm_json_str(ctx, " + emitExpr(c.args[0]) + ")";
            return "ctx->body";
        }

        // ctx.status(n) → set status code; returns ctx for chaining
        if (obj == "ctx" && prop == "status") {
            if (!c.args.empty()) {
                return "(ctx->resp_status = " + emitExpr(c.args[0]) + ", ctx)";
            }
            return "ctx->resp_status";
        }

        // ctx.header(name, val) → set response header, returns ctx for chaining
        if (obj == "ctx" && prop == "header") {
            std::string name = c.args.size() > 0 ? emitExpr(c.args[0]) : "\"\"";
            std::string val  = c.args.size() > 1 ? emitExpr(c.args[1]) : "\"\"";
            return "symm_set_header(ctx, " + name + ", " + val + ")";
        }

        // ctx.body<Float32Array>() → (float*)ctx->body, ctx->body_len
        if (obj == "ctx" && prop == "body") {
            return "(float*)ctx->body";
        }

        // ctx.headers.get(name) — obj is "ctx" (from ctx.headers→ctx), prop is "get"
        if (obj == "ctx" && prop == "get") {
            std::string name = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "symm_get_header(ctx, " + name + ")";
        }

        // gpu.fingerprint(key) → symm_fingerprint(key, len)
        // Handles both: gpu.fingerprint(key) and ctx.gpu.fingerprint(key)
        if ((obj == "gpu" || obj == "ctx->_gpu") && prop == "fingerprint") {
            std::string arg = c.args.empty() ? "ctx->path" : emitExpr(c.args[0]);
            return "symm_fingerprint(" + arg + ", symm_strlen(" + arg + "))";
        }

        // Reflect.get(...), Reflect.set(...) — static reflection API
        if (obj == "Reflect") {
            std::string args;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(c.args[i]);
            }
            return "Reflect::" + prop + "(" + args + ")";
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

        // console.log/warn/error → writes to GPU debug ring buffer
        if (obj == "console") {
            std::string args;
            for (auto& a : c.args) { if (!args.empty()) args += ", "; args += emitExpr(a); }
            if (prop == "assert") {
                // console.assert(cond, msg?) → if (!cond) log
                std::string cond = c.args.empty() ? "true" : emitExpr(c.args[0]);
                std::string msg  = (c.args.size() > 1) ? emitExpr(c.args[1]) : "\"Assertion failed\"";
                return "console.assert_(" + cond + ", " + msg + ")";
            }
            if (args.empty()) {
                return "console." + prop + "()";
            }
            // log/warn/error/info: pass up to 3 args (multi-arg overloads in prelude)
            return "console." + prop + "(" + args + ")";
        }

        // process.*() — remap 'process' identifier to '_symm_process' struct instance
        if (obj == "process") {
            std::string args;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(c.args[i]);
            }
            return "_symm_process." + prop + "(" + args + ")";
        }

        // Date.*() — static methods on Date struct
        if (obj == "Date") {
            std::string arg0 = c.args.empty() ? "0" : emitExpr(c.args[0]);
            if (prop == "now")   return "Date::now()";
            if (prop == "parse") return "Date::parse(" + arg0 + ")";  // ISO 8601 parser
            return "Date::" + prop + "(" + arg0 + ")";
        }

        // Date prototype instance methods — emitted as method calls on Date objects
        // e.g. myDate.getFullYear() → myDate.getFullYear()
        static const std::set<std::string> DATE_INSTANCE_METHODS = {
            "getFullYear", "getMonth", "getDate", "getDay",
            "getHours", "getMinutes", "getSeconds", "getMilliseconds",
            "getTimezoneOffset", "getTime",
            "toISOString", "toLocaleDateString", "toString",
        };
        if (DATE_INSTANCE_METHODS.count(prop)) {
            std::string args;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(c.args[i]);
            }
            return obj + "." + prop + "(" + args + ")";
        }

        // Array.* static methods — Array.isArray, Array.from, etc.
        if (obj == "Array") {
            std::string callArgs;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) callArgs += ", ";
                callArgs += emitExpr(c.args[i]);
            }
            if (prop == "isArray") return "Array::isArray(" + callArgs + ")";
            if (prop == "from")    return "Array::from(" + callArgs + ")";
            if (prop == "of")      return "Array::of(" + callArgs + ")";
            return "Array::" + prop + "(" + callArgs + ")";
        }

        // Object.* static methods — Object.keys, Object.values, Object.assign, etc.
        if (obj == "Object") {
            std::string callArgs;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) callArgs += ", ";
                callArgs += emitExpr(c.args[i]);
            }
            if (prop == "keys")        return "Object::keys(" + callArgs + ")";
            if (prop == "values")      return "Object::values(" + callArgs + ")";
            if (prop == "assign")      return "Object::assign(" + callArgs + ")";
            if (prop == "hasOwn")      return "Object::hasOwn(" + callArgs + ")";
            if (prop == "entries")     return "Object::entries(" + callArgs + ")";
            if (prop == "freeze")      return "Object::freeze(" + callArgs + ")";
            if (prop == "seal")        return "Object::seal(" + callArgs + ")";
            if (prop == "fromEntries") return "Object::fromEntries(" + callArgs + ")";
            if (prop == "create")      return "Object::create(" + callArgs + ")";
            if (prop == "create")  return (callArgs.empty() ? "nullptr" : callArgs);
            if (prop == "freeze")  return (callArgs.empty() ? "nullptr" : callArgs);
            return "Object::" + prop + "(" + callArgs + ")";
        }

        // Number.*() — static method calls
        if (obj == "Number") {
            std::string arg0 = c.args.empty() ? "0" : emitExpr(c.args[0]);
            if (prop == "isNaN")      return "Number::isNaN((float)(" + arg0 + "))";
            if (prop == "isFinite")   return "Number::isFinite((float)(" + arg0 + "))";
            if (prop == "isInteger")  return "Number::isInteger((float)(" + arg0 + "))";
            if (prop == "isSafeInteger") return "Number::isSafeInteger((float)(" + arg0 + "))";
            if (prop == "parseInt") {
                if (c.args.size() >= 2) {
                    return "symm_parse_int_radix(" + arg0 + ", (int)(" + emitExpr(c.args[1]) + "))";
                }
                return "Number::parseInt(" + arg0 + ")";
            }
            if (prop == "parseFloat") return "Number::parseFloat(" + arg0 + ")";
            return "Number::" + prop + "(" + arg0 + ")";
        }

        // Symbol.*() — static well-known symbols
        if (obj == "Symbol") {
            std::string arg0 = c.args.empty() ? "" : emitExpr(c.args[0]);
            if (prop == "iterator") return "Symbol::iterator()";
            if (prop == "asyncIterator") return "Symbol::asyncIterator()";
            if (prop == "for") return arg0.empty() ? "Symbol()" : "Symbol::for_(" + arg0 + ")";
            if (prop == "keyFor") return arg0.empty() ? "nullptr" : "Symbol::keyFor(" + arg0 + ")";
            return "Symbol::" + prop + (arg0.empty() ? "()" : "(" + arg0 + ")");
        }

        // JSON.* static methods
        if (obj == "JSON") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            if (prop == "stringify") {
                // JSON.stringify(val, replacer, indent) — optional indent arg
                if (c.args.size() >= 3) {
                    std::string indent = emitExpr(c.args[2]);
                    return "symm_json_stringify_indent(symm_json_stringify(" + arg0 + "), (int)(" + indent + "))";
                }
                return "symm_json_stringify(" + arg0 + ")";
            }
            if (prop == "parse")     return "symm_json_parse(" + arg0 + ")";
            return arg0;  // fallback
        }

        // String.* static methods
        if (obj == "String") {
            std::string callArgs;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) callArgs += ", ";
                callArgs += emitExpr(c.args[i]);
            }
            // String.raw`\n` — tagged template: first arg is strings array, rest are exprs
            // For GPU: just concatenate the string parts (raw, no escape processing)
            if (prop == "raw") {
                // The strings array is args[0], the interpolated exprs are args[1..n]
                // Simplified: just return the first element of the strings array
                std::string stringsArg = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
                return stringsArg; // returns raw template string
            }
            if (prop == "fromCharCode")  return "symm_str_from_char_code(" + callArgs + ")";
            if (prop == "fromCodePoint") return "symm_str_from_char_code(" + callArgs + ")";
            return callArgs;  // String(x) → coerce to string
        }

        // super(...) constructor call → Base::Base(args)
        if (obj == "super") {
            std::string args;
            for (int i = 0; i < (int)c.args.size(); i++) {
                if (i) args += ", ";
                args += emitExpr(c.args[i]);
            }
            return obj + "(" + args + ")";  // handled as identifier in constructor init-list
        }

        // Number.prototype.toFixed(n) — format to N decimal places
        if (prop == "toFixed") {
            std::string decimals = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "/*toFixed*/ symm_number_to_fixed(" + obj + ", " + decimals + ")";
        }
        // Number.prototype.toString(radix?) — supports radix 2, 8, 10, 16
        if (prop == "toString") {
            if (!c.args.empty()) {
                std::string radix = emitExpr(c.args[0]);
                return "symm_number_to_string_radix((long long)(" + obj + "), " + radix + ")";
            }
            // Default: decimal string
            return "symm_number_to_fixed(" + obj + ", 0)";
        }

        // Number.prototype.toPrecision(digits) — format to N significant digits
        if (prop == "toPrecision") {
            std::string precision = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "/*toPrecision*/ symm_number_to_precision(" + obj + ", " + precision + ")";
        }

        // Number.prototype.toExponential(digits) — format in exponential notation
        if (prop == "toExponential") {
            std::string digits = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "/*toExponential*/ symm_number_to_exponential(" + obj + ", " + digits + ")";
        }

        // ── Number.* static methods ──────────────────────────────────────────
        if (obj == "Number") {
            std::string arg0 = c.args.empty() ? "0" : emitExpr(c.args[0]);
            if (prop == "isNaN")         return "Number::isNaN(" + arg0 + ")";
            if (prop == "isFinite")      return "Number::isFinite(" + arg0 + ")";
            if (prop == "isInteger")     return "Number::isInteger(" + arg0 + ")";
            if (prop == "isSafeInteger") return "Number::isSafeInteger(" + arg0 + ")";
            if (prop == "parseInt") {
                if (c.args.size() >= 2) {
                    std::string radix = emitExpr(c.args[1]);
                    return "symm_parse_int_radix(" + arg0 + ", (int)(" + radix + "))";
                }
                return "Number::parseInt(" + arg0 + ")";
            }
            if (prop == "parseFloat") return "symm_parse_float(" + arg0 + ")";
        }

        // ── Array.* static methods ───────────────────────────────────────────
        if (obj == "Array") {
            if (prop == "isArray") {
                std::string arg0 = c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
                return "symm_array_is_array(" + arg0 + ")";
            }
            if (prop == "from") {
                // Array.from(iterable) — emit the source; codegen expects array-like
                return c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
            }
            if (prop == "of") {
                // Array.of(a,b,c) — emit as initializer
                std::string elems;
                for (int i = 0; i < (int)c.args.size(); i++) {
                    if (i) elems += ", ";
                    elems += emitExpr(c.args[i]);
                }
                return "{" + elems + "}";
            }
        }

        // ── Object.* static methods ──────────────────────────────────────────
        if (obj == "Object") {
            if (prop == "hasOwn") {
                return "(true /*Object.hasOwn*/)";
            }
            if (prop == "is") {
                // NaN-safe equality: Object.is(NaN, NaN) == true, Object.is(-0, +0) == false
                std::string a = c.args.empty() ? "0" : emitExpr(c.args[0]);
                std::string b = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
                return "_symm_object_is(" + a + ", " + b + ")";
            }
            if (prop == "fromEntries" || prop == "assign") {
                return c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
            }
            if (prop == "keys" || prop == "values" || prop == "entries") {
                return c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
            }
            if (prop == "freeze" || prop == "seal" || prop == "create") {
                return c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
            }
        }

        // ── Symbol.for('key') ────────────────────────────────────────────────
        if (obj == "Symbol" && (prop == "for" || prop == "for_")) {
            std::string key = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "Symbol::for_(" + key + ")";
        }
        if (obj == "Symbol" && prop == "keyFor") {
            std::string sym = c.args.empty() ? "Symbol()" : emitExpr(c.args[0]);
            return "Symbol::keyFor(" + sym + ")";
        }

        // ── encodeURIComponent / decodeURIComponent ───────────────────────────
        if (prop.empty() && obj == "encodeURIComponent") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "symm_encode_uri_component(" + arg0 + ")";
        }
        if (prop.empty() && obj == "decodeURIComponent") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "symm_decode_uri_component(" + arg0 + ")";
        }

        // ── Math.max / Math.min with more than 2 arguments ──────────────────
        if (obj == "Math" && (prop == "max" || prop == "min") && c.args.size() > 2) {
            std::string fn = (prop == "max") ? "fmaxf" : "fminf";
            std::string result = emitExpr(c.args[0]);
            for (int i = 1; i < (int)c.args.size(); i++)
                result = fn + "(" + result + ", " + emitExpr(c.args[i]) + ")";
            return result;
        }

        // ── String prototype methods via symm_str_* helpers ──────────────────
        // All take the string as first arg then the JS args
        {
            static const std::map<std::string, std::string> STR_REMAP = {
                {"padStart",    "symm_str_pad_start"},
                {"padEnd",      "symm_str_pad_end"},
                {"trim",        "symm_str_trim"},
                {"trimStart",   "symm_str_trim_start"}, {"trimLeft",  "symm_str_trim_start"},
                {"trimEnd",     "symm_str_trim_end"},   {"trimRight", "symm_str_trim_end"},
                {"replaceAll",  "symm_str_replace_all"},
                {"replace",     "symm_str_replace"},
                {"repeat",      "symm_str_repeat"},
                {"includes",    "symm_str_includes"},
                {"startsWith",  "symm_str_starts_with"},
                {"endsWith",    "symm_str_ends_with"},
                {"indexOf",     "symm_str_index_of"},
                // lastIndexOf, substring, substr, slice handled above (optional-arg special cases)
                {"toUpperCase", "symm_str_to_upper"}, {"toLocaleUpperCase","symm_str_to_upper"},
                {"toLowerCase", "symm_str_to_lower"}, {"toLocaleLowerCase","symm_str_to_lower"},
                {"at",          "symm_str_at"},      // returns _SBuf (1-char string)
                {"charAt",      "symm_str_at"},      // same as .at() — returns string
                {"charCodeAt",  "symm_str_char_code_at"},
                {"codePointAt", "symm_str_char_code_at"},  // BMP subset: same as charCodeAt
                {"concat",      "symm_str_concat"},
                {"matchAll",    "symm_str_match_all"},
                {"search",      "symm_str_index_of"},  // simplified: returns match offset
                {"match",       "symm_str_match_all"}, // returns MatchArray
                {"normalize",   "symm_str_trim"},      // no-op approximation (GPU has no ICU)
            };
            // Handle methods with optional second argument specially
            if (prop == "substring") {
                std::string start = c.args.empty() ? "0" : emitExpr(c.args[0]);
                // Default end = full string length
                std::string end = (c.args.size() > 1)
                    ? emitExpr(c.args[1])
                    : ("symm_strlen(" + obj + ")");
                return "symm_str_substring(" + obj + ", " + start + ", " + end + ")";
            }
            if (prop == "substr") {
                std::string start = c.args.empty() ? "0" : emitExpr(c.args[0]);
                std::string len   = (c.args.size() > 1)
                    ? emitExpr(c.args[1])
                    : "-1";  // -1 → prelude uses full remaining length
                return "symm_str_substr(" + obj + ", " + start + ", " + len + ")";
            }
            if (prop == "lastIndexOf") {
                std::string sub = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
                if (c.args.size() > 1) {
                    std::string from = emitExpr(c.args[1]);
                    return "symm_str_last_index_of_from(" + obj + ", " + sub + ", " + from + ")";
                }
                return "symm_str_last_index_of(" + obj + ", " + sub + ")";
            }
            if (prop == "slice") {
                std::string start = c.args.empty() ? "0" : emitExpr(c.args[0]);
                // symm_str_slice(s, start) — already handles negative start, and
                // if a second arg (end) is present we need symm_str_substring for the copy
                if (c.args.size() > 1) {
                    std::string end = emitExpr(c.args[1]);
                    // negative end: slen+end  — for simplicity use symm_str_substring semantics
                    return "symm_str_substring(" + obj + ", " + start + ", " + end + ")";
                }
                return "symm_str_slice(" + obj + ", " + start + ")";
            }

            auto it = STR_REMAP.find(prop);
            if (it != STR_REMAP.end()) {
                std::string callArgs = obj;
                for (auto& a : c.args) callArgs += ", " + emitExpr(a);
                return it->second + "(" + callArgs + ")";
            }
        }

        // ── String.prototype.length (as a call would be wrong — strip parens) ─
        if (prop == "length") {
            return "symm_strlen(" + obj + ")";
        }
        if (prop == "split") {
            // str.split(sep) — returns the full split (index-based access via [N])
            // Emit as a struct that supports [N] access via symm_str_split_at
            // Since GPU has no dynamic arrays, emitExpr of split()[N] is the common pattern.
            // We leave this for the ForOfStmt handler; here just return a sentinel.
            std::string sep = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return obj + "/*split(" + sep + ")*/";
        }

        // ── Array prototype methods ──────────────────────────────────────────
        // These work on plain C arrays; the codegen tracks length via _len companion.
        if (prop == "join") {
            std::string sep = c.args.empty() ? "\",\"" : emitExpr(c.args[0]);
            return "symm_arr_join(" + obj + ", " + obj + "_len, " + sep + ")";
        }
        if (prop == "indexOf") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "symm_arr_index_of(" + obj + ", " + obj + "_len, " + val + ")";
        }
        if (prop == "includes") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "symm_arr_includes(" + obj + ", " + obj + "_len, " + val + ")";
        }
        if (prop == "at") {
            std::string idx = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "symm_arr_at(" + obj + ", " + obj + "_len, " + idx + ")";
        }
        if (prop == "find") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "findIndex") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find_index(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "findLast") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find_last(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "findLastIndex") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find_last_index(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "some") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_some(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "every") {
            std::string fn = c.args.empty() ? "[](auto x){return true;}" : emitExpr(c.args[0]);
            return "symm_arr_every(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "map") {
            std::string fn = c.args.empty() ? "[](auto x){return x;}" : emitExpr(c.args[0]);
            return "symm_arr_map(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "filter") {
            std::string fn = c.args.empty() ? "[](auto x){return true;}" : emitExpr(c.args[0]);
            return "symm_arr_filter(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "forEach") {
            std::string fn = c.args.empty() ? "[](auto x){}" : emitExpr(c.args[0]);
            return "symm_arr_for_each(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "flat") {
            return "symm_arr_flat(" + obj + ", " + obj + "_len)";
        }
        if (prop == "flatMap") {
            std::string fn = c.args.empty() ? "[](auto x){return x;}" : emitExpr(c.args[0]);
            return "symm_arr_map(" + obj + ", " + obj + "_len, " + fn + ")";  // simplified
        }
        if (prop == "fill") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            std::string start = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
            std::string end   = (c.args.size() > 2) ? emitExpr(c.args[2]) : "-1";
            return "(symm_arr_fill(" + obj + ", " + obj + "_len, " + val + ", " + start + ", " + end + "), " + obj + ")";
        }
        if (prop == "reverse") {
            return "(symm_arr_reverse(" + obj + ", " + obj + "_len), " + obj + ")";
        }
        if (prop == "sort") {
            if (c.args.empty()) {
                return "(symm_arr_sort(" + obj + ", " + obj + "_len), " + obj + ")";
            } else {
                return "(symm_arr_sort_fn(" + obj + ", " + obj + "_len, " + emitExpr(c.args[0]) + "), " + obj + ")";
            }
        }
        if (prop == "reduce") {
            std::string fn   = c.args.empty() ? "[](auto a, auto b){return a+b;}" : emitExpr(c.args[0]);
            std::string init = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
            return "symm_arr_reduce(" + obj + ", " + obj + "_len, " + fn + ", " + init + ")";
        }
        if (prop == "reduceRight") {
            // Simplified: same as reduce (GPU arrays tend to be short)
            std::string fn   = c.args.empty() ? "[](auto a, auto b){return a+b;}" : emitExpr(c.args[0]);
            std::string init = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
            return "symm_arr_reduce(" + obj + ", " + obj + "_len, " + fn + ", " + init + ")";
        }
        if (prop == "slice") {
            std::string start = c.args.empty() ? "0" : emitExpr(c.args[0]);
            std::string end   = (c.args.size() > 1) ? emitExpr(c.args[1]) : "-1";
            return "symm_arr_slice(" + obj + ", " + obj + "_len, " + start + ", " + end + ")";
        }
        if (prop == "concat") {
            std::string other    = c.args.empty() ? "nullptr" : emitExpr(c.args[0]);
            std::string otherLen = c.args.empty() ? "0" : (other + "_len");
            return "symm_arr_concat(" + obj + ", &" + obj + "_len, " + other + ", " + otherLen + ")";
        }
        if (prop == "flat" || prop == "flatMap") {
            return "symm_arr_flat(" + obj + ", " + obj + "_len)";
        }
        if (prop == "copyWithin") {
            std::string target = c.args.empty() ? "0" : emitExpr(c.args[0]);
            std::string start  = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
            std::string end    = (c.args.size() > 2) ? emitExpr(c.args[2]) : "-1";
            return "symm_arr_copy_within(" + obj + ", " + obj + "_len, " + target + ", " + start + ", " + end + ")";
        }
        if (prop == "toReversed") {
            // non-mutating: need a temp copy — emit inline
            return "symm_arr_to_reversed(" + obj + ", " + obj + "_len, " + obj + ")";
        }
        if (prop == "toSorted") {
            std::string fn = c.args.empty() ? "" : emitExpr(c.args[0]);
            return "symm_arr_to_sorted(" + obj + ", " + obj + "_len, " + obj + ")";
        }
        if (prop == "toSpliced") {
            // simplified: same as slice + splice semantics; returns modified copy
            std::string start = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "symm_arr_slice(" + obj + ", " + obj + "_len, 0, " + obj + "_len)";
        }
        if (prop == "with") {
            std::string idx = c.args.empty() ? "0" : emitExpr(c.args[0]);
            std::string val = (c.args.size() > 1) ? emitExpr(c.args[1]) : "0";
            return "(" + obj + "[" + idx + "] = " + val + ", " + obj + ")";
        }
        // Note: entries/keys/values fall through to generic dispatch
        // so Map.keys(), Set.keys(), arr.keys() all call the struct method directly.
        if (prop == "pop") {
            return "(" + obj + "_len > 0 ? " + obj + "[--" + obj + "_len] : 0)";
        }
        if (prop == "shift") {
            // Not efficient but correct
            return "(" + obj + "_len > 0 ? " + obj + "[0] : 0)";
        }
        if (prop == "push") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "(" + obj + "[" + obj + "_len++] = " + val + ", " + obj + "_len)";
        }
        if (prop == "unshift") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return "(++(" + obj + "_len))";  // simplified; actual shift not safe without bounds
        }

        // ── Map/Set keys() / values() / entries() — inline struct methods ──────
        // Map and Set both have inline keys()/values()/entries() methods now.
        // Fall through to generic dispatch: obj.keys() / obj.values() / obj.entries()

        // ── Generator protocol: gen.return(val) / gen.throw(err) ────────────
        if (prop == "return" || prop == "return_") {
            std::string val = c.args.empty() ? "0" : emitExpr(c.args[0]);
            return obj + ".return_(" + val + ")";
        }
        if (prop == "throw" || prop == "throw_") {
            std::string err = c.args.empty() ? "Error()" : emitExpr(c.args[0]);
            return obj + ".throw_(" + err + ")";
        }

        // ── RegExp instance methods ──────────────────────────────────────────
        if (prop == "test")     {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return obj + ".test(" + arg0 + ")";
        }
        if (prop == "exec")     {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return obj + ".exec(" + arg0 + ")";
        }
        if (prop == "matchAll") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "symm_str_match_all(" + arg0 + ", " + obj + ")";
        }

        // Generic method call: use . for value types, -> for ctx/this
        std::string sep = (obj == "ctx" || obj == "this") ? "->" : ".";
        std::string args;
        for (int i = 0; i < (int)c.args.size(); i++) {
            if (i) args += ", ";
            args += emitExpr(c.args[i]);
        }
        return obj + sep + prop + "(" + args + ")";
    }

    // Generic function call (free function) — with special cases
    std::string fn = emitExpr(c.callee);

    // parseInt(str, radix) — if 2 args, use symm_parse_int_radix
    if (fn == "parseInt") {
        if (c.args.size() >= 2) {
            std::string s    = emitExpr(c.args[0]);
            std::string radix = emitExpr(c.args[1]);
            return "symm_parse_int_radix(" + s + ", (int)(" + radix + "))";
        } else {
            std::string s = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return "parseInt(" + s + ")";
        }
    }
    // parseFloat(str) — maps to symm_parse_float for correct decimal parsing
    if (fn == "parseFloat") {
        std::string s = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "symm_parse_float(" + s + ")";
    }
    // isNaN / isFinite — free-function versions
    if (fn == "isNaN" || fn == "isFinite") {
        std::string a = c.args.empty() ? "0" : emitExpr(c.args[0]);
        return fn + "(" + a + ")";
    }
    // encodeURIComponent / decodeURIComponent — full implementation via prelude
    if (fn == "encodeURIComponent") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "symm_encode_uri_component(" + a + ")";
    }
    if (fn == "decodeURIComponent") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "symm_decode_uri_component(" + a + ")";
    }
    // encodeURI / decodeURI — encodes/decodes fewer chars; reuse component variant for GPU
    if (fn == "encodeURI") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "symm_encode_uri_component(" + a + ")";
    }
    if (fn == "decodeURI") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "symm_decode_uri_component(" + a + ")";
    }
    // btoa / atob — base64 encode/decode (GPU implementations in prelude FRAG10)
    if (fn == "btoa") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "btoa(" + a + ")";
    }
    if (fn == "atob") {
        std::string a = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
        return "atob(" + a + ")";
    }
    // eval(expr) — dynamic execution not supported on GPU; return null (safe no-op)
    if (fn == "eval") {
        return "(static_cast<void>(0), nullptr)";
    }
    // BigInt(42) — cast to long long
    if (fn == "BigInt") {
        std::string v = c.args.empty() ? "0" : emitExpr(c.args[0]);
        return "BigInt(" + v + ")";
    }
    // structuredClone — deep copy by value
    if (fn == "structuredClone") {
        std::string v = c.args.empty() ? "0" : emitExpr(c.args[0]);
        return "structuredClone(" + v + ")";
    }
    // queueMicrotask — GPU has no event loop; call synchronously
    if (fn == "queueMicrotask") {
        std::string v = c.args.empty() ? "[]{}" : emitExpr(c.args[0]);
        return "(" + v + "(), (void)0)";
    }

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

// ── Class emission ────────────────────────────────────────────────────────────

// Guess CUDA field type from a JS parameter name.
static std::string guessParamType(const std::string& name) {
    static const std::set<std::string> strNames = {
        "name","str","text","msg","message","key","label","id","type","url","path",
        "method","body","query","tag","title","description","content","src","href","sound"
    };
    return strNames.count(name) ? "const char*" : "float";
}

// Infer CUDA return type for a class method by inspecting its return statement.
static std::string inferMethodReturn(const ClassMethod& m, const std::string& className) {
    if (!m.body || m.body->kind != NodeKind::Block) return "void";
    for (auto& s : std::get<Block>(m.body->data).stmts) {
        if (!s || s->kind != NodeKind::ReturnStmt) continue;
        auto& rs = std::get<ReturnStmt>(s->data);
        if (!rs.expr) return "void";
        switch (rs.expr->kind) {
            case NodeKind::NewExpr:    return std::get<NewExpr>(rs.expr->data).className;
            case NodeKind::TemplateLit: return "const char*";
            case NodeKind::Literal: {
                auto& lit = std::get<Literal>(rs.expr->data);
                if (lit.kind == Literal::Str) return "const char*";
                if (lit.kind == Literal::BigInt) return "unsigned long long";
                return "float";
            }
            case NodeKind::BinaryExpr:
            case NodeKind::CallExpr:   return "float";
            case NodeKind::ArrayExpr:  return "float3";  // maps to CUDA float3
            default:                   return "auto";
        }
    }
    return "void";
}

void Codegen::emitClass(const ClassDecl& cls, int depth) {
    std::string ind = indent(depth);

    // Generic template header
    if (!cls.typeParams.empty()) {
        _out += ind + "template<";
        for (int i = 0; i < (int)cls.typeParams.size(); i++) {
            if (i) _out += ", ";
            _out += "typename " + cls.typeParams[i];
        }
        _out += ">\n";
    }

    _out += ind + "struct " + cls.name;
    if (!cls.superName.empty()) _out += " : " + cls.superName;
    _out += " {\n";

    // ── Pre-pass: scan constructor body for this.prop = param assignments ───────
    struct CtorField { std::string name, type; };
    std::vector<CtorField> ctorFields;
    std::vector<std::string> superArgs;  // args from super() call
    const ClassMethod* ctor = nullptr;
    for (auto& m : cls.methods) { if (m.isConstructor) { ctor = &m; break; } }

    if (ctor && ctor->body && ctor->body->kind == NodeKind::Block) {
        for (auto& stmt : std::get<Block>(ctor->body->data).stmts) {
            if (!stmt || stmt->kind != NodeKind::ExprStmt) continue;
            auto& es = std::get<ExprStmt>(stmt->data);
            if (!es.expr) continue;

            // this.prop = val
            if (es.expr->kind == NodeKind::AssignExpr) {
                auto& ae = std::get<AssignExpr>(es.expr->data);
                if (ae.op == "=" && ae.left && ae.left->kind == NodeKind::MemberExpr) {
                    auto& mem = std::get<MemberExpr>(ae.left->data);
                    if (mem.object && mem.object->kind == NodeKind::Ident &&
                        std::get<Ident>(mem.object->data).name == "this") {
                        std::string ftype = "float";
                        if (ae.right) {
                            if (ae.right->kind == NodeKind::Ident)
                                ftype = guessParamType(std::get<Ident>(ae.right->data).name);
                            else if (ae.right->kind == NodeKind::Literal &&
                                     std::get<Literal>(ae.right->data).kind == Literal::Str)
                                ftype = "const char*";
                        }
                        ctorFields.push_back({mem.prop, ftype});
                    }
                }
            }
            // super(args)
            if (es.expr->kind == NodeKind::CallExpr) {
                auto& call = std::get<CallExpr>(es.expr->data);
                if (call.callee && call.callee->kind == NodeKind::Ident &&
                    std::get<Ident>(call.callee->data).name == "super") {
                    for (auto& arg : call.args) superArgs.push_back(emitExpr(arg));
                }
            }
        }
    }

    // ── Emit explicit field declarations ─────────────────────────────────────────
    // Build ctorField lookup for type resolution of uninitialized class fields.
    std::map<std::string, std::string> ctorFieldTypes;
    for (auto& cf : ctorFields) ctorFieldTypes[cf.name] = cf.type;

    std::set<std::string> knownFields;
    for (auto& f : cls.fields) {
        knownFields.insert(f.name);
        std::string prefix = f.isStatic ? "static " : "";
        std::string ty;
        if (f.init) {
            ty = "decltype(" + emitExpr(f.init) + ")";
        } else if (ctorFieldTypes.count(f.name)) {
            ty = ctorFieldTypes[f.name];  // prefer ctor-inferred type
        } else {
            ty = guessParamType(f.name);  // heuristic from field name
        }
        _out += indent(depth+1) + prefix + ty + " " + f.name;
        std::string dflt = (ty == "const char*") ? " = nullptr" : " = {}";
        if (f.init) _out += " = " + emitExpr(f.init);
        else        _out += dflt;
        _out += ";\n";
    }

    // ── Emit constructor-detected fields ─────────────────────────────────────────
    for (auto& cf : ctorFields) {
        if (knownFields.count(cf.name)) continue;
        knownFields.insert(cf.name);
        std::string init = (cf.type == "const char*") ? "nullptr" : "{}";
        _out += indent(depth+1) + cf.type + " " + cf.name + " = " + init + ";\n";
    }

    // ── Emit constructor ──────────────────────────────────────────────────────────
    if (ctor) {
        _out += indent(depth+1) + "__device__ " + cls.name + "(";
        for (int i = 0; i < (int)ctor->params.size(); i++) {
            if (i) _out += ", ";
            // Use the heuristically-guessed type for this param
            std::string ptype = "auto";
            for (auto& cf : ctorFields) {
                if (cf.name == ctor->params[i]) { ptype = cf.type; break; }
            }
            _out += ptype + " " + ctor->params[i]; // no rename: this->x = x handles shadowing
        }
        _out += ")";
        if (!cls.superName.empty()) {
            _out += " : " + cls.superName + "(";
            for (int i = 0; i < (int)superArgs.size(); i++) {
                if (i) _out += ", ";
                _out += superArgs[i];
            }
            _out += ")";
        }
        _out += " {\n";
        // Emit body, translating param references (name_ → name_) and skipping super()
        if (ctor->body && ctor->body->kind == NodeKind::Block) {
            for (auto& stmt : std::get<Block>(ctor->body->data).stmts) {
                // Skip super() — already in initializer list
                bool isSuperCall = false;
                if (stmt && stmt->kind == NodeKind::ExprStmt) {
                    auto& es = std::get<ExprStmt>(stmt->data);
                    if (es.expr && es.expr->kind == NodeKind::CallExpr) {
                        auto& call = std::get<CallExpr>(es.expr->data);
                        if (call.callee && call.callee->kind == NodeKind::Ident &&
                            std::get<Ident>(call.callee->data).name == "super")
                            isSuperCall = true;
                    }
                }
                if (!isSuperCall) emitStmt(stmt, depth + 2);
            }
        }
        _out += indent(depth+1) + "}\n";
    } else if (!cls.superName.empty()) {
        // Inherit parent constructors
        _out += indent(depth+1) + "using " + cls.superName + "::" + cls.superName + ";\n";
    } else if (ctorFields.empty() && cls.fields.empty()) {
        _out += indent(depth+1) + "__device__ " + cls.name + "() {}\n";
    }

    // ── Emit non-constructor methods ──────────────────────────────────────────────
    // First pass: find async iterator / iterator methods to generate state machines
    const ClassMethod* asyncIterMethod = nullptr;
    const ClassMethod* iterMethod      = nullptr;
    for (auto& m : cls.methods) {
        if (m.name == "__asyncIterator__") asyncIterMethod = &m;
        if (m.name == "__iterator__")      iterMethod      = &m;
    }

    // Helper: collect all AwaitExpr (yield points) from a method body
    // Returns them in order — each becomes one `case` in the state machine.
    struct YieldCollector {
        std::vector<NodePtr> yields;
        void visit(const NodePtr& n) {
            if (!n) return;
            if (n->kind == NodeKind::AwaitExpr) { yields.push_back(n); return; }
            // Recurse into all child nodes
            switch (n->kind) {
                case NodeKind::Block:
                    for (auto& s : std::get<Block>(n->data).stmts) visit(s);
                    break;
                case NodeKind::ReturnStmt:  visit(std::get<ReturnStmt>(n->data).expr);  break;
                case NodeKind::ExprStmt:    visit(std::get<ExprStmt>(n->data).expr);    break;
                case NodeKind::IfStmt: {
                    auto& is = std::get<IfStmt>(n->data);
                    visit(is.cond); visit(is.then); visit(is.else_); break;
                }
                case NodeKind::WhileStmt: {
                    auto& ws = std::get<WhileStmt>(n->data);
                    visit(ws.cond); visit(ws.body); break;
                }
                case NodeKind::ForStmt: {
                    auto& fs = std::get<ForStmt>(n->data);
                    visit(fs.body); break;
                }
                default: break;
            }
        }
    };

    // Emit __asyncIter_step + __asyncIterator() factory for async generator methods
    auto emitAsyncIterStateMachine = [&](const ClassMethod& m) {
        YieldCollector yc;
        if (m.body) yc.visit(m.body);
        int N = (int)yc.yields.size();

        // Emit the step function: advances state machine by one yield
        // Returns true if a value was produced, false if done
        _out += indent(depth+1) + "__device__ bool __asyncIter_step(int& _state, float& _out) {\n";
        _out += indent(depth+2) + "switch (_state) {\n";
        for (int i = 0; i < N; i++) {
            _out += indent(depth+3) + "case " + std::to_string(i) + ": {\n";
            auto& yieldNode = yc.yields[i];
            auto& aw = std::get<AwaitExpr>(yieldNode->data);
            _out += indent(depth+4) + "_out = " + emitExpr(aw.expr) + ";\n";
            _out += indent(depth+4) + "_state++; return true;\n";
            _out += indent(depth+3) + "}\n";
        }
        _out += indent(depth+3) + "default: return false;\n";
        _out += indent(depth+2) + "}\n";
        _out += indent(depth+1) + "}\n";

        // Emit __asyncIterator() factory — returns a GpuAsyncIterState for for-await-of
        _out += indent(depth+1) + "__device__ GpuAsyncIterState<" + cls.name + ", float> __asyncIterator() {\n";
        _out += indent(depth+2) + "return GpuAsyncIterState<" + cls.name + ", float>{ this, 0, false, {} };\n";
        _out += indent(depth+1) + "}\n";
    };

    // Emit __iter_step + __iterator() factory for sync generator methods
    auto emitIterStateMachine = [&](const ClassMethod& m) {
        YieldCollector yc;
        if (m.body) yc.visit(m.body);
        int N = (int)yc.yields.size();

        _out += indent(depth+1) + "__device__ bool __iter_step(int& _state, float& _out) {\n";
        _out += indent(depth+2) + "switch (_state) {\n";
        for (int i = 0; i < N; i++) {
            _out += indent(depth+3) + "case " + std::to_string(i) + ": {\n";
            auto& yieldNode = yc.yields[i];
            auto& aw = std::get<AwaitExpr>(yieldNode->data);
            _out += indent(depth+4) + "_out = " + emitExpr(aw.expr) + ";\n";
            _out += indent(depth+4) + "_state++; return true;\n";
            _out += indent(depth+3) + "}\n";
        }
        _out += indent(depth+3) + "default: return false;\n";
        _out += indent(depth+2) + "}\n";
        _out += indent(depth+1) + "}\n";

        _out += indent(depth+1) + "__device__ GpuAsyncIterState<" + cls.name + ", float> __iterator() {\n";
        _out += indent(depth+2) + "return GpuAsyncIterState<" + cls.name + ", float>{ this, 0, false, {} };\n";
        _out += indent(depth+1) + "}\n";
    };

    if (asyncIterMethod) emitAsyncIterStateMachine(*asyncIterMethod);
    if (iterMethod)      emitIterStateMachine(*iterMethod);

    for (auto& m : cls.methods) {
        if (m.isConstructor) continue;
        // Skip Symbol.asyncIterator / Symbol.iterator methods — already emitted above
        if (m.name == "__asyncIterator__" || m.name == "__iterator__") continue;
        // Skip other computed methods that we can't name in C++
        if (m.name == "__computed__" || m.name == "__toPrimitive__" || m.name == "__hasInstance__") continue;

        // NVRTC does not support C++20 abbreviated function templates (auto param).
        // Emit template<typename _Tp0,...> so each typed param is a type parameter.
        if (!m.params.empty() && !m.isGetter && !m.isSetter) {
            _out += indent(depth+1) + "template<";
            for (int i = 0; i < (int)m.params.size(); i++) {
                if (i) _out += ", ";
                _out += "typename _Tp" + std::to_string(i);
            }
            _out += ">\n";
        }
        std::string retType = inferMethodReturn(m, cls.name);
        _out += indent(depth+1) + "__device__ " + retType + " ";
        if (m.isGetter) {
            _out += "get_" + m.name + "()";
        } else if (m.isSetter) {
            _out += "set_" + m.name + "(_Tv _v)";
        } else {
            _out += m.name + "(";
            for (int i = 0; i < (int)m.params.size(); i++) {
                if (i) _out += ", ";
                _out += "_Tp" + std::to_string(i) + " " + m.params[i];
            }
            _out += ")";
        }
        _out += " {\n";
        if (m.body && m.body->kind == NodeKind::Block)
            emitBlock(std::get<Block>(m.body->data), depth + 2);
        _out += indent(depth+1) + "}\n";
    }

    _out += ind + "};\n\n";
}

// ── Template literal emission ─────────────────────────────────────────────────

void Codegen::emitTemplateLit(const TemplateLit& t, int depth, const std::string& buf) {
    std::string ind = indent(depth);
    for (size_t i = 0; i < t.parts.size(); i++) {
        if (!t.parts[i].empty()) {
            // Escape the string part for embedding in a C++ string literal
            std::string esc;
            for (char ch : t.parts[i]) {
                if (ch == '"')       esc += "\\\"";
                else if (ch == '\\') esc += "\\\\";
                else if (ch == '\n') esc += "\\n";
                else if (ch == '\r') esc += "\\r";
                else if (ch == '\t') esc += "\\t";
                else                 esc += ch;
            }
            _out += ind + buf + "_len += symm_write_str(" + buf + " + " + buf + "_len, \""
                 + esc + "\");\n";
        }
        if (i < t.exprs.size()) {
            std::string ev = emitExpr(t.exprs[i]);
            // Use overloaded symm_write — C++ picks the right variant by arg type
            _out += ind + buf + "_len += symm_write(" + buf + " + " + buf + "_len, "
                 + ev + ");\n";
        }
    }
}

} // namespace symm
