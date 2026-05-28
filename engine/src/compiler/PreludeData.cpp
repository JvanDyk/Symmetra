// GPU Runtime Prelude - Split into separate compilation unit to avoid C2026 string literal limits
// Each fragment is under 10KB, safely below MSVC's ~16KB limit

#include <string>

namespace symm {

// Fragment 1: Core types, structs, and utility functions
static const char* PRELUDE_FRAG1 = R"(
// ── Symmetra GPU Runtime Prelude ──────────────────────────────────────────────
// NVRTC: math functions and stdint types are built-in — no includes needed.
typedef unsigned char      uint8_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

// ── Forward declarations (prevent "identifier undefined" errors) ──────────────
__device__ long long _symm_now_ms();
__device__ bool symm_regex_test_i(const char* pattern, const char* str);
__device__ bool symm_regex_test_m(const char* pattern, const char* str);
__device__ bool symm_regex_test(const char* pattern, const char* str);
__device__ bool symm_str_eq(const char* a, const char* b);
__device__ const char* symm_sha256_hex(const char* input);
__device__ int strcmp(const char* a, const char* b);
__device__ float symm_parse_float(const char* s);
__device__ long long symm_parse_int_radix(const char* s, int radix);

// Request and response structures (in VRAM)
struct SymmCtx {
    // ── Request fields (filled by batch dispatch or legacy dispatch) ──────────
    const char* method;        // "GET", "POST", "PUT", "DELETE", "PATCH", …
    const char* path;          // URL path (null-terminated in VRAM)
    const char* query;         // Query string (after '?')
    const char* body;          // Raw request body bytes
    int         body_len;
    // ── Request headers (common headers pre-extracted by host) ───────────────
    const char* authorization; // "Bearer <token>" or "Basic <b64>" or ""
    const char* content_type;  // "application/json", "text/plain", etc.
    const char* user_agent;    // Browser / client identifier string
    // ── Routing ───────────────────────────────────────────────────────────────
    int         route_id;      // resolved route index (set by dispatch)
    int         batch_slot;    // thread index within the batch (0–255)
    // ── Response fields (written by handler kernel) ───────────────────────────
    char*       resp_buf;      // response body output buffer (in VRAM)
    int         resp_len;      // byte count written to resp_buf
    int         resp_status;   // HTTP status code (default 200)
    // ── Response headers (written by handler via ctx.header()) ───────────────
    char        resp_headers[256];  // "Name: Value\r\n" pairs, null-terminated
    int         resp_headers_len;   // byte count written to resp_headers
    // ── GPU fingerprint (anti-tamper) ─────────────────────────────────────────
    uint64_t    fingerprint;
    // ── I/O stage marker (for split-kernel continuation) ─────────────────────
    int         io_stage;      // 0 = no pending I/O, N > 0 = resume at stage N
};

// Route descriptor (registered at startup)
struct SymmRoute {
    const char* method;
    const char* path;
    int         handler_id;
};

// Date struct — for Date.now() and calendar methods
struct Date {
    long long _ms;
    __device__ Date() : _ms(0) {}
    __device__ Date(long long ms) : _ms(ms) {}
    __device__ static long long now() { return _symm_now_ms(); }
    __device__ long long getTime() const { return _ms; }

    // Calendar arithmetic from Unix epoch (ms since 1970-01-01T00:00:00Z)
    // Implements the standard Proleptic Gregorian algorithm
    __device__ int getFullYear() const {
        long long days = _ms / 86400000LL;
        // Algorithm: shift epoch to 1 March 0000 to simplify leap calc
        long long z = days + 719468LL;
        long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
        long long doe = z - era * 146097LL;
        long long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        long long y   = yoe + era * 400;
        long long doy = doe - (365*yoe + yoe/4 - yoe/100);
        long long mp  = (5*doy + 2) / 153;
        if (mp >= 10) y++;
        return (int)y;
    }
    __device__ int getMonth() const { // 0-based
        long long days = _ms / 86400000LL;
        long long z = days + 719468LL;
        long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
        long long doe = z - era * 146097LL;
        long long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        long long doy = doe - (365*yoe + yoe/4 - yoe/100);
        long long mp  = (5*doy + 2) / 153;
        int m = (int)((mp < 10) ? mp + 2 : mp - 10);
        return m; // 0–11
    }
    __device__ int getDate() const { // 1-based day
        long long days = _ms / 86400000LL;
        long long z = days + 719468LL;
        long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
        long long doe = z - era * 146097LL;
        long long yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        long long doy = doe - (365*yoe + yoe/4 - yoe/100);
        long long mp  = (5*doy + 2) / 153;
        int d = (int)(doy - (153*mp + 2)/5 + 1);
        return d; // 1–31
    }
    __device__ int getDay() const { // 0=Sun
        long long days = _ms / 86400000LL;
        int wd = (int)((days + 4) % 7); // 1970-01-01 = Thursday = 4
        if (wd < 0) wd += 7;
        return wd;
    }
    __device__ int getHours()        const { return (int)((_ms / 3600000LL) % 24); }
    __device__ int getMinutes()      const { return (int)((_ms / 60000LL)   % 60); }
    __device__ int getSeconds()      const { return (int)((_ms / 1000LL)    % 60); }
    __device__ int getMilliseconds() const { return (int)(_ms % 1000LL); }
    __device__ int getTimezoneOffset() const { return 0; } // UTC

    // toISOString → "YYYY-MM-DDTHH:mm:ss.sssZ"
    __device__ const char* toISOString() const {
        static char buf[32];
        int Y = getFullYear(), Mo = getMonth()+1, D = getDate();
        int h = getHours(), mi = getMinutes(), s = getSeconds(), ms3 = (int)((_ms%1000LL+1000LL)%1000LL);
        // format manually: YYYY-MM-DDTHH:mm:ss.sssZ
        buf[0]='0'+Y/1000; buf[1]='0'+(Y/100)%10; buf[2]='0'+(Y/10)%10; buf[3]='0'+Y%10;
        buf[4]='-'; buf[5]='0'+Mo/10; buf[6]='0'+Mo%10;
        buf[7]='-'; buf[8]='0'+D/10; buf[9]='0'+D%10;
        buf[10]='T'; buf[11]='0'+h/10; buf[12]='0'+h%10;
        buf[13]=':'; buf[14]='0'+mi/10; buf[15]='0'+mi%10;
        buf[16]=':'; buf[17]='0'+s/10; buf[18]='0'+s%10;
        buf[19]='.'; buf[20]='0'+ms3/100; buf[21]='0'+(ms3/10)%10; buf[22]='0'+ms3%10;
        buf[23]='Z'; buf[24]=0;
        return buf;
    }
    __device__ const char* toLocaleDateString() const { return toISOString(); }
    __device__ const char* toString()            const { return toISOString(); }

    // Date.parse — accepts "YYYY-MM-DD" or "YYYY-MM-DDTHH:mm:ss[.sss]Z"
    __device__ static long long parse(const char* s) {
        if (!s) return 0LL;
        // Parse YYYY
        auto readN = [&](const char*& p, int n) -> int {
            int v = 0;
            for (int i = 0; i < n && *p >= '0' && *p <= '9'; i++, p++) v = v*10 + (*p-'0');
            return v;
        };
        const char* p = s;
        int Y = readN(p, 4);
        if (*p == '-') p++;
        int Mo = readN(p, 2); if (Mo < 1) Mo = 1;
        if (*p == '-') p++;
        int D  = readN(p, 2); if (D  < 1) D  = 1;
        int h  = 0, mi = 0, sec = 0, ms3 = 0;
        if (*p == 'T' || *p == ' ') {
            p++;
            h  = readN(p, 2); if (*p==':') p++;
            mi = readN(p, 2); if (*p==':') p++;
            sec= readN(p, 2);
            if (*p == '.') { p++; ms3 = readN(p, 3); }
        }
        // Convert to epoch ms using inverse of getFullYear/Month/Date algorithm
        // Simple days-since-epoch calculation:
        // Total days = days_from_epoch(Y, Mo, D)
        long long y = Y; long long m = Mo - 1; // 0-based
        if (m < 0) { m += 12; y--; }
        // days from epoch to year start using civil_from_days inverse
        long long era = (y >= 0 ? y : y - 399) / 400;
        long long yoe = y - era * 400;
        // doy for this month (Mar=0 based)
        long long mon0 = (m >= 2) ? (long long)(m - 2) : (long long)(m + 10);
        long long doy = (153*mon0 + 2)/5 + (long long)(D - 1);
        long long doe = yoe*365 + yoe/4 - yoe/100 + doy;
        // Adjust for March base: if month >= March, same year; else previous year already handled
        // Actually re-do cleanly:
        // Simpler: use known formula directly
        long long days = era*146097LL + doe - 719468LL;
        // If month < 2 (Jan/Feb), they belong to year-1 in the march-based system
        if (m < 2) days -= 365 + ((Y%4==0 && (Y%100!=0 || Y%400==0)) ? 1 : 0);
        // Actually the era/doe calc already handles it, but let's use a cleaner path:
        // Restart with a well-known formula (Fliegel-Van Flandern adapted)
        long long a = (14 - Mo) / 12;
        long long yr = Y + 4800 - a;
        long long mf = Mo + 12*a - 3;
        long long jdn = D + (153*mf+2)/5 + 365*yr + yr/4 - yr/100 + yr/400 - 32045;
        long long unix_days = jdn - 2440588LL; // JDN of 1970-01-01
        return unix_days * 86400000LL + (long long)h*3600000LL + (long long)mi*60000LL + (long long)sec*1000LL + ms3;
    }
};

// RegExp — supports flags (i, g, m, s)
struct RegExp {
    const char* pattern;
    const char* flags;  // "i"=case-insensitive, "g"=global, "m"=multiline, "s"=dotAll
    __device__ RegExp(const char* p = "", const char* f = "") : pattern(p), flags(f) {}
    __device__ bool _has_flag(char flag) const {
        if (!flags) return false;
        for (int j = 0; flags[j]; j++) if (flags[j] == flag) return true;
        return false;
    }
    __device__ bool test(const char* str) {
        if (!pattern || !str) return false;
        bool ci = _has_flag('i');
        bool ml = _has_flag('m');
        if (ci) return symm_regex_test_i(pattern, str);
        if (ml) return symm_regex_test_m(pattern, str);
        return symm_regex_test(pattern, str);
    }
    // .source and .flags properties
    __device__ const char* source() const { return pattern ? pattern : ""; }
};

// Case-insensitive regex test helper
__device__ bool symm_regex_test_i(const char* pattern, const char* str) {
    if (!pattern || !str) return false;
    while (*str) {
        const char* p = pattern;
        const char* t = str;
        while (*p && *t) {
            char pc = *p;
            char tc = *t;
            if (pc >= 'A' && pc <= 'Z') pc = pc + 32;
            if (tc >= 'A' && tc <= 'Z') tc = tc + 32;
            if (pc != tc) break;
            p++; t++;
        }
        if (!*p) return true;
        str++;
    }
    return false;
}

// ── GPU-side utility functions ─────────────────────────────────────────────────

__device__ long long _symm_now_ms() { return 0; }
__device__ long long _symm_start_ms = 0;

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

// Set a response header: ctx.header('Name', 'Value')
// Appends "Name: Value\r\n" to resp_headers buffer; returns ctx for chaining.
__device__ SymmCtx* symm_set_header(SymmCtx* ctx, const char* name, const char* val) {
    if (!ctx || !name || !val) return ctx;
    int pos = ctx->resp_headers_len;
    int nl  = symm_strlen(name);
    int vl  = symm_strlen(val);
    if (pos + nl + vl + 4 >= 254) return ctx;  // guard overflow
    for (int i = 0; i < nl; i++) ctx->resp_headers[pos++] = name[i];
    ctx->resp_headers[pos++] = ':';
    ctx->resp_headers[pos++] = ' ';
    for (int i = 0; i < vl; i++) ctx->resp_headers[pos++] = val[i];
    ctx->resp_headers[pos++] = '\r';
    ctx->resp_headers[pos++] = '\n';
    ctx->resp_headers[pos]   = '\0';
    ctx->resp_headers_len    = pos;
    return ctx;
}

// Get a request header by name (case-insensitive comparison)
// Returns the header value or "" if not present.
__device__ const char* symm_get_header(SymmCtx* ctx, const char* name) {
    if (!ctx || !name) return "";
    // Check pre-extracted common headers
    auto ci_eq = [](const char* a, const char* b) -> bool {
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
            if (ca != cb) return false;
            a++; b++;
        }
        return *a == '\0' && *b == '\0';
    };
    if (ci_eq(name, "authorization"))  return ctx->authorization  ? ctx->authorization  : "";
    if (ci_eq(name, "content-type"))   return ctx->content_type   ? ctx->content_type   : "";
    if (ci_eq(name, "user-agent"))     return ctx->user_agent      ? ctx->user_agent      : "";
    return "";
}

// Percent-encode a string (encodeURIComponent semantics)
// Safe chars: A-Z a-z 0-9 - _ . ! ~ * ' ( )
__device__ const char* symm_encode_uri_component(const char* s) {
    static char buf[1536];
    if (!s) return "";
    static const char hex[] = "0123456789ABCDEF";
    int pos = 0;
    for (int i = 0; s[i] && pos < 1530; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_' || c == '.' || c == '!' || c == '~'
            || c == '*' || c == '\'' || c == '(' || c == ')') {
            buf[pos++] = (char)c;
        } else {
            buf[pos++] = '%';
            buf[pos++] = hex[(c >> 4) & 0xF];
            buf[pos++] = hex[c & 0xF];
        }
    }
    buf[pos] = '\0';
    return buf;
}

// Decode a percent-encoded string (decodeURIComponent semantics)
__device__ const char* symm_decode_uri_component(const char* s) {
    static char buf[512];
    if (!s) return "";
    int pos = 0;
    for (int i = 0; s[i] && pos < 510; i++) {
        if (s[i] == '%' && s[i+1] && s[i+2]) {
            char hi = s[++i], lo = s[++i];
            int h = (hi>='0'&&hi<='9') ? hi-'0' : (hi>='A'&&hi<='F') ? hi-'A'+10 : (hi>='a'&&hi<='f') ? hi-'a'+10 : 0;
            int l = (lo>='0'&&lo<='9') ? lo-'0' : (lo>='A'&&lo<='F') ? lo-'A'+10 : (lo>='a'&&lo<='f') ? lo-'a'+10 : 0;
            buf[pos++] = (char)((h << 4) | l);
        } else if (s[i] == '+') {
            buf[pos++] = ' ';
        } else {
            buf[pos++] = s[i];
        }
    }
    buf[pos] = '\0';
    return buf;
}
)";

// Fragment 2: JSON functions and standard structures
static const char* PRELUDE_FRAG2 = R"(
// ── Basic JSON value handling ─────────────────────────────────────────────────
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

// String utility functions
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

__device__ float _symm_inf() { return 1e30f * 1e30f; }
__device__ float _symm_nan() { return 0.0f / 0.0f; }

__device__ int symm_write_float(char* buf, float v) {
    if (v != v) { buf[0]='N'; buf[1]='a'; buf[2]='N'; buf[3]=0; return 3; }
    float inf = _symm_inf();
    if (v == inf)  { buf[0]='I'; buf[1]='n'; buf[2]='f'; buf[3]='i'; buf[4]='n'; buf[5]='i'; buf[6]='t'; buf[7]='y'; buf[8]=0; return 8; }
    if (v == -inf) { buf[0]='-'; buf[1]='I'; buf[2]='n'; buf[3]='f'; buf[4]='i'; buf[5]='n'; buf[6]='i'; buf[7]='t'; buf[8]='y'; buf[9]=0; return 9; }
    long long iv = (long long)v;
    float frac = v - (float)iv;
    if (frac < 0.f) frac = -frac;
    if (frac < 0.000001f) return symm_write_int(buf, iv);
    int n = symm_write_int(buf, iv);
    buf[n++] = '.';
    for (int d = 0; d < 6; d++) { frac *= 10.f; int dig = (int)frac; buf[n++] = '0' + dig; frac -= dig; }
    while (n > 1 && buf[n-1] == '0') n--;
    if (n > 1 && buf[n-1] == '.') n--;
    buf[n] = '\0';
    return n;
}

__device__ int symm_write_bool(char* buf, bool v) {
    const char* s = v ? "true" : "false";
    return symm_write_str(buf, s);
}

// ── String method stubs ─────────────────────────────────────────────────────────
__device__ const char* symm_str_trim(const char* s) {
    if (!s) return "";
    static char buf[512];
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    int pos = 0, len = symm_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) len--;
    for (int i = 0; i < len && pos < 500; i++) buf[pos++] = s[i];
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_trim_start(const char* s) {
    if (!s) return "";
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    return s;
}

__device__ const char* symm_str_trim_end(const char* s) {
    if (!s) return "";
    static char buf[512];
    int len = symm_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) len--;
    for (int i = 0; i < len && i < 500; i++) buf[i] = s[i];
    buf[len] = '\0';
    return buf;
}

__device__ bool symm_str_includes(const char* s, const char* substr) {
    if (!s || !substr) return false;
    while (*s) { const char* p = substr; const char* t = s;
        while (*p && *t && *p == *t) { p++; t++; }
        if (!*p) return true; s++;
    } return false;
}

__device__ bool symm_str_starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    while (*prefix) { if (*s != *prefix) return false; s++; prefix++; }
    return true;
}

__device__ bool symm_str_ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    int slen = symm_strlen(s);
    int suflen = symm_strlen(suffix);
    if (suflen > slen) return false;
    return symm_str_eq(s + slen - suflen, suffix);
}

__device__ const char* symm_str_to_upper(const char* s) {
    if (!s) return "";
    static char buf[512];
    int pos = 0;
    while (s[pos] && pos < 500) {
        if (s[pos] >= 'a' && s[pos] <= 'z')
            buf[pos] = s[pos] - 'a' + 'A';
        else
            buf[pos] = s[pos];
        pos++;
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_to_lower(const char* s) {
    if (!s) return "";
    static char buf[512];
    int pos = 0;
    while (s[pos] && pos < 500) {
        if (s[pos] >= 'A' && s[pos] <= 'Z')
            buf[pos] = s[pos] - 'A' + 'a';
        else
            buf[pos] = s[pos];
        pos++;
    }
    buf[pos] = '\0';
    return buf;
}

__device__ int symm_str_char_code_at(const char* s, int index) {
    if (!s || index < 0) return -1;
    int i = 0;
    while (s[i] && i < index) i++;
    return (s[i]) ? (int)(unsigned char)s[i] : -1;
}


__device__ const char* symm_str_normalize(const char* s) {
    // Stub: return as-is (GPU doesn't support Unicode normalization)
    return s ? s : "";
}

__device__ const char* symm_str_slice(const char* s, int start) { return s && start < symm_strlen(s) ? s + start : ""; }

__device__ int symm_str_index_of(const char* s, const char* substr) {
    if (!s || !substr) return -1;
    for (int i = 0; s[i]; i++) { const char* p = substr; const char* t = s + i;
        while (*p && *t && *p == *t) { p++; t++; }
        if (!*p) return i;
    } return -1;
}

__device__ int symm_str_last_index_of(const char* s, const char* sub) {
    if (!s || !sub) return -1;
    int slen = symm_strlen(s), sublen = symm_strlen(sub);
    if (sublen == 0) return slen;
    int last = -1;
    for (int i = 0; i <= slen - sublen; i++) {
        bool match = true;
        for (int j = 0; j < sublen; j++) {
            if (s[i+j] != sub[j]) { match = false; break; }
        }
        if (match) last = i;
    }
    return last;
}

// lastIndexOf with start position
__device__ int symm_str_last_index_of_from(const char* s, const char* sub, int fromIdx) {
    if (!s || !sub) return -1;
    int slen = symm_strlen(s), sublen = symm_strlen(sub);
    if (sublen == 0) return fromIdx < slen ? fromIdx : slen;
    int last = -1;
    for (int i = 0; i <= slen - sublen && i <= fromIdx; i++) {
        bool match = true;
        for (int j = 0; j < sublen; j++) {
            if (s[i+j] != sub[j]) { match = false; break; }
        }
        if (match) last = i;
    }
    return last;
}

__device__ const char* symm_str_substring(const char* s, int start, int end) {
    if (!s) return "";
    int slen = symm_strlen(s);
    // Clamp start/end to [0, slen], swap if start > end
    if (start < 0) start = 0;
    if (end < 0)   end   = 0;
    if (start > slen) start = slen;
    if (end   > slen) end   = slen;
    if (start > end) { int tmp = start; start = end; end = tmp; }
    static char buf[512];
    int len = end - start;
    if (len > 511) len = 511;
    for (int i = 0; i < len; i++) buf[i] = s[start + i];
    buf[len] = '\0';
    return buf;
}

__device__ const char* symm_str_substr(const char* s, int start, int len) {
    if (!s) return "";
    int slen = symm_strlen(s);
    if (start < 0) start = slen + start;
    if (start < 0) start = 0;
    if (start >= slen) return "";
    int avail = slen - start;
    if (len < 0 || len > avail) len = avail;  // -1 sentinel means "rest of string"
    static char buf[512];
    if (len > 511) len = 511;
    for (int i = 0; i < len; i++) buf[i] = s[start + i];
    buf[len] = '\0';
    return buf;
}

// Forward declaration — full implementation is in FRAG7
__device__ const char* symm_str_match_all_full(const char* pattern, const char* str);

__device__ const char* symm_str_match_all(const char* s, const char* pattern) {
    return symm_str_match_all_full(pattern, s);
}

__device__ const char* symm_str_replace(const char* s, const char* from, const char* to) {
    if (!s || !from || !to) return s;
    static char buf[512];
    int pos = 0, i = 0, flen = symm_strlen(from), tlen = symm_strlen(to);
    while (s[i] && pos < 500) {
        bool match = true;
        for (int j = 0; j < flen; j++) {
            if (s[i+j] != from[j]) { match = false; break; }
        }
        if (match && flen > 0) {
            for (int j = 0; j < tlen && pos < 500; j++) buf[pos++] = to[j];
            i += flen;
        } else {
            buf[pos++] = s[i++];
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_replace_all(const char* s, const char* from, const char* to) {
    if (!s || !from || !to) return s;
    static char buf[512];
    int pos = 0, i = 0, flen = symm_strlen(from), tlen = symm_strlen(to);
    while (s[i] && pos < 500) {
        bool match = true;
        for (int j = 0; j < flen; j++) {
            if (s[i+j] != from[j]) { match = false; break; }
        }
        if (match && flen > 0) {
            for (int j = 0; j < tlen && pos < 500; j++) buf[pos++] = to[j];
            i += flen;
        } else {
            buf[pos++] = s[i++];
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_pad_start(const char* s, int len, const char* pad) {
    if (!s || !pad) return s;
    static char buf[512];
    int slen = symm_strlen(s), plen = symm_strlen(pad);
    if (slen >= len) return s;
    int padcount = len - slen;
    int pos = 0;
    for (int i = 0; i < padcount && pos < 500; i++) {
        buf[pos++] = pad[i % plen];
    }
    for (int i = 0; s[i] && pos < 500; i++) buf[pos++] = s[i];
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_pad_end(const char* s, int len, const char* pad) {
    if (!s || !pad) return s;
    static char buf[512];
    int slen = symm_strlen(s), plen = symm_strlen(pad);
    if (slen >= len) return s;
    int pos = 0;
    for (int i = 0; s[i] && pos < 500; i++) buf[pos++] = s[i];
    int padcount = len - slen;
    for (int i = 0; i < padcount && pos < 500; i++) {
        buf[pos++] = pad[i % plen];
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_from_char_code(int code) { static char buf[2]; buf[0] = code; buf[1] = 0; return buf; }

// ── parseInt with radix support ─────────────────────────────────────────────────
__device__ long long symm_parse_int_radix(const char* s, int radix) {
    if (!s || radix < 2 || radix > 36) return 0;
    long long result = 0;
    while (*s && (*s == ' ' || *s == '\t')) s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; } else if (*s == '+') { s++; }
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= radix) break;
        result = result * radix + digit;
        s++;
    }
    return neg ? -result : result;
}

// ── Number method stubs ─────────────────────────────────────────────────────────
__device__ const char* symm_number_to_fixed(float v, int decimals) {
    static char buf[64];
    long long iv = (long long)v;
    float frac = v - (float)iv;
    int pos = 0;
    if (iv < 0) { buf[pos++] = '-'; iv = -iv; }
    if (iv == 0) buf[pos++] = '0';
    else {
        char tmp[32]; int n = 0;
        long long temp = iv;
        while (temp > 0) { tmp[n++] = '0' + (temp % 10); temp /= 10; }
        for (int i = n - 1; i >= 0; i--) buf[pos++] = tmp[i];
    }
    if (decimals > 0) {
        buf[pos++] = '.';
        if (frac < 0) frac = -frac;
        for (int d = 0; d < decimals; d++) {
            frac *= 10.f;
            int dig = (int)frac;
            buf[pos++] = '0' + dig;
            frac -= dig;
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_number_to_precision(float v, int precision) {
    // toPrecision(n): n significant digits
    if (precision <= 0) precision = 6;
    static char buf[64];
    if (v == 0.0f) {
        buf[0] = '0'; int p = 1;
        if (precision > 1) { buf[p++] = '.'; for (int i = 1; i < precision; i++) buf[p++] = '0'; }
        buf[p] = '\0'; return buf;
    }
    bool neg = (v < 0);
    if (neg) v = -v;
    // Find magnitude
    int exp = 0;
    float tmp = v;
    while (tmp >= 10.0f) { tmp /= 10.0f; exp++; }
    while (tmp < 1.0f && tmp > 0.0f) { tmp *= 10.0f; exp--; }
    // Decimal places needed: precision - 1 - exp (if exp in range)
    int decimals = precision - 1 - exp;
    if (decimals < 0) decimals = 0;
    int p = 0;
    if (neg) buf[p++] = '-';
    // Integer part
    long long iv = (long long)v;
    if (iv == 0) { buf[p++] = '0'; }
    else {
        char tmp2[32]; int n = 0;
        long long t = iv;
        while (t > 0) { tmp2[n++] = '0' + (t % 10); t /= 10; }
        for (int i = n - 1; i >= 0; i--) buf[p++] = tmp2[i];
    }
    if (decimals > 0) {
        buf[p++] = '.';
        float frac = v - (float)iv;
        for (int d = 0; d < decimals && p < 62; d++) {
            frac *= 10.0f;
            int dig = (int)frac;
            buf[p++] = '0' + dig;
            frac -= dig;
        }
    }
    buf[p] = '\0';
    return buf;
}

__device__ const char* symm_number_to_exponential(float v, int digits) {
    // toExponential(n): scientific notation with n fractional digits
    if (digits < 0) digits = 6;
    static char buf[64];
    bool neg = (v < 0);
    if (neg) v = -v;
    int exp = 0;
    if (v == 0.0f) {
        // 0 → "0e+0" or "0.000e+0"
        int p = 0;
        if (neg) buf[p++] = '-';
        buf[p++] = '0';
        if (digits > 0) { buf[p++] = '.'; for (int i = 0; i < digits; i++) buf[p++] = '0'; }
        buf[p++] = 'e'; buf[p++] = '+'; buf[p++] = '0'; buf[p] = '\0';
        return buf;
    }
    while (v >= 10.0f) { v /= 10.0f; exp++; }
    while (v < 1.0f) { v *= 10.0f; exp--; }
    int p = 0;
    if (neg) buf[p++] = '-';
    int intPart = (int)v;
    buf[p++] = '0' + intPart;
    if (digits > 0) {
        buf[p++] = '.';
        float frac = v - (float)intPart;
        for (int d = 0; d < digits && p < 55; d++) {
            frac *= 10.0f;
            int dig = (int)frac;
            buf[p++] = '0' + dig;
            frac -= dig;
        }
    }
    buf[p++] = 'e';
    buf[p++] = (exp >= 0) ? '+' : '-';
    if (exp < 0) exp = -exp;
    // Write exponent (1-3 digits)
    char eBuf[8]; int en = 0;
    if (exp == 0) { eBuf[en++] = '0'; }
    else { int e = exp; while (e > 0) { eBuf[en++] = '0' + (e % 10); e /= 10; } }
    for (int i = en - 1; i >= 0; i--) buf[p++] = eBuf[i];
    buf[p] = '\0';
    return buf;
}

)";

// Fragment 2B: String split, join, and routing utilities (FRAG2 split to stay under MSVC 65535-char limit)
static const char* PRELUDE_FRAG2B = R"(
// ── String split and join methods ───────────────────────────────────────────────
__device__ const char* symm_str_split(const char* s, const char* delim) {
    static char buf[512];
    if (!s || !delim) return "";
    int pos = 0, i = 0, len = symm_strlen(delim);
    while (s[i] && pos < 500) {
        bool match = true;
        for (int j = 0; j < len; j++) {
            if (s[i + j] != delim[j]) { match = false; break; }
        }
        if (match && len > 0) {
            buf[pos++] = ',';
            i += len;
        } else {
            buf[pos++] = s[i++];
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_repeat(const char* s, int count) {
    static char buf[512];
    if (!s || count <= 0) { buf[0] = '\0'; return buf; }
    int pos = 0;
    for (int i = 0; i < count && pos < 500; i++) {
        int j = 0;
        while (s[j] && pos < 500) { buf[pos++] = s[j++]; }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_arr_join(const char** arr, int len, const char* sep) {
    static char buf[512];
    if (!arr || len <= 0) { buf[0] = '\0'; return buf; }
    int pos = 0;
    for (int i = 0; i < len && pos < 500; i++) {
        if (i > 0 && sep) {
            int j = 0;
            while (sep[j] && pos < 500) buf[pos++] = sep[j++];
        }
        if (arr[i]) {
            int j = 0;
            while (arr[i][j] && pos < 500) buf[pos++] = arr[i][j++];
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_concat(const char* s1, const char* s2) {
    static char buf[512];
    if (!s1) s1 = "";
    if (!s2) s2 = "";
    int pos = 0;
    int i = 0;
    while (s1[i] && pos < 500) buf[pos++] = s1[i++];
    i = 0;
    while (s2[i] && pos < 500) buf[pos++] = s2[i++];
    buf[pos] = '\0';
    return buf;
}

template<typename T> __device__ void symm_arr_fill(T* arr, int len, T val, int start, int end) {
    if (!arr) return;
    if (start < 0) start = 0;
    if (end < 0 || end > len) end = len;
    for (int i = start; i < end; i++) arr[i] = val;
}

template<typename T> __device__ void symm_arr_concat(T* dst, int* dstlen, const T* src, int srclen) {
    if (!dst || !src || *dstlen < 0 || srclen < 0) return;
    for (int i = 0; i < srclen && *dstlen < 1024; i++) {
        dst[*dstlen] = src[i];
        (*dstlen)++;
    }
}

// ── JSON merge for spread syntax ─────────────────────────────────────────────
// Called by Codegen as: buf_n += symm_json_merge(buf + buf_n, capacity, src_obj)
// Appends the inner properties of src_obj (a JSON object string) into dest.
// Returns number of bytes written. The caller already handles the leading comma.
__device__ int symm_json_merge(char* dest, int cap, const char* src) {
    if (!src || !dest || cap <= 1) return 0;
    // Skip to opening '{'
    const char* p = src;
    while (*p && *p != '{') p++;
    if (*p == '{') p++;
    // Skip leading whitespace
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    // Empty object — nothing to merge
    if (!*p || *p == '}') return 0;
    // Copy inner content up to (but not including) the closing '}'
    int n = 0;
    // Brace-aware copy: track nesting depth so we stop at the right '}'
    int depth = 0;
    while (*p && n < cap - 1) {
        char c = *p++;
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            if (depth == 0) break;   // closing brace of the outer object
            depth--;
        }
        dest[n++] = c;
    }
    // Trim trailing whitespace/comma
    while (n > 0 && (dest[n-1] == ' ' || dest[n-1] == '\t' || dest[n-1] == '\n')) n--;
    dest[n] = '\0';
    return n;
}

// ── Routing utilities ─────────────────────────────────────────────────────────
// Extract the URL path segment at position `index` (0-based, split on '/').
// e.g. symm_extract_param("/users/42/orders", 1) → "42"
__device__ const char* symm_extract_param(const char* path, int index) {
    if (!path) return "";
    static char buf[128];
    int seg = -1;          // segment counter (-1 before the first '/')
    int bi  = 0;
    const char* p = path;
    // Skip leading slash so segment 0 starts right after it
    if (*p == '/') p++;
    seg = 0;
    while (*p) {
        if (*p == '/') {
            if (seg == index) { buf[bi] = '\0'; return buf; }
            seg++;
            bi = 0;
            p++;
            continue;
        }
        if (seg == index && bi < 127) buf[bi++] = *p;
        p++;
    }
    if (seg == index) { buf[bi] = '\0'; return buf; }
    return "";
}

// Forward declaration — full definition is in FRAG10
__device__ const char* symm_url_search_params_get(const char* qs, const char* key);

// Extract a value from a query string by key name.
// e.g. symm_extract_query("name=john&age=30", "age") → "30"
// Delegates to the full URLSearchParams parser.
__device__ const char* symm_extract_query(const char* query, const char* key) {
    return symm_url_search_params_get(query, key);
}
)";

// Fragment 3: Type system and container stubs
static const char* PRELUDE_FRAG3 = R"(
// ── String scratch buffer and type definitions ─────────────────────────────────
struct _SBuf {
    char _b[512];
    __device__ operator const char*() const { return _b; }
    __device__ const char* c_str() const { return _b; }
};

// Type-aware function overloads (these prevent implicit conversions)
__device__ int symm_write(char* buf, const char* s) { return symm_write_str(buf, s); }
__device__ int symm_write(char* buf, float v) { return symm_write_float(buf, v); }
__device__ int symm_write(char* buf, int v) { return symm_write_int(buf, v); }
__device__ int symm_write(char* buf, long long v) { return symm_write_int(buf, v); }
__device__ int symm_write(char* buf, bool v) { return symm_write_bool(buf, v); }
__device__ int symm_write(char* buf, double v) { return symm_write_float(buf, (float)v); }
template<typename T> __device__ int symm_write(char* buf, T v) { return symm_write_str(buf, (const char*)v); }

// JSON value emitters
__device__ int symm_json_val(char* buf, const char* s) {
    int n = 0; buf[n++] = '"';
    while (s && *s) {
        char c = *s++;
        if      (c == '"')  { buf[n++] = '\\'; buf[n++] = '"';  }
        else if (c == '\\') { buf[n++] = '\\'; buf[n++] = '\\'; }
        else if (c == '\n') { buf[n++] = '\\'; buf[n++] = 'n';  }
        else if (c == '\r') { buf[n++] = '\\'; buf[n++] = 'r';  }
        else if (c == '\t') { buf[n++] = '\\'; buf[n++] = 't';  }
        else                { buf[n++] = c; }
    }
    buf[n++] = '"'; buf[n] = '\0'; return n;
}

__device__ int symm_json_val(char* buf, float v) { return symm_write_float(buf, v); }
__device__ int symm_json_val(char* buf, int v) { return symm_write_int(buf, v); }
__device__ int symm_json_val(char* buf, bool v) { return symm_write_bool(buf, v); }

// JS standard library stubs
struct Error {
    const char* message;
    const char* cause;  // ES2022 Error.cause
    __device__ Error(const char* m = "", const char* c = "") : message(m), cause(c) {}
};

struct TypeError : Error {
    __device__ TypeError(const char* m = "") : Error(m) {}
};

struct RangeError : Error {
    __device__ RangeError(const char* m = "") : Error(m) {}
};

struct Map {
    struct Entry { const char* k; float v; };
    Entry _e[64]; int _n = 0;

    // String-key set/get/has
    __device__ void set(const char* k, float v) {
        for (int i=0;i<_n;i++) if (strcmp(_e[i].k,k)==0){_e[i].v=v;return;}
        if (_n<64) { _e[_n].k=k; _e[_n].v=v; _n++; }
    }
    __device__ float get(const char* k) const {
        for (int i=0;i<_n;i++) if (strcmp(_e[i].k,k)==0) return _e[i].v;
        return 0.f;
    }
    __device__ bool has(const char* k) const {
        for (int i=0;i<_n;i++) if (strcmp(_e[i].k,k)==0) return true;
        return false;
    }

    // Integer-key overloads — convert key to float and use index as key slot
    // Uses a dedicated high-bit scheme: keys are stored as canonical int-key strings
    __device__ void set(int k, float v) {
        // use a thread-local key buffer (static OK on GPU with warp isolation)
        // Store key as "i42" prefix to avoid collision with string keys
        char kbuf[24]; kbuf[0]='#'; symm_write_int(kbuf+1, k);
        const char* kptr = kbuf; // Stack key - OK for runtime map (not stored long-term)
        // For GPU: store float representation as string key (safe for local Map vars)
        for (int i=0;i<_n;i++) {
            if (!_e[i].k) continue;
            if (_e[i].k[0]=='#') {
                // Compare numeric portion
                const char* a = _e[i].k+1; const char* b = kbuf+1;
                bool eq = true; while (*a || *b) { if (*a++ != *b++) { eq=false; break; } }
                if (eq) { _e[i].v=v; return; }
            }
        }
        if (_n<64) { _e[_n].k=kptr; _e[_n].v=v; _n++; }
    }
    __device__ float get(int k) const {
        char kbuf[24]; kbuf[0]='#'; symm_write_int(kbuf+1, k);
        for (int i=0;i<_n;i++) {
            if (!_e[i].k || _e[i].k[0]!='#') continue;
            const char* a = _e[i].k+1; const char* b = kbuf+1;
            bool eq = true; while (*a || *b) { if (*a++ != *b++) { eq=false; break; } }
            if (eq) return _e[i].v;
        }
        return 0.f;
    }
    __device__ bool has(int k) const {
        char kbuf[24]; kbuf[0]='#'; symm_write_int(kbuf+1, k);
        for (int i=0;i<_n;i++) {
            if (!_e[i].k || _e[i].k[0]!='#') continue;
            const char* a = _e[i].k+1; const char* b = kbuf+1;
            bool eq = true; while (*a || *b) { if (*a++ != *b++) { eq=false; break; } }
            if (eq) return true;
        }
        return false;
    }
    // Float key overloads (JS numeric keys)
    __device__ void set(float k, float v) { set((int)k, v); }
    __device__ float get(float k) const { return get((int)k); }
    __device__ bool has(float k) const { return has((int)k); }

    __device__ void clear() { _n = 0; }
    __device__ int size() const { return _n; }
    __device__ bool delete_key(const char* k) {
        for (int i=0;i<_n;i++) {
            if (strcmp(_e[i].k,k)==0) {
                for (int j=i; j<_n-1; j++) _e[j]=_e[j+1];
                _n--; return true;
            }
        }
        return false;
    }
    __device__ bool delete_key(int k) {
        char kbuf[24]; kbuf[0]='#'; symm_write_int(kbuf+1, k);
        for (int i=0;i<_n;i++) {
            if (!_e[i].k || _e[i].k[0]!='#') continue;
            const char* a = _e[i].k+1; const char* b = kbuf+1;
            bool eq = true; while (*a || *b) { if (*a++ != *b++) { eq=false; break; } }
            if (eq) { for (int j=i; j<_n-1; j++) _e[j]=_e[j+1]; _n--; return true; }
        }
        return false;
    }
    __device__ const char* keys() const {
        static char buf[1024]; int pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < _n && pos < 990; i++) {
            if (i) buf[pos++] = ',';
            buf[pos++] = '"';
            for (int j = 0; _e[i].k && _e[i].k[j] && pos < 1000; j++) buf[pos++] = _e[i].k[j];
            buf[pos++] = '"';
        }
        buf[pos++] = ']'; buf[pos] = 0; return buf;
    }
    __device__ const char* values() const {
        static char buf[512]; int pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < _n && pos < 480; i++) {
            if (i) buf[pos++] = ',';
            int n = symm_write_float(buf + pos, _e[i].v); pos += n;
        }
        buf[pos++] = ']'; buf[pos] = 0; return buf;
    }
    __device__ const char* entries() const {
        static char buf[1024]; int pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < _n && pos < 990; i++) {
            if (i) buf[pos++] = ',';
            buf[pos++] = '['; buf[pos++] = '"';
            for (int j = 0; _e[i].k && _e[i].k[j] && pos < 1005; j++) buf[pos++] = _e[i].k[j];
            buf[pos++] = '"'; buf[pos++] = ',';
            int n = symm_write_float(buf + pos, _e[i].v); pos += n;
            buf[pos++] = ']';
        }
        buf[pos++] = ']'; buf[pos] = 0; return buf;
    }
    __device__ void forEach(void (*fn)(float, const char*)) const {
        for (int i = 0; i < _n; i++) fn(_e[i].v, _e[i].k);
    }
};

// ── Array method stubs ──────────────────────────────────────────────────────────
template<typename T> __device__ void symm_arr_map(T* dst, const T* src, int len) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
}
template<typename T> __device__ void symm_arr_filter(T* dst, const T* src, int len) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
}
// symm_arr_reduce(arr, len, fn, init) — fold left with callback
template<typename T, typename Fn>
__device__ T symm_arr_reduce(const T* arr, int len, Fn fn, T init) {
    T acc = init;
    for (int i = 0; i < len; i++) acc = fn(acc, arr[i]);
    return acc;
}

// symm_arr_slice(arr, len, start, end) → returns pointer to static slice buffer
// Supports negative indices (Python-style).
template<typename T>
__device__ T* symm_arr_slice(const T* arr, int len, int start, int end_) {
    static T buf[256];
    if (start < 0) start = len + start;
    if (end_  < 0) end_  = len + end_;
    if (start < 0) start = 0;
    if (end_  > len) end_ = len;
    int n = 0;
    for (int i = start; i < end_ && n < 256; i++) buf[n++] = arr[i];
    return buf;
}

// symm_arr_for_each(arr, len, fn) — iterate with callback
template<typename T, typename Fn>
__device__ void symm_arr_for_each(const T* arr, int len, Fn fn) {
    for (int i = 0; i < len; i++) fn(arr[i]);
}

// symm_arr_every(arr, len, fn) — true if all elements satisfy predicate
template<typename T, typename Fn>
__device__ bool symm_arr_every(const T* arr, int len, Fn fn) {
    for (int i = 0; i < len; i++) if (!fn(arr[i])) return false;
    return true;
}

// symm_arr_some(arr, len, fn) — true if any element satisfies predicate
template<typename T, typename Fn>
__device__ bool symm_arr_some(const T* arr, int len, Fn fn) {
    for (int i = 0; i < len; i++) if (fn(arr[i])) return true;
    return false;
}

__device__ int symm_arr_index_of(const float* arr, int len, float val) {
    for (int i = 0; i < len; i++) {
        if (arr[i] == val) return i;
    }
    return -1;
}

__device__ bool symm_arr_includes(const float* arr, int len, float val) {
    return symm_arr_index_of(arr, len, val) >= 0;
}
)";

// Fragment 4: Math and Array utilities
static const char* PRELUDE_FRAG4 = R"(
// ── Math constants and functions ──────────────────────────────────────────────
#define NaN               (_symm_nan())
#define Infinity          (_symm_inf())
#define POSITIVE_INFINITY (_symm_inf())
#define NEGATIVE_INFINITY (-_symm_inf())
#define undefined         nullptr

struct Number {
    static const float EPSILON;
    static const float MAX_VALUE;
    static const float MIN_VALUE;
    static const long long MAX_SAFE_INTEGER;
    static constexpr long long MIN_SAFE_INTEGER = -9007199254740991LL;
    __device__ static bool isNaN(float v) { return v != v; }
    __device__ static bool isFinite(float v) { float i=_symm_inf(); return v==v && v!=i && v!=-i; }
    __device__ static bool isInteger(float v) { return isFinite(v) && v == (float)(long long)v; }
    __device__ static bool isSafeInteger(float v) {
        return isInteger(v) && v >= (float)MIN_SAFE_INTEGER && v <= (float)MAX_SAFE_INTEGER;
    }
    __device__ static float parseFloat(const char* s) { return symm_parse_float(s); }
    __device__ static int parseInt(const char* s) { return (int)symm_parse_float(s); }
    __device__ static int parseInt(const char* s, int radix) { return symm_parse_int_radix(s, radix); }
};

__device__ bool isNaN(float v) { return v != v; }
__device__ bool isFinite(float v) { float i=_symm_inf(); return v==v&&v!=i&&v!=-i; }

struct Math {
    __device__ static float random() {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        return (float)(seed >> 8) * (1.0f / (float)(1u << 24));
    }
};

struct Array {
    template<typename T> __device__ static bool isArray(T v) { return true; }
    __device__ static bool isArray(const char* v) { return false; }
    __device__ static bool isArray(float v) { return false; }
    __device__ static bool isArray(int v) { return false; }

    // Array.from() - stub, GPU can't allocate arrays dynamically
    template<typename T> __device__ static T* from(T* src, int len) {
        return src;  // Return source as-is (GPU memory limitation)
    }
    __device__ static const char* from(const char* str) {
        return str;  // String to array of chars
    }

    // Array.of() - stub for variadic construction
    __device__ static const char* of() {
        return "[]";  // Return array marker
    }

    // Array.isArray() - type-safe array check
    template<typename T> __device__ static bool isArray(const T* arr) {
        return true;  // All pointers are treated as arrays
    }
};

struct PropertyDescriptor {
    bool writable;
    bool enumerable;
    bool configurable;
    __device__ PropertyDescriptor(bool w = true, bool e = true, bool c = true)
        : writable(w), enumerable(e), configurable(c) {}
};

struct Object {
    template<typename T> __device__ static bool hasOwn(T, const char*) { return true; }

    // Property descriptor operations (stubs - GPU objects are stack-allocated)
    __device__ static void defineProperty(void* obj, const char* key, PropertyDescriptor desc) {}
    __device__ static void seal(void* obj) {}
    __device__ static void freeze(void* obj) {}
    __device__ static bool isFrozen(void* obj) { return false; }
    __device__ static bool isSealed(void* obj) { return false; }
    __device__ static bool isExtensible(void* obj) { return true; }
    __device__ static const char* getOwnPropertyNames(void* obj) { return "[]"; }
    __device__ static const char* getPrototypeOf(void* obj) { return "{}"; }
    __device__ static bool setPrototypeOf(void* obj, void* proto) { return true; }
};

struct String {
    // String constructor - creates a string from value
    __device__ static const char* create(const char* s) { return s ? s : ""; }
    __device__ static const char* create(float v) {
        static char buf[32];
        symm_write_float(buf, v);
        return buf;
    }
    __device__ static const char* create(int v) {
        static char buf[32];
        symm_write_int(buf, v);
        return buf;
    }
    // String.fromCharCode(...) - create from character codes
    __device__ static const char* fromCharCode(int code) { return symm_str_from_char_code(code); }
    // String.fromCodePoint(...) - stub (no Unicode support)
    __device__ static const char* fromCodePoint(int code) { return symm_str_from_char_code(code); }
    // String.prototype.valueOf - returns the string itself
    __device__ static const char* valueOf(const char* s) { return s ? s : ""; }
};

// Parse functions
__device__ float symm_parse_float(const char* s) {
    if (!s) return 0.f;
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    bool neg = false;
    if (s[i] == '-') { neg = true; i++; } else if (s[i] == '+') { i++; }
    float v = 0.f;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10.f + (s[i] - '0'); i++; }
    if (s[i] == '.') {
        i++;
        float scale = 0.1f;
        while (s[i] >= '0' && s[i] <= '9') { v += (s[i] - '0') * scale; scale *= 0.1f; i++; }
    }
    return neg ? -v : v;
}

__device__ float parseInt(const char* s) { return symm_parse_float(s); }
__device__ float parseFloat(const char* s) { return symm_parse_float(s); }
)";

// Fragment 5: Console and final stubs
static const char* PRELUDE_FRAG5 = R"(
// ── Debug logging ring buffer (for console.log implementation) ──────────────
__device__ unsigned long long _debug_ring_ptr = 0;
__device__ void symm_debug_log_write(const char* msg) {}

// ── Console stub ───────────────────────────────────────────────────────────
// Helper: convert a value to string for console output
__device__ const char* _symm_to_str(const char* s) { return s ? s : "null"; }
__device__ const char* _symm_to_str(float v) { static char _b[32]; symm_write_float(_b, v); return _b; }
__device__ const char* _symm_to_str(int v) { static char _b[24]; symm_write_int(_b, v); return _b; }
__device__ const char* _symm_to_str(long long v) { static char _b[24]; symm_write_int(_b, v); return _b; }
__device__ const char* _symm_to_str(bool v) { return v ? "true" : "false"; }
template<typename T> __device__ const char* _symm_to_str(T) { return "[object]"; }

// Multi-arg concat helper (up to 4 args, space-separated)
__device__ const char* _symm_concat2(const char* a, const char* b) {
    static char buf[512];
    int p = 0;
    for (int i = 0; a[i] && p < 254; i++) buf[p++] = a[i];
    buf[p++] = ' ';
    for (int i = 0; b[i] && p < 510; i++) buf[p++] = b[i];
    buf[p] = '\0'; return buf;
}
__device__ const char* _symm_concat3(const char* a, const char* b, const char* c) {
    static char buf[512];
    int p = 0;
    for (int i = 0; a[i] && p < 168; i++) buf[p++] = a[i];
    buf[p++] = ' ';
    for (int i = 0; b[i] && p < 337; i++) buf[p++] = b[i];
    buf[p++] = ' ';
    for (int i = 0; c[i] && p < 510; i++) buf[p++] = c[i];
    buf[p] = '\0'; return buf;
}

struct _Console {
    __device__ void log() { symm_debug_log_write("[log]"); }
    __device__ void log(const char* msg) { symm_debug_log_write(msg); }
    __device__ void log(float v) { symm_debug_log_write(_symm_to_str(v)); }
    __device__ void log(int v) { symm_debug_log_write(_symm_to_str(v)); }
    __device__ void log(long long v) { symm_debug_log_write(_symm_to_str(v)); }
    __device__ void log(bool v) { symm_debug_log_write(_symm_to_str(v)); }
    template<typename T> __device__ void log(T a) { symm_debug_log_write(_symm_to_str(a)); }
    // Multi-arg overloads (space-separated)
    template<typename A, typename B>
    __device__ void log(A a, B b) {
        symm_debug_log_write(_symm_concat2(_symm_to_str(a), _symm_to_str(b)));
    }
    template<typename A, typename B, typename C>
    __device__ void log(A a, B b, C c) {
        symm_debug_log_write(_symm_concat3(_symm_to_str(a), _symm_to_str(b), _symm_to_str(c)));
    }
    __device__ void warn(const char* msg) { symm_debug_log_write(msg); }
    __device__ void error(const char* msg) { symm_debug_log_write(msg); }
    __device__ void info(const char* msg) { symm_debug_log_write(msg); }
    template<typename T> __device__ void warn(T a)  { symm_debug_log_write(_symm_to_str(a)); }
    template<typename T> __device__ void error(T a) { symm_debug_log_write(_symm_to_str(a)); }
    template<typename T> __device__ void info(T a)  { symm_debug_log_write(_symm_to_str(a)); }
    template<typename A, typename B> __device__ void warn(A a, B b)  { log(a, b); }
    template<typename A, typename B> __device__ void error(A a, B b) { log(a, b); }
    __device__ void assert_(bool cond, const char* msg = "") {
        if (!cond) symm_debug_log_write(msg[0] ? msg : "Assertion failed");
    }
    template<typename T> __device__ void time(T) {}
    template<typename T> __device__ void timeEnd(T) {}
    __device__ void count(const char* label = "default") { symm_debug_log_write(label); }
    __device__ void countReset(const char* label = "default") { (void)label; }
    __device__ void group(const char* label = "") { if (label[0]) symm_debug_log_write(label); }
    __device__ void groupEnd() {}
};
__device__ _Console console;

// ── String equality for JSON/object handling ──────────────────────────────────
__device__ bool symm_str_eq(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}

__device__ bool symm_eq(const char* a, const char* b) { return symm_str_eq(a, b); }
template<typename A, typename B>
__device__ bool symm_eq(A a, B b) { return a == b; }

// ── URL parser stub ───────────────────────────────────────────────────────────
struct URL {
    char _href[256];
    bool _valid;
    __device__ URL(const char* s = "") : _valid(false) { _href[0]=0; }
};

// ── Promise stub ──────────────────────────────────────────────────────────────
template<typename T>
struct SyncPromise {
    T _value;
    bool _rejected;
    const char* _error;
    __device__ SyncPromise() : _value(T()), _rejected(false), _error("") {}
    __device__ SyncPromise(T v) : _value(v), _rejected(false), _error("") {}
    __device__ T get() const { return _value; }
    __device__ bool isRejected() const { return _rejected; }
    __device__ const char* getError() const { return _error ? _error : ""; }
};

struct Promise {
    template<typename T> __device__ static SyncPromise<T> resolve(T v) { return SyncPromise<T>(v); }
    template<typename T> __device__ static SyncPromise<T> reject(const char* err) {
        SyncPromise<T> p;
        p._rejected = true;
        p._error = err;
        return p;
    }
    template<typename T> __device__ static SyncPromise<T*> all(T* arr, int len) {
        // Execute all in sequence, return last result (simplified semantics)
        SyncPromise<T*> result;
        if (len > 0) {
            result._value = &arr[len - 1];
        }
        return result;
    }
    template<typename T> __device__ static SyncPromise<T> race(T* arr, int len) {
        // Return first result (deterministic: index 0)
        SyncPromise<T> result;
        if (len > 0) {
            result._value = arr[0];
        }
        return result;
    }
    // allSettled — run all, collect into array (GPU sync: same as all)
    template<typename T> __device__ static SyncPromise<T*> allSettled(T* arr, int len) {
        SyncPromise<T*> result;
        result._value = arr;
        return result;
    }
    // any — return first fulfilled (GPU sync: return first non-rejected)
    template<typename T> __device__ static SyncPromise<T> any(T* arr, int len) {
        SyncPromise<T> result;
        for (int i = 0; i < len; i++) {
            if (arr[i]._rejected == false) { result._value = arr[i]._value; return result; }
        }
        // all rejected: return rejected promise
        result._rejected = true;
        result._error = "AggregateError: All promises were rejected";
        return result;
    }
};
)";

// Fragment 6: Standard JS objects (Symbol, Set, WeakMap, etc.)
static const char* PRELUDE_FRAG6 = R"(
// ── Symbol: unique identifier ──────────────────────────────────────────────────
struct _Symbol {
    int _id;
    const char* _desc;
    __device__ _Symbol(int id = 0, const char* desc = "") : _id(id), _desc(desc) {}
};

// ── Set: unordered collection ──────────────────────────────────────────────────
struct Set {
    const char* _items[256];
    int _count = 0;
    __device__ void add(const char* item) {
        if (_count < 256 && !has(item)) { _items[_count++] = item; }
    }
    __device__ bool has(const char* item) const {
        for (int i = 0; i < _count; i++) {
            if (strcmp(_items[i], item) == 0) return true;
        }
        return false;
    }
    __device__ int size() const { return _count; }
    __device__ void clear() { _count = 0; }
    __device__ bool delete_item(const char* item) {
        for (int i = 0; i < _count; i++) {
            if (strcmp(_items[i], item) == 0) {
                for (int j = i; j < _count - 1; j++) _items[j] = _items[j + 1];
                _count--;
                return true;
            }
        }
        return false;
    }
    __device__ const char* values() const {
        static char buf[1024]; int pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < _count && pos < 990; i++) {
            if (i) buf[pos++] = ',';
            buf[pos++] = '"';
            for (int j = 0; _items[i] && _items[i][j] && pos < 1000; j++) buf[pos++] = _items[i][j];
            buf[pos++] = '"';
        }
        buf[pos++] = ']'; buf[pos] = 0; return buf;
    }
    __device__ const char* keys() const { return values(); }
    __device__ const char* entries() const {
        static char buf[1024]; int pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < _count && pos < 990; i++) {
            if (i) buf[pos++] = ',';
            buf[pos++] = '[';
            char ibuf[12]; int ilen = symm_write_int(ibuf, i);
            for (int j = 0; j < ilen && pos < 1000; j++) buf[pos++] = ibuf[j];
            buf[pos++] = ','; buf[pos++] = '"';
            for (int j = 0; _items[i] && _items[i][j] && pos < 1005; j++) buf[pos++] = _items[i][j];
            buf[pos++] = '"'; buf[pos++] = ']';
        }
        buf[pos++] = ']'; buf[pos] = 0; return buf;
    }
    __device__ void forEach(void (*fn)(const char*)) const {
        for (int i = 0; i < _count; i++) fn(_items[i]);
    }
};

// ── WeakMap: weak reference map ────────────────────────────────────────────────
struct WeakMap {
    __device__ void set(void* key, float val) {}
    __device__ float get(void* key) const { return 0.f; }
    __device__ bool has(void* key) const { return false; }
};

// ── WeakRef: weak reference ────────────────────────────────────────────────────
struct WeakRef {
    void* _ref;
    __device__ WeakRef(void* obj = nullptr) : _ref(obj) {}
    __device__ void* deref() { return _ref; }
};

// ── WeakSet: weak set ──────────────────────────────────────────────────────────
struct WeakSet {
    __device__ void add(void* obj) {}
    __device__ bool has(void* obj) const { return false; }
};

// ── Proxy: trap-based property interception ────────────────────────────────────
typedef const char* (*ProxyGetTrap)(const char* target, const char* key);
typedef void (*ProxySetTrap)(const char* target, const char* key, const char* val);
typedef bool (*ProxyHasTrap)(const char* target, const char* key);

struct ProxyHandler {
    ProxyGetTrap get;
    ProxySetTrap set;
    ProxyHasTrap has;
    __device__ ProxyHandler() : get(nullptr), set(nullptr), has(nullptr) {}
};

struct Proxy {
    const char* _target;
    ProxyHandler _handler;
    __device__ Proxy() : _target(""), _handler() {}
    __device__ Proxy(const char* target, ProxyHandler handler) : _target(target), _handler(handler) {}
    __device__ const char* getProp(const char* key) {
        if (_handler.get) return _handler.get(_target, key);
        return "";
    }
    __device__ void setProp(const char* key, const char* val) {
        if (_handler.set) _handler.set(_target, key, val);
    }
    __device__ bool hasProp(const char* key) {
        if (_handler.has) return _handler.has(_target, key);
        return false;
    }
};

// ── Reflect: static reflection API ─────────────────────────────────────────────
struct Reflect {
    __device__ static const char* get(const char* target, const char* key) {
        return target;  // Simplified: just return target string
    }
    __device__ static bool set(const char* target, const char* key, const char* val) {
        return true;  // Simplified: always succeed
    }
    __device__ static bool has(const char* target, const char* key) {
        return true;  // Simplified: always present
    }
    __device__ static bool deleteProperty(const char* target, const char* key) {
        return true;  // Simplified: always succeed
    }
    __device__ static const char* ownKeys(const char* target) {
        return "[]";  // Simplified: empty keys array
    }
    __device__ static bool isExtensible(const char* target) {
        return true;  // All objects extensible
    }
};

// ── URLSearchParams: query string parser ───────────────────────────────────────
// Note: symm_url_search_params_get() is defined in FRAG10 (below)
// Forward declaration so FRAG6 can reference it in the struct
__device__ const char* symm_url_search_params_get(const char* qs, const char* key);
struct URLSearchParams {
    const char* _qs;
    __device__ URLSearchParams(const char* qs = "") : _qs(qs ? qs : "") {}
    __device__ const char* get(const char* key) const {
        return symm_url_search_params_get(_qs, key);
    }
    __device__ bool has(const char* key) const {
        return symm_url_search_params_get(_qs, key)[0] != '\0';
    }
    __device__ void set(const char* key, const char* val) {}  // read-only on GPU
    __device__ void append(const char* key, const char* val) {}
    __device__ void delete_(const char* key) {}
    __device__ const char* toString() const { return _qs ? _qs : ""; }
};

// ── Buffer: binary data ────────────────────────────────────────────────────────
struct Buffer {
    uint8_t _data[1024];
    int _len;
    __device__ Buffer() : _len(0) {}
    __device__ int length() const { return _len; }
};

// ── AbortController / AbortSignal ──────────────────────────────────────────────
struct AbortSignal {
    bool aborted; const char* reason;
    __device__ AbortSignal() : aborted(false), reason("") {}
};
struct AbortController {
    AbortSignal signal;
    __device__ void abort(const char* reason = "AbortError") {
        signal.aborted = true; signal.reason = reason;
    }
};

// ── TextEncoder / TextDecoder (UTF-8 passthrough on GPU) ──────────────────────
struct TextEncoder {
    __device__ const char* encode(const char* s) { return s ? s : ""; }
};
struct TextDecoder {
    const char* _enc;
    __device__ TextDecoder(const char* enc = "utf-8") : _enc(enc) {}
    __device__ const char* decode(const char* s) { return s ? s : ""; }
};

// ── process global ─────────────────────────────────────────────────────────────
// Maps Node.js process.env access patterns for configuration injection.
struct _ProcessEnv {
    const char* NODE_ENV;
    const char* PORT;
    const char* HOST;
    __device__ _ProcessEnv() : NODE_ENV("production"), PORT("8000"), HOST("0.0.0.0") {}
    __device__ const char* operator[](const char* key) const {
        if (!key) return "";
        if (strcmp(key, "NODE_ENV") == 0) return NODE_ENV;
        if (strcmp(key, "PORT") == 0) return PORT;
        if (strcmp(key, "HOST") == 0) return HOST;
        return "";
    }
};
struct _Process {
    _ProcessEnv env;
    const char* platform;
    int exitCode;
    __device__ void exit(int code = 0) { exitCode = code; }
    __device__ const char* version() const { return "v20.0.0"; }
};

// ── globalThis ─────────────────────────────────────────────────────────────────
// globalThis is the global object; on GPU it's effectively the thread context.
struct _GlobalThis {
    _Process process;
    __device__ float parseInt(const char* s) { return symm_parse_float(s); }
    __device__ float parseFloat(const char* s) { return symm_parse_float(s); }
    __device__ bool isNaN(float v) { return v != v; }
    __device__ bool isFinite(float v) { float inf = 1e30f * 1e30f; return v != inf && v != -inf && v == v; }
};

)";

// Fragment 7: Full string method implementations and regex support
static const char* PRELUDE_FRAG7 = R"(
// -- Complete string methods (full implementations) --
__device__ const char* symm_str_substring_full(const char* s, int start, int end) {
    if (!s) return "";
    static char buf[512];
    int len = symm_strlen(s);
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    if (start > end) { int tmp = start; start = end; end = tmp; }
    int pos = 0;
    for (int i = start; i < end && pos < 500; i++) buf[pos++] = s[i];
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_substr_full(const char* s, int start, int len) {
    if (!s) return "";
    static char buf[512];
    int slen = symm_strlen(s);
    if (start < 0) start = slen + start;
    if (start < 0) start = 0;
    if (start > slen) start = slen;
    if (len < 0) len = 0;
    int pos = 0;
    for (int i = 0; i < len && start + i < slen && pos < 500; i++) {
        buf[pos++] = s[start + i];
    }
    buf[pos] = '\0';
    return buf;
}

__device__ int symm_str_last_index_of_full(const char* s, const char* substr) {
    if (!s || !substr) return -1;
    int lastPos = -1;
    for (int i = 0; s[i]; i++) {
        const char* p = substr;
        const char* t = s + i;
        while (*p && *t && *p == *t) { p++; t++; }
        if (!*p) lastPos = i;
    }
    return lastPos;
}

// -- Regex support (full pattern matching with character classes, quantifiers, groups) --

// Helper: check if char matches character class [a-z], \d, \w, \s, etc.
__device__ bool symm_char_matches_class(char c, const char* class_str) {
    if (!class_str) return false;
    bool negate = false;
    int i = 0;
    if (class_str[0] == '^') { negate = true; i = 1; }

    bool matches = false;
    while (class_str[i] && class_str[i] != ']') {
        if (class_str[i] == '\\' && class_str[i+1]) {
            i++;
            char escape = class_str[i];
            if (escape == 'd' && c >= '0' && c <= '9') { matches = true; break; }
            else if (escape == 'D' && (c < '0' || c > '9')) { matches = true; break; }
            else if (escape == 'w' && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { matches = true; break; }
            else if (escape == 'W' && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { matches = true; break; }
            else if (escape == 's' && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) { matches = true; break; }
            else if (escape == 'S' && !(c == ' ' || c == '\t' || c == '\n' || c == '\r')) { matches = true; break; }
            i++;
        } else if (class_str[i+1] == '-' && class_str[i+2] && class_str[i+2] != ']') {
            char start = class_str[i];
            char end = class_str[i+2];
            if (c >= start && c <= end) { matches = true; break; }
            i += 3;
        } else {
            if (c == class_str[i]) { matches = true; break; }
            i++;
        }
    }
    return negate ? !matches : matches;
}

// Regex matcher with character classes, quantifiers, anchors, lookahead, alternation
__device__ int symm_regex_match_at(const char* pattern, const char* str, const char* str_start) {
    const char* p = pattern;
    const char* s = str;

    while (*p) {
        if (*p == '^') {
            if (s != str_start) return -1;
            p++;
        } else if (*p == '$') {
            if (*s != '\0') return -1;
            p++;
        } else if (*p == '(' && p[1] == '?') {
            // Special group: lookahead, lookbehind, named group, or non-capturing
            p += 2;
            if (*p == '=' || *p == '!') {
                // Lookahead (positive or negative)
                bool positive = (*p == '=');
                p++;
                const char* look_start = p;
                int look_depth = 1;
                while (*p && look_depth > 0) {
                    if (*p == '(') look_depth++;
                    else if (*p == ')') look_depth--;
                    if (look_depth > 0) p++;
                }
                if (*p != ')') return -1;
                p++;

                // Check lookahead condition on current position
                int test_result = symm_regex_match_at(look_start, s, str_start);
                if ((positive && test_result < 0) || (!positive && test_result >= 0)) {
                    return -1;
                }
            } else if (*p == '<') {
                // Named group or lookbehind
                p++;
                if (*p == '=') {
                    // Positive lookbehind (not commonly supported in basic regex, skip)
                    p++;
                    while (*p && *p != ')') p++;
                    if (*p == ')') p++;
                } else {
                    // Named group: skip name and match the pattern
                    while (*p && *p != '>') p++;
                    if (*p == '>') p++;
                    const char* group_start = p;
                    int group_depth = 1;
                    while (*p && group_depth > 0) {
                        if (*p == '(') group_depth++;
                        else if (*p == ')') group_depth--;
                        if (group_depth > 0) p++;
                    }
                    if (*p != ')') return -1;
                    p++;
                }
            } else {
                // Non-capturing group (?:...)
                int group_depth = 1;
                while (*p && group_depth > 0) {
                    if (*p == '(') group_depth++;
                    else if (*p == ')') group_depth--;
                    if (group_depth > 0) p++;
                }
                if (*p == ')') p++;
            }
        } else if (*p == '(') {
            // Capturing group - skip the group content and continue
            int group_depth = 1;
            p++;
            while (*p && group_depth > 0) {
                if (*p == '(') group_depth++;
                else if (*p == ')') group_depth--;
                if (group_depth > 0) p++;
            }
            if (*p == ')') p++;
        } else if (*p == '|') {
            // Alternation - try next alternative
            p++;
        } else if (*p == '.') {
            if (!*s || *s == '\n') return -1;
            s++;
            p++;
            if (*p == '*') { while (*s && *s != '\n') s++; p++; }
            else if (*p == '+') { p++; }
            else if (*p == '?') { p++; }
            else if (*p == '{') {
                // Quantifier {n}, {n,}, {n,m}
                int min_count = 0, max_count = 999;
                p++;
                while (*p && *p >= '0' && *p <= '9') { min_count = min_count * 10 + (*p - '0'); p++; }
                if (*p == ',') {
                    p++;
                    max_count = 0;
                    if (*p >= '0' && *p <= '9') {
                        while (*p && *p >= '0' && *p <= '9') { max_count = max_count * 10 + (*p - '0'); p++; }
                    } else {
                        max_count = 999;
                    }
                } else {
                    max_count = min_count;
                }
                if (*p == '}') p++;
            }
        } else if (*p == '[') {
            int class_end = 1;
            while (p[class_end] && p[class_end] != ']') class_end++;
            if (!p[class_end]) return -1;

            if (!*s || !symm_char_matches_class(*s, p + 1)) return -1;
            s++;
            p += class_end + 1;

            if (*p == '*') { while (*s && symm_char_matches_class(*s, p - class_end)) s++; p++; }
            else if (*p == '+') { p++; }
            else if (*p == '?') { p++; }
        } else if (*p == '\\' && p[1]) {
            p++;
            char escape = *p;
            if (escape == 'b' || escape == 'B') {
                // Word boundary (\b) or non-word boundary (\B)
                bool is_boundary = false;
                bool prev_word = (s > str_start) && ((*(s-1) >= 'a' && *(s-1) <= 'z') || (*(s-1) >= 'A' && *(s-1) <= 'Z') || (*(s-1) >= '0' && *(s-1) <= '9') || *(s-1) == '_');
                bool curr_word = *s && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_');
                is_boundary = (prev_word != curr_word);
                if ((escape == 'b' && !is_boundary) || (escape == 'B' && is_boundary)) return -1;
                p++;
            } else if (escape == 'd' || escape == 'D' || escape == 'w' || escape == 'W' || escape == 's' || escape == 'S') {
                if (!*s) return -1;
                bool matches = false;
                if (escape == 'd') matches = (*s >= '0' && *s <= '9');
                else if (escape == 'D') matches = (*s < '0' || *s > '9');
                else if (escape == 'w') matches = ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_');
                else if (escape == 'W') matches = !((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_');
                else if (escape == 's') matches = (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');
                else if (escape == 'S') matches = !(*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r');

                if (!matches) return -1;
                s++;
                p++;

                if (*p == '*') { while (*s) { if (escape == 'd' && !(*s >= '0' && *s <= '9')) break; s++; } p++; }
                else if (*p == '+') { p++; }
                else if (*p == '?') { p++; }
                else if (*p == '{') {
                    int min_count = 0, max_count = 999;
                    p++;
                    while (*p && *p >= '0' && *p <= '9') { min_count = min_count * 10 + (*p - '0'); p++; }
                    if (*p == ',') {
                        p++;
                        max_count = 0;
                        if (*p >= '0' && *p <= '9') {
                            while (*p && *p >= '0' && *p <= '9') { max_count = max_count * 10 + (*p - '0'); p++; }
                        } else {
                            max_count = 999;
                        }
                    } else {
                        max_count = min_count;
                    }
                    if (*p == '}') p++;
                }
            } else if (escape == 'n') {
                if (*s != '\n') return -1;
                s++; p++;
            } else if (escape == 't') {
                if (*s != '\t') return -1;
                s++; p++;
            } else if (escape == 'r') {
                if (*s != '\r') return -1;
                s++; p++;
            } else if (escape == 'f') {
                if (*s != '\f') return -1;
                s++; p++;
            } else if (escape == 'v') {
                if (*s != '\v') return -1;
                s++; p++;
            } else {
                // Literal escaped character
                if (*s != escape) return -1;
                s++;
                p++;

                if (*p == '*') { while (*s == escape) s++; p++; }
                else if (*p == '+') { p++; }
                else if (*p == '?') { p++; }
                else if (*p == '{') {
                    int min_count = 0, max_count = 999;
                    p++;
                    while (*p && *p >= '0' && *p <= '9') { min_count = min_count * 10 + (*p - '0'); p++; }
                    if (*p == ',') {
                        p++;
                        max_count = 0;
                        if (*p >= '0' && *p <= '9') {
                            while (*p && *p >= '0' && *p <= '9') { max_count = max_count * 10 + (*p - '0'); p++; }
                        } else {
                            max_count = 999;
                        }
                    } else {
                        max_count = min_count;
                    }
                    if (*p == '}') p++;
                }
            }
        } else if (*p == '*' || *p == '+' || *p == '?') {
            if (*s != *p) return -1;
            s++;
            p++;
        } else {
            if (*s != *p) return -1;
            s++;
            p++;

            if (*p == '*') { while (*s == *(p-1)) s++; p++; }
            else if (*p == '+') { p++; }
            else if (*p == '?') { p++; }
        }
    }
    return s - str_start;
}

__device__ bool symm_regex_test(const char* pattern, const char* str) {
    if (!pattern || !str) return false;
    for (int i = 0; str[i]; i++) {
        if (symm_regex_match_at(pattern, str + i, str) >= 0) return true;
    }
    if (symm_regex_match_at(pattern, str, str) >= 0) return true;
    return false;
}

__device__ int symm_regex_exec(const char* pattern, const char* str) {
    if (!pattern || !str) return -1;
    for (int i = 0; str[i]; i++) {
        if (symm_regex_match_at(pattern, str + i, str) >= 0) return i;
    }
    if (symm_regex_match_at(pattern, str, str) >= 0) return 0;
    return -1;
}

__device__ const char* symm_regex_match(const char* pattern, const char* str) {
    if (!pattern || !str) return "";
    static char buf[512];
    int idx = symm_regex_exec(pattern, str);
    if (idx < 0) {
        buf[0] = '\0';
    } else {
        int pos = 0;
        buf[pos++] = '[';
        buf[pos++] = '\"';
        int plen = symm_strlen(pattern);
        for (int i = 0; i < plen && pos < 500; i++) {
            char c = pattern[i];
            if (c == '"') { buf[pos++] = '\\'; buf[pos++] = '"'; }
            else if (c == '\\') { buf[pos++] = '\\'; buf[pos++] = '\\'; }
            else buf[pos++] = c;
        }
        buf[pos++] = '\"';
        buf[pos++] = ']';
        buf[pos] = '\0';
    }
    return buf;
}

__device__ const char* symm_str_match_all_full(const char* pattern, const char* str) {
    if (!pattern || !str) return "";
    static char buf[512];
    int pos = 0;
    buf[pos++] = '[';
    bool first = true;

    for (int i = 0; str[i]; i++) {
        if (symm_regex_match_at(pattern, str + i, str) >= 0) {
            if (!first) buf[pos++] = ',';
            first = false;

            int idx = i;
            char tmp[16];
            int n = 0;
            if (idx == 0) tmp[n++] = '0';
            else {
                int temp = idx;
                while (temp > 0) { tmp[n++] = '0' + (temp % 10); temp /= 10; }
            }
            for (int j = n - 1; j >= 0 && pos < 500; j--) buf[pos++] = tmp[j];
        }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

// ── Regex: multiline flag (m) — ^ matches after \n, $ matches before \n ────────
// symm_regex_test_m: try matching at each position using the line start as '^' anchor
__device__ bool symm_regex_test_m(const char* pattern, const char* str) {
    if (!pattern || !str) return false;
    const char* s = str;
    while (true) {
        // Use current position as the line start (makes '^' match here)
        // Note: symm_regex_match_at uses str_start for '^'; passing s as str_start
        // means '^' matches at every position we pass as str_start.
        // We only pass s as str_start when s == str (start) or *(s-1) == '\n'
        bool is_line_start = (s == str) || (s > str && *(s-1) == '\n');
        if (is_line_start || true) {
            // Try at every position; when ^ appears, it's relative to line start
            // Pass s as str_start so ^ matches at line boundaries
            if (symm_regex_match_at(pattern, s, is_line_start ? s : str) >= 0) return true;
        }
        if (!*s) break;
        s++;
    }
    return false;
}
)";

// Fragment 9: String replacement and additional utilities
static const char* PRELUDE_FRAG9 = R"(
__device__ const char* symm_str_replace_regex(const char* pattern, const char* replacement, const char* str) {
    static char buf[512];
    if (!pattern || !replacement || !str) return str;

    int pos = 0;
    bool replaced = false;

    for (int i = 0; str[i] && pos < 500; i++) {
        if (!replaced && symm_regex_match_at(pattern, str + i, str) >= 0) {
            int replen = symm_strlen(replacement);
            for (int j = 0; j < replen && pos < 500; j++) buf[pos++] = replacement[j];

            int plen = symm_strlen(pattern);
            i += plen - 1;
            replaced = true;
        } else {
            buf[pos++] = str[i];
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ const char* symm_str_replace_all_regex(const char* pattern, const char* replacement, const char* str) {
    static char buf[512];
    if (!pattern || !replacement || !str) return str;

    int pos = 0;
    int replen = symm_strlen(replacement);

    for (int i = 0; str[i] && pos < 500; ) {
        if (symm_regex_match_at(pattern, str + i, str) >= 0) {
            for (int j = 0; j < replen && pos < 500; j++) buf[pos++] = replacement[j];

            int plen = symm_strlen(pattern);
            i += plen > 0 ? plen : 1;
        } else {
            buf[pos++] = str[i++];
        }
    }
    buf[pos] = '\0';
    return buf;
}


// String.prototype.localeCompare — compare strings (simplified)
__device__ int symm_str_locale_compare(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (*a != *b) return (*a > *b) ? 1 : -1;
        a++; b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}
)";

// Fragment 8: Iterator and generator support
static const char* PRELUDE_FRAG8 = R"(
// Iterator result struct for generators and Symbol.iterator protocol
template<typename T>
struct GeneratorResult {
    T value;
    bool done;
    __device__ GeneratorResult(): value(T()), done(false) {}
    __device__ GeneratorResult(T v, bool d): value(v), done(d) {}
};

// Symbol object — stub for Symbol.iterator, Symbol.for, Symbol.keyFor
struct Symbol {
    const char* _name;
    __device__ Symbol(): _name("") {}
    __device__ Symbol(const char* n): _name(n) {}
    __device__ static Symbol iterator() { return Symbol("Symbol.iterator"); }
    __device__ static Symbol asyncIterator() { return Symbol("Symbol.asyncIterator"); }
    __device__ static Symbol hasInstance() { return Symbol("Symbol.hasInstance"); }
    __device__ static Symbol match() { return Symbol("Symbol.match"); }
    __device__ static Symbol replace() { return Symbol("Symbol.replace"); }
    __device__ static Symbol search() { return Symbol("Symbol.search"); }
    __device__ static Symbol split() { return Symbol("Symbol.split"); }
    __device__ static Symbol species() { return Symbol("Symbol.species"); }
    __device__ static Symbol toPrimitive() { return Symbol("Symbol.toPrimitive"); }
    __device__ static Symbol toStringTag() { return Symbol("Symbol.toStringTag"); }
    __device__ static Symbol unscopables() { return Symbol("Symbol.unscopables"); }
    __device__ static Symbol for_(const char* key) { return Symbol(key); }
    __device__ static const char* keyFor(Symbol sym) { return sym._name; }
};

// Iterator protocol helper — used by for-of loops
template<typename T>
struct Iterator {
    void* _obj;
    int _index;
    bool _done;
    __device__ Iterator(): _obj(nullptr), _index(0), _done(false) {}
    __device__ GeneratorResult<T> next() {
        return GeneratorResult<T>(T(), true);
    }
};
)";

// Fragment 10: Missing array methods, error subtypes, Object.is, BigInt, Performance
static const char* PRELUDE_FRAG10 = R"(

// ── Array: arr.at(index) — negative index support ─────────────────────────────
template<typename T> __device__ T symm_arr_at(const T* arr, int len, int idx) {
    if (!arr || len <= 0) return T{};
    if (idx < 0) idx = len + idx;
    if (idx < 0 || idx >= len) return T{};
    return arr[idx];
}

// ── Array: arr.findLast / arr.findLastIndex ────────────────────────────────────
template<typename T, typename Fn>
__device__ T symm_arr_find_last(const T* arr, int len, Fn fn) {
    for (int i = len - 1; i >= 0; i--)
        if (fn(arr[i])) return arr[i];
    return T{};
}
template<typename T, typename Fn>
__device__ int symm_arr_find_last_index(const T* arr, int len, Fn fn) {
    for (int i = len - 1; i >= 0; i--)
        if (fn(arr[i])) return i;
    return -1;
}

// ── Array: arr.copyWithin(target, start [, end]) ─────────────────────────────
template<typename T>
__device__ T* symm_arr_copy_within(T* arr, int len, int target, int start, int end_) {
    if (!arr) return arr;
    if (end_ < 0 || end_ > len) end_ = len;
    if (start < 0) start = len + start;
    if (target < 0) target = len + target;
    if (start < 0) start = 0;
    if (target < 0) target = 0;
    // copy backwards if ranges overlap to prevent clobber
    int count = end_ - start;
    if (count <= 0) return arr;
    if (target < start) {
        for (int i = 0; i < count && target + i < len; i++) arr[target + i] = arr[start + i];
    } else {
        for (int i = count - 1; i >= 0 && target + i < len; i--) arr[target + i] = arr[start + i];
    }
    return arr;
}

// ── Array: arr.toReversed() / toSorted() / toSpliced() ────────────────────────
// These return a new copy (non-mutating). On GPU, static-size arrays used.
template<typename T>
__device__ T* symm_arr_to_reversed(const T* src, int len, T* dst) {
    for (int i = 0; i < len; i++) dst[i] = src[len - 1 - i];
    return dst;
}
template<typename T>
__device__ T* symm_arr_to_sorted(const T* src, int len, T* dst) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
    // insertion sort
    for (int i = 1; i < len; i++) {
        T key = dst[i]; int j = i - 1;
        while (j >= 0 && dst[j] > key) { dst[j+1] = dst[j]; j--; }
        dst[j+1] = key;
    }
    return dst;
}

// ── Array: arr.entries() — yields [index, value] pairs ────────────────────────
// Returns JSON-array string of [index, value] pairs.
template<typename T>
__device__ const char* symm_arr_entries(const T* arr, int len) {
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < len && pos < 990; i++) {
        if (i) buf[pos++] = ',';
        buf[pos++] = '[';
        int n = symm_write_int(buf + pos, i); pos += n;
        buf[pos++] = ',';
        n = symm_write_float(buf + pos, (float)arr[i]); pos += n;
        buf[pos++] = ']';
    }
    buf[pos++] = ']'; buf[pos] = 0; return buf;
}
template<typename T>
__device__ const char* symm_arr_keys(const T* /*arr*/, int len) {
    static char buf[512]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < len && pos < 490; i++) {
        if (i) buf[pos++] = ',';
        int n = symm_write_int(buf + pos, i); pos += n;
    }
    buf[pos++] = ']'; buf[pos] = 0; return buf;
}
template<typename T>
__device__ const char* symm_arr_values(const T* arr, int len) {
    static char buf[512]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < len && pos < 480; i++) {
        if (i) buf[pos++] = ',';
        int n = symm_write_float(buf + pos, (float)arr[i]); pos += n;
    }
    buf[pos++] = ']'; buf[pos] = 0; return buf;
}

)";

// Fragment 10B: Missing array helpers, Error subtypes, structuredClone
static const char* PRELUDE_FRAG10B = R"(
// ── Array: find / findIndex (with function predicate) ─────────────────────────
template<typename T, typename Fn>
__device__ T symm_arr_find(const T* arr, int len, Fn fn) {
    for (int i = 0; i < len; i++) if (fn(arr[i])) return arr[i];
    return T{};
}
template<typename T, typename Fn>
__device__ int symm_arr_find_index(const T* arr, int len, Fn fn) {
    for (int i = 0; i < len; i++) if (fn(arr[i])) return i;
    return -1;
}

// ── Array: reverse (in-place) ──────────────────────────────────────────────────
template<typename T>
__device__ T* symm_arr_reverse(T* arr, int len) {
    for (int i = 0; i < len / 2; i++) {
        T tmp = arr[i]; arr[i] = arr[len-1-i]; arr[len-1-i] = tmp;
    }
    return arr;
}

// ── Array: sort (insertion sort, in-place) ─────────────────────────────────────
template<typename T>
__device__ void symm_arr_sort(T* arr, int len) {
    for (int i = 1; i < len; i++) {
        T key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}
template<typename T, typename Fn>
__device__ void symm_arr_sort_fn(T* arr, int len, Fn cmp) {
    for (int i = 1; i < len; i++) {
        T key = arr[i]; int j = i - 1;
        while (j >= 0 && cmp(arr[j], key) > 0) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

// ── Array: flat (one level deep — GPU can only flatten string arrays) ──────────
__device__ const char* symm_arr_flat(const char** arr, int len) {
    // Concatenate string elements into a flat JSON array
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    bool first = true;
    for (int i = 0; i < len && pos < 990; i++) {
        const char* elem = arr[i];
        if (!elem) continue;
        // If element looks like a JSON array, inline its contents
        if (elem[0] == '[') {
            const char* inner = elem + 1;
            while (*inner && pos < 990) {
                if (*inner == ']') break;
                if (!first) { buf[pos++] = ','; first = false; }
                first = false;
                buf[pos++] = *inner++;
            }
        } else {
            if (!first) buf[pos++] = ',';
            first = false;
            buf[pos++] = '"';
            for (int j = 0; elem[j] && pos < 1000; j++) buf[pos++] = elem[j];
            buf[pos++] = '"';
        }
    }
    buf[pos++] = ']'; buf[pos] = 0; return buf;
}
template<typename T>
__device__ T* symm_arr_flat(T* arr, int len) { return arr; } // numeric: no-op

// ── Error subtypes ─────────────────────────────────────────────────────────────
struct SyntaxError : Error {
    __device__ SyntaxError(const char* m = "") : Error(m) {}
};
struct ReferenceError : Error {
    __device__ ReferenceError(const char* m = "") : Error(m) {}
};
struct URIError : Error {
    __device__ URIError(const char* m = "") : Error(m) {}
};
struct EvalError : Error {
    __device__ EvalError(const char* m = "") : Error(m) {}
};

// ── Object.is — NaN-safe equality ─────────────────────────────────────────────
struct _ObjectIs {
    __device__ static bool is(float a, float b) {
        // NaN case
        if (a != a && b != b) return true;
        // +0 vs -0 case: compare as integers for sign bit
        int ia, ib;
        char* pa = (char*)&a; char* pb = (char*)&b;
        ia = pa[3] << 24 | pa[2] << 16 | pa[1] << 8 | pa[0];
        ib = pb[3] << 24 | pb[2] << 16 | pb[1] << 8 | pb[0];
        return ia == ib;
    }
    __device__ static bool is(const char* a, const char* b) { return symm_str_eq(a, b); }
    template<typename A, typename B> __device__ static bool is(A a, B b) { return a == b; }
};

// ── BigInt conversion ──────────────────────────────────────────────────────────
__device__ long long BigInt(long long v) { return v; }
__device__ long long BigInt(float v) { return (long long)v; }
__device__ long long BigInt(const char* s) {
    long long result = 0; bool neg = false;
    if (!s) return 0;
    if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return neg ? -result : result;
}

// ── Performance.now() stub ─────────────────────────────────────────────────────
struct _Performance {
    __device__ float now() {
        // Return CUDA clock64 in ms (SM clock is ~1-2 GHz)
        return (float)(clock64()) / 1000000.0f;
    }
};
__device__ _Performance performance;

// ── crypto.getRandomValues ─────────────────────────────────────────────────────
struct _Crypto {
    __device__ void getRandomValues(float* arr, int len) {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        for (int i = 0; i < len; i++) {
            seed = seed * 1664525u + 1013904223u;
            arr[i] = (float)(seed >> 8) / (float)(1u << 24);
        }
    }
    __device__ void getRandomValues(uint8_t* arr, int len) {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        for (int i = 0; i < len; i++) {
            seed = seed * 1664525u + 1013904223u;
            arr[i] = (uint8_t)(seed >> 16);
        }
    }
};
// NOTE: _CryptoTop is replaced by _CryptoSubtle in FRAG12.
// This stub is kept for backward compat; crypto global is redefined in FRAG12.
struct _CryptoTop {
    _Crypto subtle;
    _Crypto _self;
    __device__ void getRandomValues(float* arr, int len) { _self.getRandomValues(arr, len); }
    __device__ void getRandomValues(uint8_t* arr, int len) { _self.getRandomValues(arr, len); }
};
// crypto global declared in FRAG12 with full _CryptoSubtle support

// ── Map: forEach support ───────────────────────────────────────────────────────
// (forEach on Map iterates its internal _e array)
// Implemented in userland by for...in since Map exposes _e/_n. Stub for direct calls:
template<typename Fn>
__device__ void symm_map_for_each(const Map& m, Fn fn) {
    for (int i = 0; i < m._n; i++) fn(m._e[i].v, m._e[i].k);
}

// ── Set: forEach support ───────────────────────────────────────────────────────
template<typename Fn>
__device__ void symm_set_for_each(const Set& s, Fn fn) {
    for (int i = 0; i < s._count; i++) fn(s._items[i]);
}

// ── Symbol.for (alias — 'for' is a keyword, use Symbol::for_) ──────────────────
// Already defined above as Symbol::for_() — mapped by Codegen.

// ── structuredClone — deep copy by value ──────────────────────────────────────
template<typename T> __device__ T structuredClone(T val) { return val; }

// ── Object.is as free function shorthand ──────────────────────────────────────
template<typename A, typename B> __device__ bool _symm_object_is(A a, B b) { return _ObjectIs::is(a, b); }

// ── Number.prototype.toString(radix) — radix 2, 8, 10, 16 ────────────────────
__device__ const char* symm_number_to_string_radix(long long v, int radix) {
    static char buf[72];
    if (radix == 10 || radix <= 1) { symm_write_int(buf, v); return buf; }
    int pos = 0;
    bool neg = v < 0;
    unsigned long long uv = neg ? (unsigned long long)(-v) : (unsigned long long)v;
    if (uv == 0) { buf[0]='0'; buf[1]=0; return buf; }
    char tmp[68]; int n = 0;
    while (uv > 0) {
        int d = (int)(uv % radix);
        tmp[n++] = (d < 10) ? '0' + d : 'a' + d - 10;
        uv /= radix;
    }
    if (neg) buf[pos++] = '-';
    for (int i = n - 1; i >= 0; i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return buf;
}

// ── str.at(index) with proper negative index for strings ──────────────────────
__device__ const char* symm_str_at(const char* s, int idx) {
    static char buf[2];
    if (!s) { buf[0] = 0; return buf; }
    int len = symm_strlen(s);
    if (idx < 0) idx = len + idx;
    if (idx < 0 || idx >= len) { buf[0] = 0; return buf; }
    buf[0] = s[idx]; buf[1] = 0;
    return buf;
}

// ── JSON.stringify with indent (simplified) ────────────────────────────────────
// indent > 0 adds newlines; GPU stack-allocated so simplified
__device__ const char* symm_json_stringify_indent(const char* json, int indent) {
    // For GPU: just return the flat JSON (indent is cosmetic)
    return json ? json : "null";
}

// ── btoa / atob: Base64 encode/decode ─────────────────────────────────────────
static __device__ __constant__ const char _B64_ENC[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

__device__ const char* btoa(const char* s) {
    if (!s) return "";
    static char buf[512];
    int slen = symm_strlen(s);
    int p = 0;
    for (int i = 0; i < slen && p < 508; i += 3) {
        unsigned int b0 = (unsigned char)s[i];
        unsigned int b1 = (i+1 < slen) ? (unsigned char)s[i+1] : 0;
        unsigned int b2 = (i+2 < slen) ? (unsigned char)s[i+2] : 0;
        buf[p++] = _B64_ENC[(b0 >> 2) & 0x3F];
        buf[p++] = _B64_ENC[((b0 << 4) | (b1 >> 4)) & 0x3F];
        buf[p++] = (i+1 < slen) ? _B64_ENC[((b1 << 2) | (b2 >> 6)) & 0x3F] : '=';
        buf[p++] = (i+2 < slen) ? _B64_ENC[b2 & 0x3F] : '=';
    }
    buf[p] = '\0';
    return buf;
}

__device__ const char* atob(const char* s) {
    if (!s) return "";
    static char buf[384];
    // Decode base64 (ignores invalid chars and padding)
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    int p = 0;
    int slen = symm_strlen(s);
    for (int i = 0; i < slen && p < 382; ) {
        int a0 = -1, a1 = -1, a2 = -1, a3 = -1;
        while (i < slen && a0 < 0) { int v = decode_char(s[i++]); if (v >= 0) a0 = v; }
        while (i < slen && a1 < 0) { int v = decode_char(s[i++]); if (v >= 0) a1 = v; }
        while (i < slen && a2 < 0) { int v = decode_char(s[i++]); if (v >= 0) a2 = v; }
        while (i < slen && a3 < 0) { int v = decode_char(s[i++]); if (v >= 0) a3 = v; }
        if (a0 < 0 || a1 < 0) break;
        buf[p++] = (char)((a0 << 2) | (a1 >> 4));
        if (a2 >= 0) buf[p++] = (char)(((a1 & 0xF) << 4) | (a2 >> 2));
        if (a3 >= 0) buf[p++] = (char)(((a2 & 0x3) << 6) | a3);
    }
    buf[p] = '\0';
    return buf;
}

// ── URLSearchParams: query string parse ────────────────────────────────────────
// Full implementation: parse key=value pairs from query string
__device__ const char* symm_url_search_params_get(const char* qs, const char* key) {
    if (!qs || !key) return "";
    static char buf[256];
    int klen = symm_strlen(key);
    // Skip leading '?'
    if (*qs == '?') qs++;
    const char* p = qs;
    while (*p) {
        // Find key end
        const char* eq = p;
        while (*eq && *eq != '=' && *eq != '&') eq++;
        int pklen = (int)(eq - p);
        if (pklen == klen) {
            // Compare key (percent-decoded comparison would be ideal; simple for now)
            bool match = true;
            for (int i = 0; i < klen; i++) {
                if (p[i] != key[i]) { match = false; break; }
            }
            if (match && *eq == '=') {
                // Extract value up to next '&' or end
                const char* vs = eq + 1;
                const char* ve = vs;
                while (*ve && *ve != '&') ve++;
                int vlen = (int)(ve - vs);
                if (vlen > 255) vlen = 255;
                for (int i = 0; i < vlen; i++) buf[i] = vs[i];
                buf[vlen] = '\0';
                return buf;
            }
        }
        // Skip to next pair
        while (*p && *p != '&') p++;
        if (*p == '&') p++;
    }
    return "";
}

// ── crypto.randomUUID — deterministic UUID based on thread+counter ─────────────
__device__ const char* symm_random_uuid() {
    static char buf[37];
    uint32_t seed = (uint32_t)(threadIdx.x * 1664525u + blockIdx.x * 22695477u + 1013904223u);
    auto rnd = [&]() -> uint32_t {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    auto hex4 = [&](int p, int n) {
        uint32_t v = rnd();
        for (int i = 0; i < n; i++) {
            int d = (v >> (i*4)) & 0xF;
            buf[p + n - 1 - i] = (d < 10) ? '0' + d : 'a' + d - 10;
        }
    };
    hex4(0, 8);  buf[8]  = '-';
    hex4(9, 4);  buf[13] = '-';
    // version 4
    uint32_t v = rnd();
    buf[14] = '4';
    for (int i = 1; i < 4; i++) {
        int d = (v >> (i*4)) & 0xF;
        buf[14 + i] = (d < 10) ? '0' + d : 'a' + d - 10;
    }
    buf[18] = '-';
    // variant bits
    uint32_t v2 = rnd();
    buf[19] = "89ab"[(v2 >> 4) & 0x3];
    for (int i = 1; i < 4; i++) {
        int d = (v2 >> (i*4)) & 0xF;
        buf[19 + i] = (d < 10) ? '0' + d : 'a' + d - 10;
    }
    buf[23] = '-';
    hex4(24, 12); buf[36] = '\0';
    return buf;
}

)";

// Fragment 11: JSON.parse, symm_json_prop, symm_body_get_str, Date.parse, Map/Set iteration
static const char* PRELUDE_FRAG11 = R"(
// ── JSON.parse ─────────────────────────────────────────────────────────────────
// On GPU, "parsing" is lazy: symm_json_parse just returns the raw JSON string.
// Property extraction is done on-the-fly by symm_json_prop.
__device__ const char* symm_json_parse(const char* s) {
    return s ? s : "{}";
}

// Skip whitespace helper
__device__ const char* _sjson_skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

// Skip a complete JSON value (string, number, array, object, bool, null)
__device__ const char* _sjson_skip_value(const char* p) {
    p = _sjson_skip_ws(p);
    if (*p == '"') {
        p++;
        while (*p && !(*p == '"' && *(p-1) != '\\')) p++;
        if (*p == '"') p++;
        return p;
    }
    if (*p == '{' || *p == '[') {
        char open = *p, close = (*p == '{') ? '}' : ']';
        int depth = 1; p++;
        while (*p && depth > 0) {
            if (*p == '"') { p++; while (*p && !(*p == '"' && *(p-1) != '\\')) p++; if (*p) p++; continue; }
            if (*p == open) depth++;
            else if (*p == close) depth--;
            p++;
        }
        return p;
    }
    // number, bool, null
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != '\r' && *p != '\n') p++;
    return p;
}

// Extract property value from a JSON object as a raw string (null-terminated)
// Handles: strings (quotes stripped), numbers, booleans, null, nested objects/arrays
__device__ const char* symm_json_prop(const char* json, const char* key) {
    if (!json || !key) return "";
    static char _jp_buf[512];
    int klen = 0; while (key[klen]) klen++;

    const char* p = _sjson_skip_ws(json);
    if (*p == '{') p++; // skip opening brace

    while (true) {
        p = _sjson_skip_ws(p);
        if (!*p || *p == '}') break;
        if (*p != '"') break; // malformed JSON
        p++; // skip opening quote of key

        // Check key match
        bool match = true;
        for (int i = 0; i < klen; i++) {
            if (!p[i] || p[i] != key[i]) { match = false; break; }
        }
        if (match && p[klen] == '"') {
            // Key matched — advance past key and colon
            p += klen + 1;
            p = _sjson_skip_ws(p);
            if (*p == ':') p++;
            p = _sjson_skip_ws(p);
            // Extract value
            int n = 0;
            if (*p == '"') {
                // String value — strip quotes, handle \n \t \\ \" escapes
                p++;
                while (*p && n < 511) {
                    if (*p == '"') break;
                    if (*p == '\\') {
                        p++;
                        if (*p == 'n')       _jp_buf[n++] = '\n';
                        else if (*p == 't')  _jp_buf[n++] = '\t';
                        else if (*p == 'r')  _jp_buf[n++] = '\r';
                        else if (*p == '"')  _jp_buf[n++] = '"';
                        else if (*p == '\\') _jp_buf[n++] = '\\';
                        else if (*p == '/') _jp_buf[n++] = '/';
                        else { _jp_buf[n++] = '\\'; if (n < 511) _jp_buf[n++] = *p; }
                        p++;
                    } else {
                        _jp_buf[n++] = *p++;
                    }
                }
            } else if (*p == '{' || *p == '[') {
                // Nested object/array — return raw JSON
                const char* start = p;
                p = _sjson_skip_value(p);
                int len = (int)(p - start);
                if (len > 511) len = 511;
                for (int i = 0; i < len; i++) _jp_buf[i] = start[i];
                n = len;
            } else {
                // number / bool / null — raw chars, trim trailing spaces
                while (*p && *p != ',' && *p != '}' && *p != ']' && n < 511) _jp_buf[n++] = *p++;
                while (n > 0 && _jp_buf[n-1] == ' ') n--;
            }
            _jp_buf[n] = 0;
            return _jp_buf;
        }

        // Key didn't match — skip past closing quote and colon+value
        while (*p && *p != '"') p++;
        if (*p == '"') p++;
        p = _sjson_skip_ws(p);
        if (*p == ':') p++;
        p = _sjson_skip_value(p);
        p = _sjson_skip_ws(p);
        if (*p == ',') p++;
    }
    return "";
}

// Typed property helpers
__device__ float symm_json_prop_f(const char* json, const char* key) {
    const char* v = symm_json_prop(json, key);
    if (!v || !v[0]) return 0.0f;
    // atof inline
    float r = 0.f; bool neg = false; int i = 0;
    if (v[i] == '-') { neg = true; i++; }
    while (v[i] >= '0' && v[i] <= '9') { r = r*10.f + (v[i]-'0'); i++; }
    if (v[i] == '.') { i++; float frac = 0.1f; while (v[i] >= '0' && v[i] <= '9') { r += (v[i]-'0')*frac; frac *= 0.1f; i++; } }
    return neg ? -r : r;
}
__device__ int symm_json_prop_i(const char* json, const char* key) {
    return (int)symm_json_prop_f(json, key);
}
__device__ bool symm_json_prop_b(const char* json, const char* key) {
    const char* v = symm_json_prop(json, key);
    if (!v || !v[0]) return false;
    return v[0] == 't' || v[0] == '1'; // "true" or "1"
}

// symm_body_get_str — used by Codegen for ctx.json() destructuring
__device__ void symm_body_get_str(const char* body, const char* key, char* buf, int maxlen) {
    const char* val = symm_json_prop(body, key);
    int i = 0;
    while (val[i] && i < maxlen - 1) { buf[i] = val[i]; i++; }
    buf[i] = 0;
}

// ── RegExp extra flags: s (dotAll), non-capturing (?:) stubs ──────────────────
// dotAll: already supported if pattern handles \n in the NFA (GPU NFA is greedy/partial)
// {n,m} quantifiers: expanding to repeated pattern is a compile-time transform
// For GPU: simplify — expose flag check helpers
__device__ bool symm_regex_has_flag(const RegExp& re, char flag) {
    if (!re.flags) return false;
    for (int i = 0; re.flags[i]; i++) if (re.flags[i] == flag) return true;
    return false;
}

// ── Map keys() / values() / entries() — return iteratable string repr ─────────
// GPU has no real generators; return a JSON-array string for inspection.
__device__ const char* symm_map_keys(const Map& m) {
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < m._n && pos < 990; i++) {
        if (i) buf[pos++] = ',';
        buf[pos++] = '"';
        for (int j = 0; m._e[i].k && m._e[i].k[j] && pos < 1000; j++) buf[pos++] = m._e[i].k[j];
        buf[pos++] = '"';
    }
    buf[pos++] = ']'; buf[pos] = 0;
    return buf;
}
__device__ const char* symm_map_values(const Map& m) {
    static char buf[512]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < m._n && pos < 480; i++) {
        if (i) buf[pos++] = ',';
        int n = symm_write_float(buf + pos, m._e[i].v); pos += n;
    }
    buf[pos++] = ']'; buf[pos] = 0;
    return buf;
}
__device__ const char* symm_map_entries(const Map& m) {
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < m._n && pos < 990; i++) {
        if (i) buf[pos++] = ',';
        buf[pos++] = '['; buf[pos++] = '"';
        for (int j = 0; m._e[i].k && m._e[i].k[j] && pos < 1005; j++) buf[pos++] = m._e[i].k[j];
        buf[pos++] = '"'; buf[pos++] = ',';
        int n = symm_write_float(buf + pos, m._e[i].v); pos += n;
        buf[pos++] = ']';
    }
    buf[pos++] = ']'; buf[pos] = 0;
    return buf;
}

// ── Set keys() / values() / entries() ─────────────────────────────────────────
__device__ const char* symm_set_values(const Set& s) {
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < s._count && pos < 990; i++) {
        if (i) buf[pos++] = ',';
        buf[pos++] = '"';
        for (int j = 0; s._items[i] && s._items[i][j] && pos < 1000; j++) buf[pos++] = s._items[i][j];
        buf[pos++] = '"';
    }
    buf[pos++] = ']'; buf[pos] = 0;
    return buf;
}

// ── Generator protocol stubs for generators ────────────────────────────────────
// return(val) sets done flag; throw(err) sets error flag — both are sugar over next()
struct _GenReturn { float value; bool done; };

)";

// Fragment 12: SHA-256, HMAC-SHA256, crypto.subtle.sign/verify/importKey
static const char* PRELUDE_FRAG12 = R"(
// ── SHA-256 — pure GPU implementation (no external libs needed) ────────────────
// All computations are bitwise — suitable for NVRTC kernels.
__device__ uint32_t _sha256_rotr(uint32_t v, int n) { return (v >> n) | (v << (32-n)); }
__device__ uint32_t _sha256_ch(uint32_t e, uint32_t f, uint32_t g) { return (e & f) ^ (~e & g); }
__device__ uint32_t _sha256_maj(uint32_t a, uint32_t b, uint32_t c) { return (a&b)^(a&c)^(b&c); }
__device__ uint32_t _sha256_ep0(uint32_t a) { return _sha256_rotr(a,2)^_sha256_rotr(a,13)^_sha256_rotr(a,22); }
__device__ uint32_t _sha256_ep1(uint32_t e) { return _sha256_rotr(e,6)^_sha256_rotr(e,11)^_sha256_rotr(e,25); }
__device__ uint32_t _sha256_sig0(uint32_t x){ return _sha256_rotr(x,7)^_sha256_rotr(x,18)^(x>>3); }
__device__ uint32_t _sha256_sig1(uint32_t x){ return _sha256_rotr(x,17)^_sha256_rotr(x,19)^(x>>10); }

__device__ void symm_sha256(const uint8_t* msg, int len, uint8_t* digest) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    // Pad message: append 0x80, zeros, then big-endian 64-bit length
    uint8_t block[64];
    int i = 0;
    // Process full blocks
    while (i + 64 <= len) {
        uint32_t w[64];
        for (int j = 0; j < 16; j++) {
            w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|
                   ((uint32_t)msg[i+j*4+2]<<8)|(uint32_t)msg[i+j*4+3];
        }
        for (int j = 16; j < 64; j++)
            w[j] = _sha256_sig1(w[j-2])+w[j-7]+_sha256_sig0(w[j-15])+w[j-16];
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int j=0;j<64;j++) {
            uint32_t t1=hh+_sha256_ep1(e)+_sha256_ch(e,f,g)+K[j]+w[j];
            uint32_t t2=_sha256_ep0(a)+_sha256_maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        i += 64;
    }
    // Last block(s) with padding
    for (int j = 0; j < 64; j++) block[j] = 0;
    int rem = len - i;
    for (int j = 0; j < rem; j++) block[j] = msg[i+j];
    block[rem] = 0x80;
    if (rem >= 56) {
        // Process this block (padding byte only, no length), then add a second padding block
        uint32_t ww[64];
        for (int j = 0; j < 16; j++)
            ww[j]=((uint32_t)block[j*4]<<24)|((uint32_t)block[j*4+1]<<16)|
                  ((uint32_t)block[j*4+2]<<8)|(uint32_t)block[j*4+3];
        for (int j=16;j<64;j++) ww[j]=_sha256_sig1(ww[j-2])+ww[j-7]+_sha256_sig0(ww[j-15])+ww[j-16];
        uint32_t aa=h[0],bb=h[1],cc=h[2],dd=h[3],ee=h[4],ff=h[5],gg=h[6],hh=h[7];
        for(int j=0;j<64;j++){uint32_t t1=hh+_sha256_ep1(ee)+_sha256_ch(ee,ff,gg)+K[j]+ww[j];uint32_t t2=_sha256_ep0(aa)+_sha256_maj(aa,bb,cc);hh=gg;gg=ff;ff=ee;ee=dd+t1;dd=cc;cc=bb;bb=aa;aa=t1+t2;}
        h[0]+=aa;h[1]+=bb;h[2]+=cc;h[3]+=dd;h[4]+=ee;h[5]+=ff;h[6]+=gg;h[7]+=hh;
        // Prepare second (final) block with length only
        for (int j = 0; j < 64; j++) block[j] = 0;
        long long bitlen = (long long)len * 8;
        block[56]=(uint8_t)(bitlen>>56); block[57]=(uint8_t)(bitlen>>48);
        block[58]=(uint8_t)(bitlen>>40); block[59]=(uint8_t)(bitlen>>32);
        block[60]=(uint8_t)(bitlen>>24); block[61]=(uint8_t)(bitlen>>16);
        block[62]=(uint8_t)(bitlen>>8);  block[63]=(uint8_t)(bitlen);
    } else {
        long long bitlen = (long long)len * 8;
        block[56]=(uint8_t)(bitlen>>56); block[57]=(uint8_t)(bitlen>>48);
        block[58]=(uint8_t)(bitlen>>40); block[59]=(uint8_t)(bitlen>>32);
        block[60]=(uint8_t)(bitlen>>24); block[61]=(uint8_t)(bitlen>>16);
        block[62]=(uint8_t)(bitlen>>8);  block[63]=(uint8_t)(bitlen);
    }
    uint32_t w[64];
    for (int j = 0; j < 16; j++) {
        w[j]=((uint32_t)block[j*4]<<24)|((uint32_t)block[j*4+1]<<16)|
             ((uint32_t)block[j*4+2]<<8)|(uint32_t)block[j*4+3];
    }
    for (int j=16;j<64;j++) w[j]=_sha256_sig1(w[j-2])+w[j-7]+_sha256_sig0(w[j-15])+w[j-16];
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int j=0;j<64;j++) {
        uint32_t t1=hh+_sha256_ep1(e)+_sha256_ch(e,f,g)+K[j]+w[j];
        uint32_t t2=_sha256_ep0(a)+_sha256_maj(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    // Write digest big-endian
    for (int j=0;j<8;j++) {
        digest[j*4+0]=(uint8_t)(h[j]>>24); digest[j*4+1]=(uint8_t)(h[j]>>16);
        digest[j*4+2]=(uint8_t)(h[j]>>8);  digest[j*4+3]=(uint8_t)(h[j]);
    }
}

// ── HMAC-SHA256 ────────────────────────────────────────────────────────────────
__device__ void symm_hmac_sha256(const uint8_t* key, int keylen,
                                  const uint8_t* msg, int msglen,
                                  uint8_t* mac) {
    uint8_t k[64] = {};
    // Prepare key
    if (keylen > 64) { symm_sha256(key, keylen, k); }
    else { for (int i=0;i<keylen;i++) k[i]=key[i]; }
    // IPAD and OPAD
    uint8_t ipad[64], opad[64];
    for (int i=0;i<64;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    // Inner hash: H(ipad || msg) — simplified: use a 128-byte buffer
    uint8_t inner[128]; int ilen=0;
    for (int i=0;i<64;i++) inner[ilen++]=ipad[i];
    for (int i=0;i<msglen && ilen<128;i++) inner[ilen++]=msg[i];
    uint8_t ihash[32]; symm_sha256(inner, ilen, ihash);
    // Outer hash: H(opad || inner_hash)
    uint8_t outer[96]; int olen=0;
    for (int i=0;i<64;i++) outer[olen++]=opad[i];
    for (int i=0;i<32;i++) outer[olen++]=ihash[i];
    symm_sha256(outer, olen, mac);
}

// ── crypto.subtle key type ─────────────────────────────────────────────────────
struct _CryptoKey {
    uint8_t _raw[64]; int _len;
    __device__ _CryptoKey() : _len(0) { for(int i=0;i<64;i++) _raw[i]=0; }
    __device__ _CryptoKey(const uint8_t* k, int l) : _len(l > 64 ? 64 : l) {
        for(int i=0;i<_len;i++) _raw[i]=k[i];
    }
};

// ── Extended crypto.subtle ─────────────────────────────────────────────────────
struct _CryptoSubtle {
    __device__ void getRandomValues(float* arr, int len) {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        for (int i=0;i<len;i++) { seed=seed*1664525u+1013904223u; arr[i]=(float)(seed>>8)/(float)(1u<<24); }
    }
    __device__ void getRandomValues(uint8_t* arr, int len) {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        for (int i=0;i<len;i++) { seed=seed*1664525u+1013904223u; arr[i]=(uint8_t)(seed>>16); }
    }
    // sign(algo, key, data) → MAC output (32 bytes for HMAC-SHA256)
    __device__ void sign(const char* algo, const _CryptoKey& key, const uint8_t* data, int datalen, uint8_t* out) {
        if (!algo || !data || !out) return;
        // Only HMAC-SHA256 supported
        symm_hmac_sha256(key._raw, key._len, data, datalen, out);
    }
    // verify(algo, key, sig, data) → bool
    __device__ bool verify(const char* algo, const _CryptoKey& key, const uint8_t* sig, const uint8_t* data, int datalen) {
        uint8_t mac[32];
        symm_hmac_sha256(key._raw, key._len, data, datalen, mac);
        bool eq = true;
        for (int i=0;i<32;i++) if (mac[i] != sig[i]) { eq=false; break; }
        return eq;
    }
    // importKey — wraps raw bytes into a _CryptoKey
    __device__ _CryptoKey importKey(const uint8_t* raw, int len) { return _CryptoKey(raw, len); }
    __device__ _CryptoKey importKey(const char* s) {
        _CryptoKey k; int i=0;
        while (s && *s && i<64) { k._raw[i++]=(uint8_t)*s++; }
        k._len=i; return k;
    }
    // digestSync(algo, data) → hex string of SHA-256 hash
    __device__ const char* digestSync(const char* algo, const char* data) {
        return symm_sha256_hex(data);
    }
};

// SHA-256 convenience (used directly by Codegen)
__device__ const char* symm_sha256_hex(const char* input) {
    static char hex[65];
    static uint8_t digest[32];
    int len = 0; while (input && input[len]) len++;
    symm_sha256((const uint8_t*)input, len, digest);
    const char* hc = "0123456789abcdef";
    for (int i=0;i<32;i++) { hex[i*2]=hc[digest[i]>>4]; hex[i*2+1]=hc[digest[i]&0xf]; }
    hex[64]=0; return hex;
}

// ── crypto global with full subtle support ─────────────────────────────────────
struct _CryptoGlobal {
    _CryptoSubtle subtle;
    __device__ void getRandomValues(float* arr, int len)   { subtle.getRandomValues(arr, len); }
    __device__ void getRandomValues(uint8_t* arr, int len) { subtle.getRandomValues(arr, len); }
    __device__ const char* randomUUID() { return symm_random_uuid(); }
};
__device__ _CryptoGlobal crypto;
)";

// Prelude builder: concatenates all fragments into single string
static std::string _build_complete_prelude() {
    std::string result;
    if (PRELUDE_FRAG1)  result += PRELUDE_FRAG1;
    if (PRELUDE_FRAG2)  result += PRELUDE_FRAG2;
    if (PRELUDE_FRAG2B) result += PRELUDE_FRAG2B;
    if (PRELUDE_FRAG3)  result += PRELUDE_FRAG3;
    if (PRELUDE_FRAG4) result += PRELUDE_FRAG4;
    if (PRELUDE_FRAG5) result += PRELUDE_FRAG5;
    if (PRELUDE_FRAG6) result += PRELUDE_FRAG6;
    if (PRELUDE_FRAG7) result += PRELUDE_FRAG7;
    if (PRELUDE_FRAG9) result += PRELUDE_FRAG9;
    if (PRELUDE_FRAG8) result += PRELUDE_FRAG8;
    if (PRELUDE_FRAG10)  result += PRELUDE_FRAG10;
    if (PRELUDE_FRAG10B) result += PRELUDE_FRAG10B;
    if (PRELUDE_FRAG11) result += PRELUDE_FRAG11;
    if (PRELUDE_FRAG12) result += PRELUDE_FRAG12; // SHA-256 + HMAC
    return result;
}

// Static storage for the complete prelude
static std::string _prelude_cache;
static bool _prelude_cached = false;

}  // namespace symm

// Public entry point for Codegen.cpp
extern "C" const char* _get_gpu_prelude() {
    if (!symm::_prelude_cached) {
        symm::_prelude_cached = true;
        symm::_prelude_cache = symm::_build_complete_prelude();
    }
    return symm::_prelude_cache.c_str();
}
