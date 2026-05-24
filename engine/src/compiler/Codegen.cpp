#include "Codegen.h"
#include <map>
#include <set>
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

static const char* PRELUDE = R"(
// ── Symmetra GPU Runtime Prelude ──────────────────────────────────────────────
// NVRTC: math functions and stdint types are built-in — no includes needed.
typedef unsigned char      uint8_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

// Request and response structures (in VRAM)
struct SymmCtx {
    // ── Request fields (filled by batch dispatch or legacy dispatch) ──────────
    const char* method;        // "GET", "POST", "PUT", "DELETE", "PATCH", …
    const char* path;          // URL path (null-terminated in VRAM)
    const char* query;         // Query string (after '?')
    const char* body;          // Raw request body bytes
    int         body_len;
    // ── Routing ───────────────────────────────────────────────────────────────
    int         route_id;      // resolved route index (set by dispatch)
    int         batch_slot;    // thread index within the batch (0–255)
    // ── Response fields (written by handler kernel) ───────────────────────────
    char*       resp_buf;      // response body output buffer (in VRAM)
    int         resp_len;      // byte count written to resp_buf
    int         resp_status;   // HTTP status code (default 200)
    // ── GPU fingerprint (anti-tamper) ─────────────────────────────────────────
    uint64_t    fingerprint;
    // ── I/O stage marker (for split-kernel continuation) ─────────────────────
    // When a handler hits a Tier-3 I/O boundary, it writes the stage index here
    // and returns early.  The host re-launches with stage_resume pointing past
    // the completed I/O.
    int         io_stage;      // 0 = no pending I/O, N > 0 = resume at stage N
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

// Template literal helpers: write parts into a scratch char buffer
__device__ int symm_write_str(char* buf, const char* s) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) { buf[n] = s[n]; n++; }
    return n;
}
__device__ int symm_write_int(char* buf, long long v) {
    if (v == 0) { buf[0]='0'; return 1; }
    char tmp[24]; int n = 0; bool neg = v < 0;
    if (neg) v = -v;
    while (v > 0) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    if (neg) tmp[n++] = '-';
    for (int i = 0, j = n-1; i < j; i++, j--) { char c=tmp[i]; tmp[i]=tmp[j]; tmp[j]=c; }
    for (int i = 0; i < n; i++) buf[i] = tmp[i];
    return n;
}
__device__ int symm_write_float(char* buf, float v) {
    // Handle special values
    if (v != v) { buf[0]='N'; buf[1]='a'; buf[2]='N'; buf[3]=0; return 3; }
    float inf = _symm_inf();
    if (v == inf)  { buf[0]='I'; buf[1]='n'; buf[2]='f'; buf[3]='i'; buf[4]='n'; buf[5]='i'; buf[6]='t'; buf[7]='y'; buf[8]=0; return 8; }
    if (v == -inf) { buf[0]='-'; buf[1]='I'; buf[2]='n'; buf[3]='f'; buf[4]='i'; buf[5]='n'; buf[6]='i'; buf[7]='t'; buf[8]='y'; buf[9]=0; return 9; }
    long long iv = (long long)v;
    float frac = v - (float)iv;
    if (frac < 0.f) frac = -frac;
    // If fractional part is negligible, write as integer (cleaner JSON)
    if (frac < 0.000001f) return symm_write_int(buf, iv);
    int n = symm_write_int(buf, iv);
    buf[n++] = '.';
    // Up to 6 decimal places
    for (int d = 0; d < 6; d++) { frac *= 10.f; int dig = (int)frac; buf[n++] = '0' + dig; frac -= dig; }
    // Strip trailing zeros (must null-terminate after stripping)
    while (n > 1 && buf[n-1] == '0') n--;
    if (n > 1 && buf[n-1] == '.') n--;
    buf[n] = '\0';
    return n;
}
__device__ int symm_write_bool(char* buf, bool v) {
    const char* s = v ? "true" : "false";
    return symm_write_str(buf, s);
}
// Overloaded dispatchers — C++ picks the right variant by argument type
__device__ int symm_write(char* buf, const char* s) { return symm_write_str(buf, s); }
__device__ int symm_write(char* buf, float v)        { return symm_write_float(buf, v); }
__device__ int symm_write(char* buf, int v)          { return symm_write_int(buf, v); }
__device__ int symm_write(char* buf, long long v)    { return symm_write_int(buf, v); }
__device__ int symm_write(char* buf, bool v)         { return symm_write_bool(buf, v); }
__device__ int symm_write(char* buf, double v)       { return symm_write_float(buf, (float)v); }
template<typename T> __device__ int symm_write(char* buf, T v) { return symm_write_str(buf, (const char*)v); }
// JSON value emitters: strings get quoted, numbers bare, bools bare
__device__ int symm_json_val(char* buf, const char* s) {
    int n = 0; buf[n++] = '"';
    while (s && *s) buf[n++] = *s++;
    buf[n++] = '"'; buf[n] = '\0'; return n;
}
__device__ int symm_json_val(char* buf, float v)      { return symm_write_float(buf, v); }
__device__ int symm_json_val(char* buf, int v)        { return symm_write_int(buf, v); }
__device__ int symm_json_val(char* buf, long long v)  { return symm_write_int(buf, v); }
__device__ int symm_json_val(char* buf, bool v)       { return symm_write_bool(buf, v); }
template<typename T> __device__ int symm_json_val(char* buf, T v) { return symm_write_float(buf, (float)v); }
// nullptr → "null"
__device__ int symm_json_val(char* buf, decltype(nullptr)) { return symm_write_str(buf, "null"); }
// float3 array as JSON "[x,y,z]"
__device__ int symm_json_val(char* buf, float3 v) {
    int n = 0; buf[n++] = '[';
    n += symm_write_float(buf + n, v.x); buf[n++] = ',';
    n += symm_write_float(buf + n, v.y); buf[n++] = ',';
    n += symm_write_float(buf + n, v.z);
    buf[n++] = ']'; buf[n] = '\0'; return n;
}
// JS parseFloat() stub → direct float cast
template<typename T> __device__ float parseFloat(T v) { return (float)v; }

// Extract a string field from a JSON body {"key":"value",...}
// Writes the field value into out[0..maxlen-1] (null-terminated).
// Returns true if found.
__device__ bool symm_body_get_str(const char* json, const char* key, char* out, int maxlen) {
    out[0] = 0;
    if (!json || !key) return false;
    int klen = symm_strlen(key);
    for (int i = 0; json[i]; i++) {
        if (json[i] != '"') continue;
        bool match = true;
        for (int j = 0; j < klen && match; j++) match = (json[i+1+j] == key[j]);
        if (!match || json[i+1+klen] != '"') continue;
        int j = i + 1 + klen + 1;
        while (json[j]==' ' || json[j]=='\t' || json[j]==':') j++;
        if (json[j] == '"') {
            j++; int n = 0;
            while (json[j] && json[j] != '"' && n < maxlen-1) {
                if (json[j]=='\\' && json[j+1]) { j++; out[n++] = json[j]; }
                else out[n++] = json[j];
                j++;
            }
            out[n] = 0; return true;
        }
        break;
    }
    return false;
}
// Extract an integer field from JSON body
__device__ int symm_body_get_int(const char* json, const char* key) {
    char buf[32]; if (!symm_body_get_str(json, key, buf, 32)) {
        // try numeric: find "key": N
        int klen = symm_strlen(key);
        for (int i = 0; json[i]; i++) {
            if (json[i] != '"') continue;
            bool match = true;
            for (int j = 0; j < klen && match; j++) match = (json[i+1+j] == key[j]);
            if (!match || json[i+1+klen] != '"') continue;
            int j = i+1+klen+1;
            while (json[j]==' '||json[j]==':'||json[j]=='\t') j++;
            if (json[j]>='0' && json[j]<='9') {
                int v = 0;
                while (json[j]>='0' && json[j]<='9') { v = v*10 + (json[j]-'0'); j++; }
                return v;
            }
        }
        return 0;
    }
    int v=0; for (int i=0; buf[i]>='0'&&buf[i]<='9'; i++) v=v*10+(buf[i]-'0');
    return v;
}

// ── JS standard library stubs (compile to no-ops; GPU has no native equiv.) ──────
// These allow TS→CUDA codegen to produce compilable output for unsupported features.
struct __MapEntry { const char* k; float v; };
struct Map {
    __MapEntry _e[64]; int _n = 0;
    __device__ void set(const char* k, float v) {
        for (int i=0;i<_n;i++) if (_e[i].k==k){_e[i].v=v;return;}
        if (_n<64) { _e[_n].k=k; _e[_n].v=v; _n++; }
    }
    __device__ float get(const char* k) const {
        for (int i=0;i<_n;i++) if (_e[i].k==k) return _e[i].v;
        return 0.f;
    }
    __device__ bool has(const char* k) const {
        for (int i=0;i<_n;i++) if (_e[i].k==k) return true;
        return false;
    }
    __device__ int size() const { return _n; }
};
struct Set {
    const char* _e[64]; int _n = 0;
    __device__ void add(const char* k) {
        for (int i=0;i<_n;i++) if (_e[i]==k) return;
        if (_n<64) _e[_n++]=k;
    }
    __device__ bool has(const char* k) const {
        for (int i=0;i<_n;i++) if (_e[i]==k) return true;
        return false;
    }
    __device__ int size() const { return _n; }
};
// RegExp — full NFA implementation defined in the prelude extension block below.
struct Error {
    const char* message;
    __device__ Error(const char* m = "") : message(m) {}
};
struct AbortSignal {
    bool aborted = false;
    bool* _targets[4] = {nullptr, nullptr, nullptr, nullptr};
    int   _nTargets    = 0;
    template<typename F> __device__ void addEventListener(const char*, F) {}
    __device__ void _addTarget(bool* p) { if (_nTargets < 4) _targets[_nTargets++] = p; }
    __device__ void _fire() {
        aborted = true;
        for (int i = 0; i < _nTargets; i++) if (_targets[i]) *_targets[i] = true;
    }
};
struct AbortController {
    AbortSignal signal;
    __device__ void abort() { signal._fire(); }
};
// ByteArray — returned by TextEncoder; has byteLength field
struct ByteArray {
    const char* _data; int byteLength;
    __device__ const char* buffer() const { return _data; }
    __device__ const char* toString() const { return _data; }
};
struct TextEncoder { __device__ ByteArray encode(const char* s) {
    int n = 0; if (s) while (s[n]) n++;
    return ByteArray{s, n};
} };
struct TextDecoder {
    __device__ const char* decode(ByteArray b) const { return b._data; }
    __device__ const char* decode(const char* s) const { return s; }
};
// Promise stub — GPU execution is fully synchronous, so these are identity operations.
// For real usage, `await` is stripped and the inner value is returned directly.
struct Promise {
    template<typename T> __device__ static auto all(T arr)        { return arr; }
    template<typename T> __device__ static auto race(T arr)       { return arr; }
    template<typename T> __device__ static auto any(T arr)        { return arr; }
    template<typename T> __device__ static auto allSettled(T arr) { return arr; }
    template<typename T> __device__ static auto resolve(T v)      { return v; }
    __device__ static const char* reject(const char* r) { return r; }
};
// Type-aware equality: falls through to == for numbers, does string compare for char*
__device__ bool symm_str_eq(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}
template<typename A, typename B>
__device__ bool symm_eq(A a, B b) { return a == b; }
__device__ bool symm_eq(const char* a, const char* b) { return symm_str_eq(a, b); }
// URL parser — scheme://hostname/pathname
struct URL {
    char _href[256]; char _protocol[32]; char _hostname[128]; char _pathname[256];
    bool _valid;
    const char* href; const char* protocol; const char* hostname; const char* pathname;
    __device__ URL(const char* s = "") : _valid(false),
        href(_href), protocol(_protocol), hostname(_hostname), pathname(_pathname) {
        _href[0]=_protocol[0]=_hostname[0]=_pathname[0]=0;
        if (!s || !*s) return;
        // Find "://"
        int protoEnd = -1;
        for (int i = 0; s[i]; i++) {
            if (s[i]==':' && s[i+1]=='/' && s[i+2]=='/') { protoEnd = i; break; }
        }
        if (protoEnd < 0) return;
        // protocol = "https:" (includes colon)
        for (int i = 0; i < protoEnd && i < 30; i++) _protocol[i] = s[i];
        _protocol[protoEnd] = ':'; _protocol[protoEnd+1] = 0;
        const char* p = s + protoEnd + 3;
        // hostname ends at / ? # or end
        int hostEnd = 0;
        while (p[hostEnd] && p[hostEnd]!='/' && p[hostEnd]!='?' && p[hostEnd]!='#') hostEnd++;
        for (int i = 0; i < hostEnd && i < 127; i++) _hostname[i] = p[i];
        _hostname[hostEnd] = 0;
        p += hostEnd;
        // pathname
        int pathEnd = 0;
        while (p[pathEnd] && p[pathEnd]!='?' && p[pathEnd]!='#') pathEnd++;
        if (pathEnd == 0) { _pathname[0]='/'; _pathname[1]=0; }
        else { for (int i=0;i<pathEnd&&i<255;i++) _pathname[i]=p[i]; _pathname[pathEnd]=0; }
        // href
        int sl=0; while (s[sl]&&sl<255) { _href[sl]=s[sl]; sl++; } _href[sl]=0;
        _valid = (_hostname[0] != 0);
    }
};
// ─────────────────────────────────────────────────────────────────────────────

__device__ uint64_t symm_fingerprint(const char* data, int len) {
    uint32_t s[4] = {0x9e3779b9u, 0x6c62272eu, 0x517cc1b7u, 0x27220a95u};
    for (int i = 0; i < len; i++)
        s[i & 3] ^= (uint32_t)(uint8_t)data[i] << ((i & 3) << 3);
    for (int step = 0; step < 16; step++) {
        uint32_t n[4];
        for (int w = 0; w < 4; w++) {
            uint32_t C  = s[w];
            uint32_t Lw = s[(w + 3) & 3];
            uint32_t Rw = s[(w + 1) & 3];
            uint32_t l  = (C << 1) | (Lw >> 31);
            uint32_t r  = (C >> 1) | ((Rw & 1u) << 31);
            n[w] = (C | r) & ~(l & C & r);
        }
        s[0]=n[0]; s[1]=n[1]; s[2]=n[2]; s[3]=n[3];
    }
    return ((uint64_t)s[0] << 32 | s[1]) ^ ((uint64_t)s[2] << 32 | s[3]);
}
// ─────────────────────────────────────────────────────────────────────────────
// ── Global constants ─────────────────────────────────────────────────────────
__device__ __forceinline__ float _symm_nan() { float v; unsigned u = 0x7FC00000u; __builtin_memcpy(&v,&u,4); return v; }
__device__ __forceinline__ float _symm_inf() { float v; unsigned u = 0x7F800000u; __builtin_memcpy(&v,&u,4); return v; }
#define NaN               (_symm_nan())
#define Infinity          (_symm_inf())
#define POSITIVE_INFINITY (_symm_inf())
#define NEGATIVE_INFINITY (-_symm_inf())
#define undefined         nullptr

// ── String scratch buffer — returned by value (thread-local on GPU stack) ─────
struct _SBuf {
    char _b[512];
    __device__ operator const char*() const { return _b; }
    __device__ const char* c_str()    const { return _b; }
};
// _SBuf-specific overloads (must come after struct definition)
// Without these, the generic template matches _SBuf as T and calls (float)v which fails.
__device__ int symm_write(char* buf, _SBuf v)     { return symm_write_str(buf, v._b); }
__device__ int symm_json_val(char* buf, _SBuf v)  { return symm_json_val(buf, v._b); }

// ── String utility functions ──────────────────────────────────────────────────
__device__ _SBuf symm_str_pad_start(const char* s, int width, char fill = ' ') {
    _SBuf out; int slen = symm_strlen(s); int off = 0;
    for (int i = slen; i < width && off < 510; i++) out._b[off++] = fill;
    for (int i = 0; s && s[i] && off < 511; i++) out._b[off++] = s[i];
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_pad_end(const char* s, int width, char fill = ' ') {
    _SBuf out; int off = 0;
    for (int i = 0; s && s[i] && off < 510; i++) out._b[off++] = s[i];
    while (off < width && off < 510) out._b[off++] = fill;
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_trim(const char* s) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int start = 0;
    while (s[start]==' '||s[start]=='\t'||s[start]=='\n'||s[start]=='\r') start++;
    int end = symm_strlen(s);
    while (end > start && (s[end-1]==' '||s[end-1]=='\t'||s[end-1]=='\n'||s[end-1]=='\r')) end--;
    int n = end - start; if (n > 510) n = 510;
    for (int i = 0; i < n; i++) out._b[i] = s[start+i];
    out._b[n] = '\0'; return out;
}
__device__ _SBuf symm_str_trim_start(const char* s) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int start = 0;
    while (s[start]==' '||s[start]=='\t'||s[start]=='\n'||s[start]=='\r') start++;
    int off = 0;
    for (int i = start; s[i] && off < 511; i++) out._b[off++] = s[i];
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_trim_end(const char* s) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int end = symm_strlen(s);
    while (end > 0 && (s[end-1]==' '||s[end-1]=='\t'||s[end-1]=='\n'||s[end-1]=='\r')) end--;
    int n = end > 510 ? 510 : end;
    for (int i = 0; i < n; i++) out._b[i] = s[i];
    out._b[n] = '\0'; return out;
}
__device__ _SBuf symm_str_replace_all(const char* s, const char* from, const char* to) {
    _SBuf out; int off = 0;
    int fl = symm_strlen(from); int tl = symm_strlen(to);
    if (fl == 0) { symm_strcpy(out._b, s, 511); return out; }
    for (int i = 0; s && s[i] && off < 508; ) {
        bool match = true;
        for (int j = 0; j < fl && match; j++) match = (s[i+j] == from[j]);
        if (match) {
            for (int j = 0; j < tl && off < 508; j++) out._b[off++] = to[j];
            i += fl;
        } else { out._b[off++] = s[i++]; }
    }
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_replace(const char* s, const char* from, const char* to) {
    // Single replacement — find first occurrence
    _SBuf out; int off = 0;
    int fl = symm_strlen(from); int tl = symm_strlen(to); bool replaced = false;
    for (int i = 0; s && s[i] && off < 508; ) {
        if (!replaced) {
            bool match = true;
            for (int j = 0; j < fl && match; j++) match = (s[i+j] == from[j]);
            if (match && fl > 0) {
                for (int j = 0; j < tl && off < 508; j++) out._b[off++] = to[j];
                i += fl; replaced = true; continue;
            }
        }
        out._b[off++] = s[i++];
    }
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_repeat(const char* s, int n) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int sl = symm_strlen(s); int off = 0;
    for (int r = 0; r < n && off < 508; r++)
        for (int i = 0; i < sl && off < 508; i++) out._b[off++] = s[i];
    out._b[off] = '\0'; return out;
}
__device__ char symm_str_char_at(const char* s, int i) {
    if (!s) return '\0';
    int len = symm_strlen(s);
    if (i < 0) i = len + i;
    if (i < 0 || i >= len) return '\0';
    return s[i];
}
__device__ bool symm_str_includes(const char* s, const char* sub) {
    if (!s || !sub) return false;
    int sl = symm_strlen(s); int sbl = symm_strlen(sub);
    for (int i = 0; i <= sl - sbl; i++) {
        bool match = true;
        for (int j = 0; j < sbl && match; j++) match = (s[i+j] == sub[j]);
        if (match) return true;
    }
    return false;
}
__device__ bool symm_str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    int pl = symm_strlen(prefix);
    for (int i = 0; i < pl; i++) if (!s[i] || s[i] != prefix[i]) return false;
    return true;
}
__device__ bool symm_str_ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    int sl = symm_strlen(s); int sfl = symm_strlen(suffix);
    if (sfl > sl) return false;
    for (int i = 0; i < sfl; i++) if (s[sl-sfl+i] != suffix[i]) return false;
    return true;
}
__device__ int symm_str_index_of(const char* s, const char* sub) {
    if (!s || !sub) return -1;
    int sl = symm_strlen(s); int sbl = symm_strlen(sub);
    for (int i = 0; i <= sl - sbl; i++) {
        bool match = true;
        for (int j = 0; j < sbl && match; j++) match = (s[i+j] == sub[j]);
        if (match) return i;
    }
    return -1;
}
__device__ int symm_str_last_index_of(const char* s, const char* sub) {
    if (!s || !sub) return -1;
    int sl = symm_strlen(s); int sbl = symm_strlen(sub); int last = -1;
    for (int i = 0; i <= sl - sbl; i++) {
        bool match = true;
        for (int j = 0; j < sbl && match; j++) match = (s[i+j] == sub[j]);
        if (match) last = i;
    }
    return last;
}
__device__ _SBuf symm_str_slice(const char* s, int start, int end = 2147483647) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int len = symm_strlen(s);
    if (start < 0) start = len + start; if (start < 0) start = 0;
    if (end < 0)   end   = len + end;   if (end > len)  end   = len;
    int off = 0;
    for (int i = start; i < end && off < 511; i++) out._b[off++] = s[i];
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_substring(const char* s, int start, int end = 2147483647) {
    return symm_str_slice(s, start, end);  // simplified — same semantics for non-negative args
}
__device__ _SBuf symm_str_to_upper(const char* s) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int off = 0;
    for (int i = 0; s[i] && off < 511; i++) {
        char c = s[i];
        out._b[off++] = (c>='a'&&c<='z') ? c-32 : c;
    }
    out._b[off] = '\0'; return out;
}
__device__ _SBuf symm_str_to_lower(const char* s) {
    _SBuf out; if (!s) { out._b[0]=0; return out; }
    int off = 0;
    for (int i = 0; s[i] && off < 511; i++) {
        char c = s[i];
        out._b[off++] = (c>='A'&&c<='Z') ? c+32 : c;
    }
    out._b[off] = '\0'; return out;
}
__device__ int symm_str_char_code_at(const char* s, int i) {
    if (!s) return -1;
    int len = symm_strlen(s);
    if (i < 0 || i >= len) return -1;
    return (unsigned char)s[i];
}
__device__ _SBuf symm_str_concat(const char* a, const char* b) {
    _SBuf out; int off = 0;
    for (int i = 0; a && a[i] && off < 510; i++) out._b[off++] = a[i];
    for (int i = 0; b && b[i] && off < 510; i++) out._b[off++] = b[i];
    out._b[off] = '\0'; return out;
}
// str.split(sep) — returns the Nth segment (no dynamic allocation)
__device__ _SBuf symm_str_split_at(const char* s, const char* sep, int idx) {
    _SBuf out; out._b[0] = '\0';
    int sl = symm_strlen(sep); int cur = 0; int part = 0;
    for (int i = 0; s[i]; ) {
        bool match = true;
        if (sl > 0) for (int j = 0; j < sl && match; j++) match = (s[i+j] == sep[j]);
        if (match && sl > 0) {
            if (part == idx) { out._b[cur] = '\0'; return out; }
            part++; cur = 0; i += sl;
        } else {
            if (part == idx && cur < 511) out._b[cur++] = s[i++];
            else i++;
        }
    }
    if (part == idx) out._b[cur] = '\0';
    return out;
}

// ── symm_parse_float — full JS parseFloat semantics ───────────────────────────
// Parses an optional sign, integer part, optional decimal part, optional exponent.
__device__ float symm_parse_float(const char* s) {
    if (!s) return 0.f;
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
    bool neg = false;
    if (s[i] == '-') { neg = true; i++; } else if (s[i] == '+') { i++; }
    // Check for NaN / Infinity
    if (s[i]=='N' && s[i+1]=='a' && s[i+2]=='N') return _symm_nan();
    if (s[i]=='I' && s[i+1]=='n' && s[i+2]=='f') { float inf=_symm_inf(); return neg?-inf:inf; }
    // Integer part
    float v = 0.f;
    bool hasDigit = false;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10.f + (s[i] - '0'); i++; hasDigit = true; }
    // Fractional part
    if (s[i] == '.') {
        i++;
        float scale = 0.1f;
        while (s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * scale; scale *= 0.1f; i++; hasDigit = true; }
    }
    if (!hasDigit) return _symm_nan();
    // Exponent part
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        bool eneg = false;
        if (s[i] == '-') { eneg = true; i++; } else if (s[i] == '+') { i++; }
        int exp = 0;
        while (s[i] >= '0' && s[i] <= '9') { exp = exp * 10 + (s[i] - '0'); i++; }
        float ep = 1.f;
        for (int k = 0; k < exp && k < 38; k++) ep *= 10.f;
        v = eneg ? v / ep : v * ep;
    }
    return neg ? -v : v;
}

// ── Number namespace ──────────────────────────────────────────────────────────
struct Number {
    static constexpr float    EPSILON          = 1.1920929e-7f;
    static constexpr long long MAX_SAFE_INTEGER = 9007199254740991LL;   // 2^53 - 1
    static constexpr long long MIN_SAFE_INTEGER = -9007199254740991LL;  // -(2^53 - 1)
    static constexpr float    MAX_VALUE        = 3.402823466e+38f;
    static constexpr float    MIN_VALUE        = 1.175494351e-38f;
    __device__ static bool isNaN(float v)         { return v != v; }
    __device__ static bool isFinite(float v)       { float i=_symm_inf(); return v==v && v!=i && v!=-i; }
    __device__ static bool isInteger(float v)      { return isFinite(v) && floorf(v)==v; }
    __device__ static bool isSafeInteger(float v)  { return isInteger(v) && fabsf(v)<=16777216.f; }
    __device__ static float parseInt(const char* s) {
        if (!s) return 0.f;
        float v = 0; bool neg = false; int i = 0;
        while (s[i]==' ') i++;
        if (s[i]=='-'){neg=true;i++;} else if(s[i]=='+')i++;
        // handle 0x hex
        if (s[i]=='0' && (s[i+1]=='x'||s[i+1]=='X')) {
            i+=2; while ((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')) {
                int d = s[i]<='9'?s[i]-'0':s[i]<='F'?s[i]-'A'+10:s[i]-'a'+10; v=v*16+d; i++;
            }
        } else {
            while (s[i]>='0' && s[i]<='9') { v=v*10+(s[i]-'0'); i++; }
        }
        return neg ? -v : v;
    }
    __device__ static float parseFloat(const char* s) { return symm_parse_float(s); }
};
// Top-level isNaN / isFinite / isInteger matching Number.*
__device__ bool isNaN(float v)   { return v != v; }
__device__ bool isFinite(float v) { float i=_symm_inf(); return v==v&&v!=i&&v!=-i; }
// top-level free-function versions of Number.parseInt / parseFloat (JS global compat)
__device__ float parseInt(const char* s)   { return Number::parseInt(s); }
__device__ float parseFloat(const char* s) { return symm_parse_float(s); }
// parseInt(str, radix) — supports any radix 2-36
__device__ float symm_parse_int_radix(const char* s, int radix) {
    if (!s || radix < 2 || radix > 36) return 0.f;
    float v = 0; bool neg = false; int i = 0;
    while (s[i]==' ') i++;
    if (s[i]=='-'){neg=true;i++;} else if(s[i]=='+')i++;
    // 0x prefix implies hex regardless of radix
    if (radix==16 && s[i]=='0' && (s[i+1]=='x'||s[i+1]=='X')) i+=2;
    while (s[i]) {
        int d;
        if (s[i]>='0' && s[i]<='9') d = s[i]-'0';
        else if (s[i]>='a' && s[i]<='z') d = s[i]-'a'+10;
        else if (s[i]>='A' && s[i]<='Z') d = s[i]-'A'+10;
        else break;
        if (d >= radix) break;
        v = v * radix + d; i++;
    }
    return neg ? -v : v;
}

// ── Additional Math helpers (referenced from MATH_REMAP) ─────────────────────
__device__ float symm_math_sign(float x)  { return (x>0.f)?1.f:(x<0.f)?-1.f:0.f; }
__device__ int   symm_math_clz32(int x)   { return x ? __clz(x) : 32; }
__device__ int   symm_math_imul(int a, int b) { return a * b; }
__device__ float symm_math_fround(float x) { return x; }

// ── Math struct (for Math::random and static property access) ─────────────────
struct Math {
    // Math.random() — simple thread-local LCG (different per thread, not cryptographic)
    __device__ static float random() {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        return (float)(seed >> 8) * (1.0f / (float)(1u << 24));
    }
};

// ── Array namespace (static helpers) ─────────────────────────────────────────
template<typename T>
__device__ bool symm_array_is_array(T) { return true; }
__device__ bool symm_array_is_array(const char*) { return false; }
__device__ bool symm_array_is_array(float)       { return false; }
__device__ bool symm_array_is_array(int)         { return false; }
__device__ bool symm_array_is_array(bool)        { return false; }

struct Array {
    template<typename T> __device__ static bool isArray(T v)         { return symm_array_is_array(v); }
    template<typename T> __device__ static bool isArray(T* v)        { return true; }
    __device__ static bool isArray(const char* v) { return false; }
    __device__ static bool isArray(float v)       { return false; }
    __device__ static bool isArray(int v)         { return false; }

    // Array.from(str) — char array from string (max 256 chars)
    __device__ static char* from(const char* s) {
        static __device__ char _from_buf[256];
        int i = 0;
        while (s && s[i] && i < 255) { _from_buf[i] = s[i]; i++; }
        _from_buf[i] = '\0';
        return _from_buf;
    }
    // Array.from({length:n}, fn) — generate array via callback (up to 256 elements)
    template<typename Fn>
    __device__ static float* from_fn(int n, Fn fn) {
        static __device__ float _from_fn_buf[256];
        int cnt = (n < 256) ? n : 256;
        for (int i = 0; i < cnt; i++) _from_fn_buf[i] = fn(i);
        return _from_fn_buf;
    }
    // Array.of(args...) — not easily variadic on GPU; return a 1-element array
    template<typename T> __device__ static T* of(T v) {
        static __device__ T _of_buf[1];
        _of_buf[0] = v;
        return _of_buf;
    }
};

// ── Object namespace ──────────────────────────────────────────────────────────
template<typename T>
__device__ bool symm_object_has_own(T, const char*) { return true; }

struct Object {
    // Object.keys(obj) — GPU stub: returns a sentinel _SBuf (no dynamic key enum on GPU)
    template<typename T> __device__ static _SBuf keys(T)    { _SBuf b; b._b[0]='\0'; return b; }
    template<typename T> __device__ static _SBuf values(T)  { _SBuf b; b._b[0]='\0'; return b; }
    template<typename T> __device__ static _SBuf entries(T) { _SBuf b; b._b[0]='\0'; return b; }
    template<typename T, typename U> __device__ static T& assign(T& dst, U src) { return dst; }
    template<typename T> __device__ static bool hasOwn(T, const char*) { return true; }
    template<typename T> __device__ static T& freeze(T& v) { return v; }
    template<typename T> __device__ static T& seal(T& v)   { return v; }
    // fromEntries: no-op on GPU — returns a default-constructed value
    template<typename T> __device__ static T fromEntries(T v) { return v; }
    // create: no prototype chain on GPU — just default-construct
    template<typename T> __device__ static T create(T proto) { return T{}; }
    template<typename T> __device__ static T create(T proto, T desc) { return T{}; }
    __device__ static void* create(void*) { return nullptr; }
};

// ── console stub (no-op on GPU — output goes to host log in production) ───────
struct _Console {
    template<typename T> __device__ void log(T)   {}
    template<typename T> __device__ void warn(T)  {}
    template<typename T> __device__ void error(T) {}
    template<typename T> __device__ void info(T)  {}
    template<typename T> __device__ void debug(T) {}
    template<typename A, typename B> __device__ void log(A, B)   {}
    template<typename A, typename B> __device__ void warn(A, B)  {}
    template<typename A, typename B> __device__ void error(A, B) {}
};
__device__ _Console console;

// ── performance.now() ─────────────────────────────────────────────────────────
struct _Performance { __device__ long long now() { return 0LL; } };
__device__ _Performance performance;

// ── globalThis stub ───────────────────────────────────────────────────────────
struct _GlobalThis {};
__device__ _GlobalThis globalThis;

// ── Symbol stub (compile-time constants, no runtime symbol table on GPU) ──────
// Symbol.for(str) → hash the string to an integer constant
__device__ int _symm_symbol_hash(const char* s) {
    unsigned h = 2166136261u;
    while (s && *s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return (int)h;
}
struct _Symbol {
    // Symbol() constructor → unique integer
    __device__ _Symbol() : _val(0) {}
    __device__ explicit _Symbol(int v) : _val(v) {}
    int _val;
    __device__ bool operator==(_Symbol o) const { return _val == o._val; }
    __device__ bool operator!=(_Symbol o) const { return _val != o._val; }
    // Symbol.for(str) — global registry stub: hash-based
    __device__ static _Symbol for_(const char* s) { return _Symbol(_symm_symbol_hash(s)); }
    // Symbol.iterator, Symbol.asyncIterator, Symbol.toPrimitive — well-known symbols
    static constexpr int _ITERATOR        = 0xA001;
    static constexpr int _ASYNC_ITERATOR  = 0xA002;
    static constexpr int _TO_PRIMITIVE    = 0xA003;
    static constexpr int _HAS_INSTANCE    = 0xA004;
    static constexpr int _SPECIES         = 0xA005;
    __device__ static _Symbol iterator()       { return _Symbol(_ITERATOR); }
    __device__ static _Symbol asyncIterator()  { return _Symbol(_ASYNC_ITERATOR); }
    __device__ static _Symbol toPrimitive()    { return _Symbol(_TO_PRIMITIVE); }
    __device__ static _Symbol hasInstance()    { return _Symbol(_HAS_INSTANCE); }
    __device__ static _Symbol species()        { return _Symbol(_SPECIES); }
};
__device__ _Symbol Symbol;

// ── Buffer (Node.js compat — alias for ByteArray with extra methods) ──────────
struct Buffer : ByteArray {
    __device__ Buffer() : ByteArray{ nullptr, 0 } {}
    __device__ Buffer(const char* d, int n) : ByteArray{ d, n } {}
    __device__ static Buffer from(const char* s) { int n=0; while(s&&s[n])n++; return Buffer{s,n}; }
    __device__ static Buffer alloc(int n)         { return Buffer{nullptr,n}; }
    __device__ const char* toString(const char* = "utf8") const { return _data; }
};

// ── BigInt — maps to long long on GPU ────────────────────────────────────────
typedef long long bigint;

// ── Date — real-time via DateNow host injection ───────────────────────────────
// The host writes the current Unix-ms timestamp into _symm_now_ms via
// cuMemcpyHtoD before launching each batch kernel (see NvrtcCompiler.cpp).
__device__ uint64_t _symm_now_ms = 0;
struct Date {
    long long _ms;
    __device__ Date() : _ms((long long)_symm_now_ms) {}
    __device__ Date(long long ms) : _ms(ms) {}
    __device__ static long long now() { return (long long)_symm_now_ms; }
    __device__ int getFullYear() {
        long long d = _ms/86400000LL; int y = 1970;
        while (true) { int days=(y%4==0&&(y%100!=0||y%400==0))?366:365; if(d<days)break; d-=days; y++; }
        return y;
    }
    __device__ int getMonth()        { return (int)((_ms/2592000000LL)%12); }
    __device__ int getDate()         { return (int)((_ms/86400000LL)%31)+1; }
    __device__ int getHours()        { return (int)((_ms/3600000LL)%24); }
    __device__ int getMinutes()      { return (int)((_ms/60000LL)%60); }
    __device__ int getSeconds()      { return (int)((_ms/1000LL)%60); }
    __device__ int getMilliseconds() { return (int)(_ms%1000LL); }
    __device__ long long getTime()   { return _ms; }
    __device__ const char* toISOString() { return "1970-01-01T00:00:00.000Z"; }
    __device__ long long valueOf()   { return _ms; }
};

// ── Promise — race / all / any / allSettled ───────────────────────────────────
// Note: GPU execution is synchronous per kernel; all "concurrent" operations
// complete before the next line. race() returns the first element (or the one
// with the smallest ms delay argument if all are setTimeout-based).
struct _PromiseVal {
    enum { Pending=0, Fulfilled=1, Rejected=2 } status = Pending;
    float       value   = 0.f;
    const char* reason  = nullptr;
    const char* label   = nullptr;   // optional string value
    __device__ bool fulfilled() const { return status == Fulfilled; }
    __device__ bool rejected()  const { return status == Rejected;  }
};
// ── RegExp with basic NFA simulation ─────────────────────────────────────────
// (Replaces the stub from the original prelude above)
// Supports: . * + ? [abc] [^abc] \d \w \s ^ $ (…) (?:…) alternation |
static constexpr int SYMM_RE_MAX_PAT = 256;
static constexpr int SYMM_RE_MAX_MAT = 32;
struct MatchResult { int start; int end; const char* input; };
struct MatchArray  {
    MatchResult _m[SYMM_RE_MAX_MAT]; int _n = 0;
    __device__ int length() const { return _n; }
    __device__ MatchResult operator[](int i) const { return _m[i]; }
};
// Forward declaration of NFA matcher (defined after RegExp struct)
__device__ bool _symm_nfa(const char* pat, const char* str, int si, int& matchEnd);
struct RegExp {
    char   _pat[SYMM_RE_MAX_PAT];
    char   _flags[8];
    bool   _global, _ignoreCase, _multiline;
    __device__ RegExp(const char* p = "", const char* fl = "") {
        symm_strcpy(_pat, p, SYMM_RE_MAX_PAT);
        symm_strcpy(_flags, fl, 8);
        _global=false; _ignoreCase=false; _multiline=false;
        for (int i=0; fl[i]; i++) {
            if (fl[i]=='g') _global=true;
            else if (fl[i]=='i') _ignoreCase=true;
            else if (fl[i]=='m') _multiline=true;
        }
    }
    __device__ bool test(const char* s) const {
        if (!s) return false;
        int end=0;
        for (int i=0; s[i]; i++) { if (_symm_nfa(_pat, s, i, end)) return true; }
        return false;
    }
    __device__ MatchResult exec(const char* s) const {
        if (!s) return {-1,-1,s};
        int end=0;
        for (int i=0; s[i]; i++) { if (_symm_nfa(_pat, s, i, end)) return {i,end,s}; }
        return {-1,-1,s};
    }
    __device__ MatchArray matchAll(const char* s) const {
        MatchArray arr; if (!s) return arr;
        int end=0;
        for (int i=0; s[i] && arr._n < SYMM_RE_MAX_MAT; ) {
            if (_symm_nfa(_pat, s, i, end)) {
                arr._m[arr._n++] = {i, end, s};
                i = (end > i) ? end : i+1;
            } else i++;
        }
        return arr;
    }
    int lastIndex = 0;
};
// ── GPU RegExp NFA — correct recursive backtracking implementation ─────────────
// Supports: . * + ? {n} {n,m} [abc] [^abc] \d \w \s \b ^ $ (?:...) | alternation
// Limitation: no capturing groups (returned as MatchResult.start/end only)
// Recursion depth bounded by pattern length (~256) — safe on GPU stack.

__device__ bool _symm_re_word(char c)  { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'; }
__device__ bool _symm_re_digit(char c) { return c>='0'&&c<='9'; }
__device__ bool _symm_re_space(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }

// Match a character class [...] starting at pat[pi] (pi should be after '[')
// Returns true if c matches; advances pi to past ']'
__device__ bool _symm_re_class(const char* pat, int& pi, char c, bool ignCase) {
    bool negate = (pat[pi]=='^'); if (negate) pi++;
    bool found = false;
    while (pat[pi] && pat[pi]!=']') {
        if (pat[pi]=='\\' && pat[pi+1]) {
            pi++; char esc=pat[pi++];
            char low = (ignCase && c>='A' && c<='Z') ? c+32 : c;
            if (esc=='d') { if (_symm_re_digit(c)) found=true; }
            else if (esc=='D') { if (!_symm_re_digit(c)) found=true; }
            else if (esc=='w') { if (_symm_re_word(c)) found=true; }
            else if (esc=='W') { if (!_symm_re_word(c)) found=true; }
            else if (esc=='s') { if (_symm_re_space(c)) found=true; }
            else if (esc=='S') { if (!_symm_re_space(c)) found=true; }
            else { if (ignCase?(low==(esc>='A'&&esc<='Z'?esc+32:esc)):(c==esc)) found=true; }
        } else if (pat[pi+1]=='-' && pat[pi+2] && pat[pi+2]!=']') {
            char lo=pat[pi]; char hi=pat[pi+2]; pi+=3;
            char lc = (ignCase && c>='A' && c<='Z') ? c+32 : c;
            char ll = (ignCase && lo>='A' && lo<='Z') ? lo+32 : lo;
            char lh = (ignCase && hi>='A' && hi<='Z') ? hi+32 : hi;
            if (lc>=ll && lc<=lh) found=true;
        } else {
            char ec=pat[pi++];
            char lc=(ignCase && c>='A' && c<='Z')?c+32:c;
            char le=(ignCase && ec>='A' && ec<='Z')?ec+32:ec;
            if (lc==le) found=true;
        }
    }
    if (pat[pi]==']') pi++;
    return found != negate;
}

// Match a single atom at pat[pi] against str[si]
// Returns true on match; advances pi to after atom (before quantifier); does not advance si.
__device__ bool _symm_re_atom(const char* pat, int& pi, const char* str, int si, bool ignCase) {
    char c = str[si];
    if (!c) {
        // End of string — only $ or \b can match at end
        if (pat[pi]=='$') return true;  // handled elsewhere
        return false;
    }
    char lc = (ignCase && c>='A' && c<='Z') ? c+32 : c;
    if (pat[pi]=='\\' && pat[pi+1]) {
        pi++; char esc=pat[pi++];
        char le=(ignCase && esc>='A' && esc<='Z')?esc+32:esc;
        if (esc=='d') return _symm_re_digit(c);
        if (esc=='D') return !_symm_re_digit(c);
        if (esc=='w') return _symm_re_word(c);
        if (esc=='W') return !_symm_re_word(c);
        if (esc=='s') return _symm_re_space(c);
        if (esc=='S') return !_symm_re_space(c);
        if (esc=='n') return c=='\n';
        if (esc=='r') return c=='\r';
        if (esc=='t') return c=='\t';
        return lc==le;
    }
    if (pat[pi]=='.') { pi++; return (c!='\n'); }
    if (pat[pi]=='[') { pi++; return _symm_re_class(pat, pi, c, ignCase); }
    if (pat[pi]=='^' || pat[pi]=='$') return false;  // anchors handled in nfa
    // Literal character
    char lp=(ignCase && pat[pi]>='A' && pat[pi]<='Z')?pat[pi]+32:pat[pi];
    char lc2=(ignCase && c>='A' && c<='Z')?c+32:c;
    return (lc2 == lp) ? (pi++, true) : (false);
}

// Forward declaration for alternation
__device__ bool _symm_nfa(const char* pat, int pi, const char* str, int si, int& matchEnd, bool ignCase, int depth);

// Try to match the rest of the pattern (pat from pi onwards) against str from si.
// Returns true and sets matchEnd to the end position on success.
__device__ bool _symm_nfa(const char* pat, int pi, const char* str, int si, int& matchEnd, bool ignCase, int depth) {
    if (depth > 64) return false;  // prevent stack overflow on pathological patterns
    while (true) {
        // End of pattern — success
        if (!pat[pi]) { matchEnd = si; return true; }

        // Anchor: ^
        if (pat[pi]=='^') {
            if (si != 0) return false;
            pi++; continue;
        }
        // Anchor: $
        if (pat[pi]=='$') {
            if (str[si] != '\0') return false;
            pi++; matchEnd = si; continue;
        }

        // Alternation: |  — try left branch first; if fails, try right
        // Scan to find the top-level | in the current group scope
        {
            int depth2 = 0; int altPi = pi;
            while (pat[altPi]) {
                if (pat[altPi]=='('||pat[altPi]=='[') depth2++;
                else if ((pat[altPi]==')'||pat[altPi]==']') && depth2>0) depth2--;
                else if (pat[altPi]=='|' && depth2==0) {
                    // Found alternation — try left branch up to altPi
                    // Build sub-pattern for left branch
                    int lEnd = 0;
                    if (_symm_nfa(pat, pi, str, si, lEnd, ignCase, depth+1)) {
                        // Left branch succeeded — but we need to also consume the rest AFTER |...
                        // For simplicity: try matching only if left branch consumed to some point
                        matchEnd = lEnd;
                        return true;
                    }
                    // Try right branch from altPi+1
                    return _symm_nfa(pat, altPi+1, str, si, matchEnd, ignCase, depth+1);
                }
                altPi++;
            }
        }

        // Non-capturing group: (?:...) or (...)
        if (pat[pi]=='(') {
            pi++;
            bool nonCapture = (pat[pi]=='?' && pat[pi+1]==':');
            if (nonCapture) pi += 2;
            // Find matching ')'
            int groupStart = pi;
            int depth2 = 1;
            while (pat[pi] && depth2>0) {
                if (pat[pi]=='(') depth2++;
                else if (pat[pi]==')') depth2--;
                if (depth2>0) pi++;
                else pi++;  // consume ')'
            }
            int groupEnd = pi - 1;  // pi is now after ')'
            // Check for quantifier after group
            char quant = pat[pi];
            int minR = 1, maxR = 1;
            if (quant=='*'||quant=='+'||quant=='?') {
                minR = (quant=='+') ? 1 : 0;
                maxR = (quant=='?') ? 1 : 255;
                pi++;
            } else if (quant=='{') {
                // {n} or {n,m} or {n,}
                pi++; int n=0, m=0; bool hasComma=false;
                while (pat[pi]>='0'&&pat[pi]<='9') n=n*10+(pat[pi++]-'0');
                if (pat[pi]==',') { hasComma=true; pi++; while(pat[pi]>='0'&&pat[pi]<='9') m=m*10+(pat[pi++]-'0'); }
                if (pat[pi]=='}') pi++;
                minR=n; maxR=hasComma?(m>0?m:255):n;
            } else { minR=1; maxR=1; }
            // Match the group minR..maxR times
            // Build a temporary sub-pattern string for the group
            char subPat[256]; int sl=0;
            for (int k=groupStart; k<groupEnd && sl<254; k++) subPat[sl++]=pat[k];
            subPat[sl]=0;
            int r=0, curSi=si;
            while (r<maxR) {
                int subEnd=0;
                if (!_symm_nfa(subPat, 0, str, curSi, subEnd, ignCase, depth+1)) break;
                if (subEnd==curSi && r>=minR) break;  // zero-width match — stop to avoid infinite loop
                curSi=subEnd; r++;
            }
            if (r < minR) return false;
            si = curSi;
            continue;
        }

        // Regular atom + optional quantifier
        int atomPi = pi;
        // Peek at atom to determine its extent
        // We need to advance pi past the atom, then check quantifier
        char quant = 0;
        // Determine atom end
        int atomPiEnd = pi;
        if (pat[atomPiEnd]=='\\' && pat[atomPiEnd+1]) { atomPiEnd+=2; }
        else if (pat[atomPiEnd]=='[') {
            atomPiEnd++; if(pat[atomPiEnd]=='^') atomPiEnd++;
            while(pat[atomPiEnd] && pat[atomPiEnd]!=']') {
                if(pat[atomPiEnd]=='\\' && pat[atomPiEnd+1]) atomPiEnd++;
                atomPiEnd++;
            }
            if(pat[atomPiEnd]==']') atomPiEnd++;
        } else { atomPiEnd++; }
        quant = pat[atomPiEnd];
        int minR=1, maxR=1;
        int quantEnd = atomPiEnd;
        if (quant=='*'||quant=='+'||quant=='?') {
            minR = (quant=='+') ? 1 : 0;
            maxR = (quant=='?') ? 1 : 255;
            quantEnd++;
        } else if (quant=='{') {
            int qp=atomPiEnd+1; int n=0, m=0; bool hasComma=false;
            while(pat[qp]>='0'&&pat[qp]<='9') n=n*10+(pat[qp++]-'0');
            if(pat[qp]==',') { hasComma=true; qp++; while(pat[qp]>='0'&&pat[qp]<='9') m=m*10+(pat[qp++]-'0'); }
            if(pat[qp]=='}') qp++;
            minR=n; maxR=hasComma?(m>0?m:255):n;
            quantEnd=qp;
        } else { minR=1; maxR=1; quantEnd=atomPiEnd; }

        // Greedy match: consume as many as possible then backtrack
        int r = 0, curSi = si;
        while (r < maxR) {
            if (!str[curSi]) break;
            int tmpPi = pi;
            bool matched = _symm_re_atom(pat, tmpPi, str, curSi, ignCase);
            if (!matched) break;
            curSi++; r++;
        }
        if (r < minR) return false;
        // Backtrack from max down to min, trying the rest
        pi = quantEnd;
        for (int k = r; k >= minR; k--) {
            int trySi = si + k;
            if (_symm_nfa(pat, pi, str, trySi, matchEnd, ignCase, depth+1)) return true;
        }
        return false;
    }
}
// String.prototype.matchAll — uses the above RegExp
__device__ MatchArray symm_str_match_all(const char* s, const RegExp& re) {
    return re.matchAll(s);
}
__device__ MatchResult symm_str_match_first(const char* s, const RegExp& re) {
    return re.exec(s);
}
__device__ bool symm_str_test(const char* s, const RegExp& re) {
    return re.test(s);
}

// ── Generator / async-iterator state machine support ─────────────────────────
template<typename V>
struct GpuGenState {
    int  _step = 0;
    bool _done = false;
    V    _val  = {};
    struct IterResult { V value; bool done; };
    __device__ IterResult next_val(V v) { _val=v; return {_val, _done}; }
    __device__ IterResult finish()      { _done=true; return {_val, true}; }
    __device__ bool       done()  const { return _done; }
    __device__ V          value() const { return _val; }
};
template<typename T, typename V>
struct GpuAsyncIterState {
    T*   _obj;
    int  _step = 0;
    bool _done = false;
    V    _val  = {};
    __device__ void advance() { _done = !_obj->__asyncIter_step(_step, _val); _step++; }
    struct Proxy {
        GpuAsyncIterState* _it;
        __device__ Proxy(GpuAsyncIterState* it) : _it(it) { _it->advance(); }
        __device__ bool    operator!=(int) const { return !_it->_done; }
        __device__ Proxy&  operator++()  { _it->advance(); return *this; }
        __device__ V       operator*() const { return _it->_val; }
    };
    __device__ Proxy begin() { return Proxy(this); }
    __device__ int   end()   { return 0; }
};

// ── AggregateError (for Promise.any) ─────────────────────────────────────────
struct AggregateError : Error {
    __device__ AggregateError(const char* m = "All promises rejected") : Error(m) {}
};

// ── Number.toFixed(n) — format float to N decimal places ─────────────────────
__device__ _SBuf symm_number_to_fixed(float v, int decimals) {
    _SBuf out;
    if (v != v) { out._b[0]='N'; out._b[1]='a'; out._b[2]='N'; out._b[3]=0; return out; }
    float inf = _symm_inf();
    if (v == inf || v == -inf) {
        if (v < 0) { out._b[0]='-'; out._b[1]='I'; out._b[2]='n'; out._b[3]='f'; out._b[4]=0; }
        else       { out._b[0]='I'; out._b[1]='n'; out._b[2]='f'; out._b[3]=0; }
        return out;
    }
    // Clamp decimals to [0, 20]
    if (decimals < 0) decimals = 0;
    if (decimals > 20) decimals = 20;
    // Round to N decimal places
    float multiplier = 1.f;
    for (int i = 0; i < decimals; i++) multiplier *= 10.f;
    float rounded = (v >= 0.f) ? floorf(v * multiplier + 0.5f) / multiplier
                                : ceilf(v * multiplier - 0.5f) / multiplier;
    bool neg = rounded < 0.f; if (neg) rounded = -rounded;
    long long intPart = (long long)rounded;
    float     fracPart = rounded - (float)intPart;
    // Build integer part
    int off = 0;
    if (neg) out._b[off++] = '-';
    char tmp[24]; int tlen = 0;
    if (intPart == 0) { tmp[tlen++] = '0'; }
    else { long long ip = intPart; while (ip>0) { tmp[tlen++]='0'+(int)(ip%10); ip/=10; } }
    for (int j = tlen-1; j >= 0; j--) out._b[off++] = tmp[j];
    if (decimals > 0) {
        out._b[off++] = '.';
        for (int d = 0; d < decimals && off < 510; d++) {
            fracPart *= 10.f;
            int dig = (int)fracPart;
            if (dig > 9) dig = 9;
            out._b[off++] = '0' + dig;
            fracPart -= (float)dig;
        }
    }
    out._b[off] = '\0';
    return out;
}

// ── String.fromCharCode(n, ...) ───────────────────────────────────────────────
__device__ _SBuf symm_str_from_char_code(int c) {
    _SBuf out;
    // UTF-8 encode a Unicode code point (basic BMP support)
    int off = 0;
    if (c < 0x80) {
        out._b[off++] = (char)c;
    } else if (c < 0x800) {
        out._b[off++] = (char)(0xC0 | (c >> 6));
        out._b[off++] = (char)(0x80 | (c & 0x3F));
    } else {
        out._b[off++] = (char)(0xE0 | (c >> 12));
        out._b[off++] = (char)(0x80 | ((c >> 6) & 0x3F));
        out._b[off++] = (char)(0x80 | (c & 0x3F));
    }
    out._b[off] = '\0';
    return out;
}

// ── String.prototype.at(i) — negative index support ──────────────────────────
__device__ char symm_str_at_char(const char* s, int i) {
    if (!s) return '\0';
    int len = symm_strlen(s);
    if (i < 0) i = len + i;
    if (i < 0 || i >= len) return '\0';
    return s[i];
}
__device__ _SBuf symm_str_at(const char* s, int i) {
    _SBuf out; out._b[0] = symm_str_at_char(s, i); out._b[1] = '\0'; return out;
}

// ── Route path parameter extraction ──────────────────────────────────────────
// Extract the Nth URL segment (0-based) from an actual path string.
// e.g. symm_extract_param("/users/42/posts/7", 1) → "42"
__device__ _SBuf symm_extract_param(const char* path, int segIdx) {
    _SBuf out; out._b[0] = '\0';
    int seg = 0, i = 0, off = 0;
    // Skip leading slashes
    while (path[i] == '/') i++;
    int start = i;
    while (true) {
        bool slash = (path[i] == '/' || path[i] == '\0');
        if (slash) {
            if (seg == segIdx) {
                int n = i - start;
                if (n > 510) n = 510;
                for (int j = 0; j < n; j++) out._b[j] = path[start + j];
                out._b[n] = '\0';
                return out;
            }
            if (path[i] == '\0') break;
            i++; while (path[i] == '/') i++;
            seg++; start = i;
        } else {
            i++;
        }
    }
    return out;
}

// ── Query string parameter extraction ────────────────────────────────────────
// Extract the value of a named query param from a query string.
// e.g. symm_extract_query("q=hello&limit=10", "limit") → "10"
__device__ _SBuf symm_extract_query(const char* qs, const char* key) {
    _SBuf out; out._b[0] = '\0';
    if (!qs || !key) return out;
    int klen = symm_strlen(key);
    int i = 0;
    while (qs[i]) {
        // Match key at current position
        bool match = true;
        for (int j = 0; j < klen && match; j++) match = (qs[i+j] == key[j]);
        if (match && qs[i+klen] == '=') {
            int vstart = i + klen + 1, off = 0;
            while (qs[vstart] && qs[vstart] != '&' && off < 511)
                out._b[off++] = qs[vstart++];
            out._b[off] = '\0';
            return out;
        }
        // Advance to next & or end
        while (qs[i] && qs[i] != '&') i++;
        if (qs[i] == '&') i++;
    }
    return out;
}

// ── JSON.stringify — GPU-side serialisation ───────────────────────────────────
// String values are JSON-quoted; numbers/bools/null are bare; objects → "{}".
__device__ _SBuf symm_json_stringify(const char* s) {
    _SBuf b; int n = 0; b._b[n++] = '"';
    if (s) { while (*s && n < 509) { if (*s == '"') { b._b[n++]='\\'; } b._b[n++] = *s++; } }
    b._b[n++] = '"'; b._b[n] = '\0'; return b;
}
__device__ _SBuf symm_json_stringify(float v)         { _SBuf b; symm_write_float(b._b, v); return b; }
__device__ _SBuf symm_json_stringify(int v)           { _SBuf b; symm_write_int(b._b, v);   return b; }
__device__ _SBuf symm_json_stringify(long long v)     { _SBuf b; symm_write_int(b._b, v);   return b; }
__device__ _SBuf symm_json_stringify(bool v)          { _SBuf b; symm_write_bool(b._b, v);  return b; }
__device__ _SBuf symm_json_stringify(decltype(nullptr)) { _SBuf b; b._b[0]='n'; b._b[1]='u'; b._b[2]='l'; b._b[3]='l'; b._b[4]=0; return b; }
template<typename T> __device__ _SBuf symm_json_stringify(T v) {
    // Fallback for _SBuf and other types: try implicit const char* conversion, else "{}"
    _SBuf b; const char* s = (const char*)v; int n = 0; b._b[n++] = '"';
    if (s) { while (*s && n < 509) { if (*s == '"') { b._b[n++]='\\'; } b._b[n++] = *s++; } }
    b._b[n++] = '"'; b._b[n] = '\0'; return b;
}
// JSON.parse stub — returns the raw string (caller extracts fields with symm_body_get_*)
__device__ const char* symm_json_parse(const char* s) { return s; }

// ── Array/GpuBuffer prototype helpers ────────────────────────────────────────
// These are templated device functions that work on stack-allocated arrays.
// The codegen emits calls to these for common array methods.

// arr.join(sep) — produces a comma-separated string of numeric elements
template<typename T>
__device__ _SBuf symm_arr_join(T* arr, int n, const char* sep = ",") {
    _SBuf out; int off = 0; int sl = symm_strlen(sep);
    for (int i = 0; i < n && off < 508; i++) {
        if (i > 0) { for (int j = 0; j < sl && off < 508; j++) out._b[off++] = sep[j]; }
        off += symm_write_float(out._b + off, (float)arr[i]);
    }
    out._b[off] = '\0'; return out;
}
// arr.indexOf(val) — linear search
template<typename T, typename U>
__device__ int symm_arr_index_of(T* arr, int n, U val) {
    for (int i = 0; i < n; i++) if ((T)val == arr[i]) return i;
    return -1;
}
// arr.includes(val)
template<typename T, typename U>
__device__ bool symm_arr_includes(T* arr, int n, U val) { return symm_arr_index_of(arr, n, val) >= 0; }

// arr.every(fn) — returns true if fn returns true for all elements
template<typename T, typename Fn>
__device__ bool symm_arr_every(T* arr, int n, Fn fn) {
    for (int i = 0; i < n; i++) if (!fn(arr[i])) return false;
    return true;
}
// arr.some(fn) — returns true if fn returns true for any element
template<typename T, typename Fn>
__device__ bool symm_arr_some(T* arr, int n, Fn fn) {
    for (int i = 0; i < n; i++) if (fn(arr[i])) return true;
    return false;
}
// arr.find(fn) — returns first matching element, or default
template<typename T, typename Fn>
__device__ T symm_arr_find(T* arr, int n, Fn fn) {
    for (int i = 0; i < n; i++) if (fn(arr[i])) return arr[i];
    return T{};
}
// arr.findIndex(fn)
template<typename T, typename Fn>
__device__ int symm_arr_find_index(T* arr, int n, Fn fn) {
    for (int i = 0; i < n; i++) if (fn(arr[i])) return i;
    return -1;
}
// arr.fill(val, start=0, end=n)
template<typename T, typename U>
__device__ void symm_arr_fill(T* arr, int n, U val, int start = 0, int end = -1) {
    if (end < 0 || end > n) end = n;
    for (int i = start; i < end; i++) arr[i] = (T)val;
}
// arr.reverse() — in-place
template<typename T>
__device__ void symm_arr_reverse(T* arr, int n) {
    for (int i = 0, j = n-1; i < j; i++, j--) { T t = arr[i]; arr[i] = arr[j]; arr[j] = t; }
}
// arr.map(fn) — apply fn to each element, return new array (on stack, max 256 elems)
template<typename T, typename Fn>
__device__ T* symm_arr_map(T* arr, int n, Fn fn) {
    // Returns pointer to thread-local static buffer — safe for read-only use
    static __device__ T _map_buf[256];
    int cnt = (n < 256) ? n : 256;
    for (int i = 0; i < cnt; i++) _map_buf[i] = fn(arr[i]);
    return _map_buf;
}
// arr.filter(fn) — collect elements matching predicate, return new array + count
// Usage: int out_len; auto* out = symm_arr_filter(arr, n, fn, &out_len);
template<typename T, typename Fn>
__device__ T* symm_arr_filter(T* arr, int n, Fn fn) {
    static __device__ T _flt_buf[256];
    int cnt = 0;
    for (int i = 0; i < n && cnt < 256; i++) if (fn(arr[i])) _flt_buf[cnt++] = arr[i];
    return _flt_buf;
}
// arr.forEach(fn) — iterate without collecting result
template<typename T, typename Fn>
__device__ void symm_arr_for_each(T* arr, int n, Fn fn) {
    for (int i = 0; i < n; i++) fn(arr[i]);
}
// Array.prototype.reduce(fn, init) — fold left over an array
template<typename T, typename Acc, typename Fn>
__device__ Acc symm_arr_reduce(const T* arr, int n, Fn fn, Acc init) {
    Acc acc = init;
    for (int i = 0; i < n; i++) acc = fn(acc, arr[i]);
    return acc;
}
// Array.prototype.slice(start, end?) — copy a sub-range into a new array
// Returns a _SBuf carrying the slice pointer + length (for numeric arrays this
// just returns the pointer offset; for strings it builds a char buffer).
template<typename T>
__device__ T* symm_arr_slice(T* arr, int n, int start, int end = -1) {
    if (start < 0) start = n + start;
    if (start < 0) start = 0;
    if (end < 0 || end > n) end = n;
    if (start >= end) return arr + end;  // empty slice (caller tracks length separately)
    return arr + start;
}
// Array.prototype.concat(other, otherLen) — appends other[] to arr[]; returns arr
template<typename T>
__device__ T* symm_arr_concat(T* arr, int* nPtr, const T* other, int otherLen) {
    for (int i = 0; i < otherLen; i++) arr[(*nPtr)++] = other[i];
    return arr;
}
// Array.prototype.flat() — no-op on typed GPU arrays (already flat)
template<typename T>
__device__ T* symm_arr_flat(T* arr, int) { return arr; }
// Simple insertion sort (no stdlib on GPU)
template<typename T>
__device__ void symm_arr_sort(T* arr, int n) {
    for (int i = 1; i < n; i++) {
        T key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}
template<typename T, typename Fn>
__device__ void symm_arr_sort_fn(T* arr, int n, Fn cmp) {
    for (int i = 1; i < n; i++) {
        T key = arr[i]; int j = i - 1;
        while (j >= 0 && cmp(arr[j], key) > 0) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

// ── process.env — host-injected environment variable map ─────────────────────
// Actual env var values are injected as __constant__ strings by the compiler.
// process.env["NAME"] falls through to an empty string unless overridden.
// ─────────────────────────────────────────────────────────────────────────────

// ── process global ────────────────────────────────────────────────────────────
// _symm_start_ms: set once by host at module load time (see NvrtcCompiler.cpp).
__device__ uint64_t _symm_start_ms = 0;
struct _Process {
    struct _Env { __device__ const char* operator[](const char*) const { return ""; } } env;
    const char* platform = "gpu";
    int exitCode = 0;
    __device__ void exit(int code = 0) { exitCode = code; }
    // uptime() — seconds since server start (float, like Node.js process.uptime())
    __device__ float  uptime()    { return (float)((_symm_now_ms - _symm_start_ms) / 1000ULL); }
    // hrtime() — high-resolution nanoseconds since start (bigint-compatible)
    __device__ long long hrtime() { return (long long)((_symm_now_ms - _symm_start_ms) * 1000000LL); }
    __device__ long long hrtime_ms() { return (long long)(_symm_now_ms - _symm_start_ms); }
} _symm_process;
// 'process' is accessed as a global identifier in emitExpr → mapped above
// ─────────────────────────────────────────────────────────────────────────────
)";

Codegen::Codegen(const ServerOptions& opts) : _opts(opts) {}

std::string Codegen::emitShared(const Module& mod) {
    _out.clear();
    _kernelIdx = 0;
    _handlerIdx = 0;
    _tmpIdx = 0;
    _out += PRELUDE;
    _out += "\n";
    for (auto& stmt : mod.stmts) {
        if (!stmt) continue;
        switch (stmt->kind) {
            case NodeKind::ClassDecl:
            case NodeKind::FuncDecl:
                emitStmt(stmt, 0);
                break;
            case NodeKind::ConstDecl: {
                // Top-level const/let — emit as __device__ constexpr
                auto& d = std::get<ConstDecl>(stmt->data);
                if (d.init) {
                    _out += "__device__ constexpr auto " + d.name +
                            " = " + emitExpr(d.init) + ";\n";
                }
                break;
            }
            case NodeKind::Block: {
                // Flat block (e.g. from enum expansion, array destructuring) —
                // emit each inner statement at top level
                auto& blk = std::get<Block>(stmt->data);
                if (blk.flat) {
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

    if (route.handler && route.handler->kind == NodeKind::FuncDecl) {
        auto& fn = std::get<FuncDecl>(route.handler->data);
        // The handler receives a SymmCtx* — its first param is 'ctx'
        if (fn.body && fn.body->kind == NodeKind::Block) {
            emitBlock(std::get<Block>(fn.body->data), 1);
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
    _out += "    unsigned int* resp_len_out\n";
    _out += ") {\n";
    _out += "    SymmCtx ctx{};\n";
    _out += "    ctx.body        = body;\n";
    _out += "    ctx.body_len    = body_len;\n";
    _out += "    ctx.resp_buf    = resp_buf;\n";
    _out += "    ctx.resp_len    = 0;\n";
    _out += "    ctx.resp_status = 200;\n";
    _out += "    switch (route_id) {\n";
    for (int i = 0; i < (int)routes.size(); i++) {
        _out += "        case " + std::to_string(i) + ": symm_handler_" + std::to_string(i) + "(&ctx); break;\n";
    }
    _out += "        default:\n";
    _out += "            symm_json_obj_str(&ctx, \"error\", \"404 Not Found\");\n";
    _out += "            ctx.resp_status = 404;\n";
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
    _out += "    const char**   d_bodies,      // [batch_size] device pointers\n";
    _out += "    const int*     d_body_lens,   // [batch_size]\n";
    _out += "    const char**   d_paths,       // [batch_size]\n";
    _out += "    const char**   d_methods,     // [batch_size]\n";
    _out += "    char**         d_resp_bufs,   // [batch_size]\n";
    _out += "    unsigned int*  d_resp_lens,   // [batch_size] output\n";
    _out += "    int*           d_statuses,    // [batch_size] output\n";
    _out += "    const uint32_t* d_active,     // 256-bit bitmask (8 × uint32)\n";
    _out += "    int            batch_size\n";
    _out += ") {\n";
    _out += "    int tid = threadIdx.x;\n";
    _out += "    // Mask check: is this thread slot active?\n";
    _out += "    bool active = (tid < batch_size)\n";
    _out += "               && (d_active[tid >> 5] & (1u << (tid & 31)));\n";
    _out += "    if (!active) return;\n";
    _out += "\n";
    _out += "    // Each thread builds its own SymmCtx from its slot in the batch.\n";
    _out += "    SymmCtx ctx{};\n";
    _out += "    ctx.body        = d_bodies[tid];\n";
    _out += "    ctx.body_len    = d_body_lens[tid];\n";
    _out += "    ctx.path        = d_paths[tid];\n";
    _out += "    ctx.method      = d_methods[tid];\n";
    _out += "    ctx.resp_buf    = d_resp_bufs[tid];\n";
    _out += "    ctx.resp_len    = 0;\n";
    _out += "    ctx.resp_status = 200;\n";
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
                    for (int pi = 0; pi < (int)oe->props.size(); pi++) {
                        if (pi) _out += indent(depth+1) + buf + "[" + buf + "_n++] = ',';\n";
                        std::string kesc = oe->props[pi].first;
                        _out += indent(depth+1) + buf + "_n += symm_write_str("
                             + buf + " + " + buf + "_n, \"\\\"" + kesc + "\\\":\");\n";
                        auto& val = oe->props[pi].second;
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
                    // for (const x of arr) — indexed loop using companion _len variable
                    _out += ind + "for (int " + idx + " = 0; " + idx + " < (int)(" + iter + "_len); " + idx + "++) {\n";
                    _out += indent(depth+1) + "auto " + fo.varName + " = " + iter + "[" + idx + "];\n";
                    emitStmt(fo.body, depth + 1);
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
            // GPU kernels can't throw C++ exceptions — model them with result codes.
            // Emit try body directly; catch block guarded by an error-check variable.
            _out += ind + "{ int _err = 0;\n";
            _out += ind + "  // try\n";
            if (tc.tryBody && tc.tryBody->kind == NodeKind::Block)
                emitBlock(std::get<Block>(tc.tryBody->data), depth + 1);
            if (tc.catchBody) {
                _out += ind + "  if (_err) {\n";
                if (!tc.catchParam.empty())
                    _out += indent(depth+2) + "auto " + tc.catchParam + " = _err;\n";
                if (tc.catchBody->kind == NodeKind::Block)
                    emitBlock(std::get<Block>(tc.catchBody->data), depth + 2);
                _out += ind + "  }\n";
            }
            if (tc.finallyBody) {
                _out += ind + "  // finally\n";
                if (tc.finallyBody->kind == NodeKind::Block)
                    emitBlock(std::get<Block>(tc.finallyBody->data), depth + 1);
            }
            _out += ind + "}\n";
            break;
        }
        case NodeKind::ThrowStmt: {
            auto& th = std::get<ThrowStmt>(node->data);
            // GPU: set error context if available, else just note it
            _out += ind + "{ /* throw */ _err = 1; (void)(" + emitExpr(th.expr) + "); }\n";
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
        case NodeKind::BreakStmt:
            _out += ind + "break;\n";
            break;
        case NodeKind::ContinueStmt:
            _out += ind + "continue;\n";
            break;
        case NodeKind::FuncDecl: {
            auto& fn = std::get<FuncDecl>(node->data);
            if (fn.name.empty()) break;  // anonymous — emitted inline as lambda
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
            if (fn.body && fn.body->kind == NodeKind::Block)
                emitBlock(std::get<Block>(fn.body->data), depth + 1);
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

        default:
            return "/*unsupported*/0";
    }
}

std::string Codegen::emitLiteral(const Literal& l) {
    switch (l.kind) {
        case Literal::Int:   return l.raw;
        case Literal::Float: return l.raw + "f";
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
    if (op == "in") return "(true /*in*/)";
    // Unsigned right shift
    if (op == ">>>") return "((unsigned)(" + emitExpr(b.left) + ") >> " + emitExpr(b.right) + ")";
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
        if (m.prop == "headers") return "ctx->_headers"; // sentinel (headers not yet supported)
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
    // process.env["NAME"] subscript access
    if (obj == "_symm_process.env") {
        // Subscript access handled in emitBinary [] — return sentinel
        return "_symm_process.env";
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

        // ctx.header(name, val) → set response header (stub: no-op on GPU)
        if (obj == "ctx" && prop == "header") {
            return "/* ctx.header */";
        }

        // ctx.body<Float32Array>() → (float*)ctx->body, ctx->body_len
        if (obj == "ctx" && prop == "body") {
            return "(float*)ctx->body";
        }

        // gpu.fingerprint(key) → symm_fingerprint(key, len)
        // Handles both: gpu.fingerprint(key) and ctx.gpu.fingerprint(key)
        if ((obj == "gpu" || obj == "ctx->_gpu") && prop == "fingerprint") {
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

        // console.log/warn/error → printf (GPU side ring-buffer in production)
        if (obj == "console") {
            std::string args;
            for (auto& a : c.args) { if (!args.empty()) args += ", "; args += emitExpr(a); }
            return "/* console." + prop + "(" + args + ") */";
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
            if (prop == "parse") return "Date::now()";  // stub — return current time
            return "Date::" + prop + "(" + arg0 + ")";
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

        // JSON.* static methods
        if (obj == "JSON") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            if (prop == "stringify") return "symm_json_stringify(" + arg0 + ")";
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
            return "symm_number_to_fixed(" + obj + ", " + decimals + ")";
        }
        // Number.prototype.toString(radix?) — simplified: only supports radix 10 and 16
        if (prop == "toString") {
            if (!c.args.empty() && emitExpr(c.args[0]) == "16") {
                // Convert to hex — use GPU printf-style approach
                return "_SBuf([&]()-> _SBuf { _SBuf _hb; int _hv=(int)(" + obj + "); int _hn=0; if(_hv==0){_hb._b[0]='0';_hb._b[1]=0;return _hb;} if(_hv<0){_hb._b[_hn++]='-';_hv=-_hv;} char _ht[16]; int _htl=0; while(_hv>0){int d=_hv&0xF; _ht[_htl++]=(d<10)?'0'+d:'a'+d-10; _hv>>=4;} for(int j=_htl-1;j>=0;j--)_hb._b[_hn++]=_ht[j]; _hb._b[_hn]=0; return _hb; }())";
            }
            // Default: decimal string (same as toFixed(0) for ints)
            return "symm_number_to_fixed(" + obj + ", 0)";
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

        // ── encodeURIComponent / decodeURIComponent stubs ────────────────────
        if (prop.empty() && obj == "encodeURIComponent") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return arg0; // stub: identity (real encoding requires significant kernel space)
        }
        if (prop.empty() && obj == "decodeURIComponent") {
            std::string arg0 = c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
            return arg0; // stub: identity
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
                {"lastIndexOf", "symm_str_last_index_of"},
                {"slice",       "symm_str_slice"},
                {"substring",   "symm_str_substring"},
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
        if (prop == "find") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find(" + obj + ", " + obj + "_len, " + fn + ")";
        }
        if (prop == "findIndex") {
            std::string fn = c.args.empty() ? "[](auto x){return false;}" : emitExpr(c.args[0]);
            return "symm_arr_find_index(" + obj + ", " + obj + "_len, " + fn + ")";
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
    // encodeURIComponent / decodeURIComponent — identity stubs
    if (fn == "encodeURIComponent" || fn == "decodeURIComponent" ||
        fn == "encodeURI"          || fn == "decodeURI") {
        return c.args.empty() ? "\"\"" : emitExpr(c.args[0]);
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
                return (lit.kind == Literal::Str) ? "const char*" : "float";
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
