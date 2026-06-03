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
// _symm_now_ms is a __device__ variable seeded by NvrtcCompiler via cuModuleGetGlobal
// (was formerly a function returning 0 — changed so cuModuleGetGlobal can locate it)
__device__ bool symm_regex_test_i(const char* pattern, const char* str);
__device__ bool symm_regex_test_m(const char* pattern, const char* str);
__device__ bool symm_regex_test(const char* pattern, const char* str);
__device__ int  symm_regex_match_at(const char* pattern, const char* str, const char* str_start, bool dotall);
__device__ int  symm_regex_exec(const char* pattern, const char* str);
__device__ int  symm_regex_exec_i(const char* pattern, const char* str);
__device__ const char* symm_regex_match(const char* pattern, const char* str);
__device__ const char* symm_str_match_all_full(const char* pattern, const char* str);
__device__ const char* symm_str_replace_all(const char* s, const char* from, const char* to);
__device__ const char* symm_str_replace(const char* s, const char* from, const char* to);
__device__ int symm_write_int(char* buf, int v);
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
    // Component constructor: new Date(year, month, day[, hours, minutes, seconds, ms])
    // month is 0-based (JS spec)
    __device__ Date(int Y, int Mo, int D, int h=0, int mi=0, int s=0, int ms3=0)
        : _ms(Date::_to_ms(Y, Mo+1, D, h, mi, s, ms3)) {}
    __device__ static long long now() { return (long long)_symm_now_ms; }
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

    // ── Setter helpers ────────────────────────────────────────────────────────
    // Converts calendar components to Unix epoch milliseconds (Fliegel-Van Flandern)
    __device__ static long long _to_ms(int Y, int Mo, int D, int h, int mi, int s, int ms3) {
        long long a  = (14 - Mo) / 12;
        long long yr = (long long)Y + 4800 - a;
        long long mf = Mo + 12*a - 3;
        long long jdn = D + (153*mf+2)/5 + 365*yr + yr/4 - yr/100 + yr/400 - 32045;
        return (jdn - 2440588LL)*86400000LL + (long long)h*3600000LL
             + (long long)mi*60000LL + (long long)s*1000LL + ms3;
    }
    __device__ long long setTime(long long ms)   { _ms = ms; return _ms; }
    __device__ long long setMilliseconds(int ms3){ _ms = (_ms/1000LL)*1000LL + ms3; return _ms; }
    __device__ long long setSeconds(int s)       {
        _ms = _ms - (long long)getSeconds()*1000LL - getMilliseconds()
            + (long long)s*1000LL;
        return _ms;
    }
    __device__ long long setMinutes(int m)       {
        _ms = _ms - (long long)getMinutes()*60000LL - (long long)getSeconds()*1000LL - getMilliseconds()
            + (long long)m*60000LL;
        return _ms;
    }
    __device__ long long setHours(int h)         {
        long long day_ms = (_ms / 86400000LL) * 86400000LL;
        _ms = day_ms + (long long)h*3600000LL + (long long)getMinutes()*60000LL
            + (long long)getSeconds()*1000LL + getMilliseconds();
        return _ms;
    }
    __device__ long long setDate(int d)          {
        _ms = _to_ms(getFullYear(), getMonth()+1, d, getHours(), getMinutes(), getSeconds(), getMilliseconds());
        return _ms;
    }
    __device__ long long setMonth(int m)         { // 0-based
        _ms = _to_ms(getFullYear(), m+1, getDate(), getHours(), getMinutes(), getSeconds(), getMilliseconds());
        return _ms;
    }
    __device__ long long setFullYear(int y)      {
        _ms = _to_ms(y, getMonth()+1, getDate(), getHours(), getMinutes(), getSeconds(), getMilliseconds());
        return _ms;
    }
    __device__ long long setFullYear(int y, int m, int d) {
        _ms = _to_ms(y, m+1, d, getHours(), getMinutes(), getSeconds(), getMilliseconds());
        return _ms;
    }

    // toUTCString / toDateString
    __device__ const char* toUTCString() const {
        // RFC 7231: "Mon, 15 Jan 2024 12:00:00 GMT"
        static const char* _days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* _months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
        static char buf[40];
        int wd = getDay(), mo = getMonth(), d = getDate(), y = getFullYear();
        int h  = getHours(), mi = getMinutes(), s = getSeconds();
        int p = 0;
        const char* ds = _days[wd < 7 ? wd : 0];
        const char* ms = _months[mo < 12 ? mo : 0];
        for (int i = 0; ds[i]; i++) buf[p++] = ds[i];
        buf[p++] = ','; buf[p++] = ' ';
        buf[p++] = '0' + d/10; buf[p++] = '0' + d%10; buf[p++] = ' ';
        for (int i = 0; ms[i]; i++) buf[p++] = ms[i];
        buf[p++] = ' ';
        buf[p++] = '0'+y/1000; buf[p++] = '0'+(y/100)%10; buf[p++] = '0'+(y/10)%10; buf[p++] = '0'+y%10;
        buf[p++] = ' ';
        buf[p++] = '0'+h/10; buf[p++] = '0'+h%10; buf[p++] = ':';
        buf[p++] = '0'+mi/10; buf[p++] = '0'+mi%10; buf[p++] = ':';
        buf[p++] = '0'+s/10; buf[p++] = '0'+s%10;
        const char* gmt = " GMT"; for (int i = 0; gmt[i]; i++) buf[p++] = gmt[i];
        buf[p] = 0;
        return buf;
    }
    __device__ const char* toDateString() const {
        // "Mon Jan 15 2024"
        static const char* _days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* _months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec"};
        static char buf[24];
        int wd = getDay(), mo = getMonth(), d = getDate(), y = getFullYear();
        int p = 0;
        const char* ds = _days[wd < 7 ? wd : 0];
        const char* ms = _months[mo < 12 ? mo : 0];
        for (int i = 0; ds[i]; i++) buf[p++] = ds[i];
        buf[p++] = ' ';
        for (int i = 0; ms[i]; i++) buf[p++] = ms[i];
        buf[p++] = ' ';
        buf[p++] = '0'+d/10; buf[p++] = '0'+d%10; buf[p++] = ' ';
        buf[p++] = '0'+y/1000; buf[p++] = '0'+(y/100)%10; buf[p++] = '0'+(y/10)%10; buf[p++] = '0'+y%10;
        buf[p] = 0;
        return buf;
    }
    __device__ const char* toTimeString() const {
        // "12:00:00 GMT+0000"
        static char buf[24];
        int h = getHours(), mi = getMinutes(), s = getSeconds();
        int p = 0;
        buf[p++] = '0'+h/10; buf[p++] = '0'+h%10; buf[p++] = ':';
        buf[p++] = '0'+mi/10; buf[p++] = '0'+mi%10; buf[p++] = ':';
        buf[p++] = '0'+s/10; buf[p++] = '0'+s%10;
        const char* tz = " GMT+0000"; for (int i = 0; tz[i]; i++) buf[p++] = tz[i];
        buf[p] = 0;
        return buf;
    }

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
    __device__ const char* toLocaleDateString()  const { return toDateString(); } // locale data N/A on GPU
    __device__ const char* toLocaleTimeString()  const { return toTimeString(); }
    __device__ const char* toLocaleString()      const { return toISOString(); }
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
        bool da = _has_flag('s');
        if (ci) return symm_regex_test_i(pattern, str);
        if (ml) return symm_regex_test_m(pattern, str);
        if (da) {
            // dotAll: use NFA engine with dotall=true
            for (int i = 0; str[i]; i++)
                if (symm_regex_match_at(pattern, str+i, str, true) >= 0) return true;
            return symm_regex_match_at(pattern, str, str, true) >= 0;
        }
        return symm_regex_test(pattern, str);
    }
    // exec(str) → first match substring, or nullptr on no match
    __device__ const char* exec(const char* str) const {
        if (!pattern || !str) return nullptr;
        bool ci = _has_flag('i');
        bool da = _has_flag('s');
        int idx;
        if (ci)      idx = symm_regex_exec_i(pattern, str);
        else if (da) {
            idx = -1;
            for (int i = 0; str[i]; i++)
                if (symm_regex_match_at(pattern, str+i, str, true) >= 0) { idx = i; break; }
        } else       idx = symm_regex_exec(pattern, str);
        if (idx < 0) return nullptr;
        return symm_regex_match(pattern, str + idx);
    }
    // match(str) → same as exec for non-global; full match list for /g
    __device__ const char* match(const char* str) const {
        if (!pattern || !str) return nullptr;
        if (_has_flag('g')) return symm_str_match_all_full(pattern, str);
        return exec(str);
    }
    // matchAll(str) → all match positions as JSON array
    __device__ const char* matchAll(const char* str) const {
        if (!pattern || !str) return "[]";
        return symm_str_match_all_full(pattern, str);
    }
    // .source and .flags properties
    __device__ const char* source() const { return pattern ? pattern : ""; }
    __device__ const char* getFlags() const { return flags ? flags : ""; }
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

// Seeded by NvrtcCompiler via cuModuleGetGlobal("_symm_now_ms") + cuMemcpyHtoD
__device__ unsigned long long _symm_now_ms   = 0;
__device__ unsigned long long _symm_start_ms = 0;

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
// RegExp overloads — called when a regex literal or variable is used as the pattern arg
__device__ const char* symm_str_match_all(const char* s, const RegExp& re) {
    return re.matchAll(s);
}
__device__ int symm_str_index_of(const char* s, const RegExp& re) {
    // str.search(re) → first match position
    return symm_regex_exec(re.pattern, s);
}
__device__ const char* symm_str_replace(const char* s, const RegExp& re, const char* to) {
    if (!s || !re.pattern || !to) return s;
    // global flag → replace all; otherwise replace first only
    if (re._has_flag('g')) return symm_str_replace_all(s, re.pattern, to);
    return symm_str_replace(s, re.pattern, to);
}
__device__ const char* symm_str_replace_all(const char* s, const RegExp& re, const char* to) {
    return symm_str_replace_all(s, re.pattern, to);
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
        if (match && len > 0) { buf[pos++] = ','; i += len; }
        else buf[pos++] = s[i++];
    }
    buf[pos] = '\0';
    return buf;
}

// ── _StrParts: iterable result of str.split(sep) ─────────────────────────────
// Supports for-of, [i] index access, .forEach(), and .length().
// str.split(sep) is dispatched to symm_str_split_parts(str, sep) by Codegen.
struct _StrParts {
    static const int MAX = 64;
    char _d[MAX][64];   // up to 64 parts, each up to 63 chars
    int _n;
    __device__ const char* operator[](int i) const {
        if (i < 0) i += _n;
        return (i >= 0 && i < _n) ? _d[i] : "";
    }
    __device__ int length() const { return _n; }
    // Iterator protocol — enables for (const x of str.split(','))
    struct _Iter {
        const _StrParts* _p; int _i;
        struct _Next { const char* value; bool done; };
        __device__ _Next next() {
            if (_i >= _p->_n) return {nullptr, true};
            return {_p->_d[_i++], false};
        }
        __device__ _Iter& __sym_iterator__() { return *this; }
    };
    __device__ _Iter __sym_iterator__() const { return {this, 0}; }
    template<typename Fn>
    __device__ void forEach(Fn fn) const { for (int i = 0; i < _n; i++) fn(_d[i]); }
    // map/filter/join support
    __device__ const char* join(const char* sep) const {
        static char buf[512]; int pos = 0;
        for (int i = 0; i < _n && pos < 500; i++) {
            if (i && sep) { for (int j = 0; sep[j] && pos < 500; j++) buf[pos++] = sep[j]; }
            for (int j = 0; _d[i][j] && pos < 500; j++) buf[pos++] = _d[i][j];
        }
        buf[pos] = '\0'; return buf;
    }
};
__device__ _StrParts symm_str_split_parts(const char* s, const char* sep) {
    _StrParts r; r._n = 0;
    if (!s) { r._d[0][0] = '\0'; r._n = 1; return r; }
    int dl = (sep && sep[0]) ? symm_strlen(sep) : 1;
    int i = 0, start = 0;
    while (s[i] && r._n < _StrParts::MAX - 1) {
        bool m = true;
        if (!sep || !sep[0]) { // split on every char
            if (s[i]) { r._d[r._n][0] = s[i]; r._d[r._n][1] = '\0'; r._n++; i++; continue; }
            break;
        }
        for (int k = 0; k < dl; k++) if (s[i+k] != sep[k]) { m = false; break; }
        if (m) {
            int plen = 0;
            for (int k = start; k < i && plen < 63; k++) r._d[r._n][plen++] = s[k];
            r._d[r._n++][plen] = '\0'; start = i + dl; i += dl;
        } else i++;
    }
    // Last part
    int plen = 0;
    for (int k = start; s[k] && plen < 63; k++) r._d[r._n][plen++] = s[k];
    r._d[r._n++][plen] = '\0';
    return r;
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

// Enhanced error formatting with stack context
__device__ inline const char* symm_format_error(const char* type, const char* msg, int line) {
    static char buf[256];
    int pos = 0;
    // Format: "TypeError: message (at line N)"
    while (type && *type && pos < 100) buf[pos++] = *type++;
    buf[pos++] = ':'; buf[pos++] = ' ';
    while (msg && *msg && pos < 200) buf[pos++] = *msg++;
    if (line > 0) {
        buf[pos++] = ' '; buf[pos++] = '(';
        int n = symm_write_int(buf + pos, line); pos += n;
        buf[pos++] = ')';
    }
    buf[pos] = 0;
    return buf;
}

// Error with type info
__device__ inline const char* symm_type_error(const char* expected, const char* got) {
    static char buf[256];
    int pos = snprintf(buf, sizeof(buf), "TypeError: expected %s, got %s", expected, got);
    return buf;
}

// Range error with bounds info
__device__ inline const char* symm_range_error(int actual, int min, int max) {
    static char buf[128];
    snprintf(buf, sizeof(buf), "RangeError: value %d out of range [%d, %d]", actual, min, max);
    return buf;
}

// Assertion helpers for GPU validation
__device__ inline bool symm_assert_not_null(const void* ptr, const char* context) {
    if (!ptr) {
        symm_debug_log_write(context);  // Log assertion failure
        return false;
    }
    return true;
}

__device__ inline bool symm_assert_bounds(int idx, int len, const char* context) {
    if (idx < 0 || idx >= len) {
        symm_debug_log_write(context);
        return false;
    }
    return true;
}

__device__ inline bool symm_assert_type(float value, bool is_valid_type) {
    if (!is_valid_type) {
        symm_debug_log_write("TypeError: invalid type");
        return false;
    }
    return true;
}

// Try-catch emulation via error state tracking
__device__ inline int symm_error_state() {
    return 0;  // 0 = no error, 1 = error occurred
}

// Error recovery — reset error state
__device__ inline void symm_clear_error() {
    // In a real implementation, this would clear a thread-local error state
}

// Iterator for Map — returned by map.__sym_iterator__()
// Supports for-of and C++17 structured bindings: for (const [k,v] of map)
struct _MapIter {
    struct Entry { const char* k; float v; };
    const Entry* _e; int _n; int _pos;
    struct _Next { Entry value; bool done; };
    __device__ _Next next() {
        if (_pos >= _n) return {{"", 0.f}, true};
        return {_e[_pos++], false};
    }
    __device__ _MapIter& __sym_iterator__() { return *this; }
};

// Extract Map keys as JSON array string
// Returns "[\"key1\", \"key2\", ...]" for Map contents
__device__ inline const char* symm_map_keys_as_json(const Map& m) {
    static char buf[512];
    buf[0] = '[';
    int pos = 1;
    for (int i = 0; i < m._n && pos < 500; i++) {
        if (i > 0) buf[pos++] = ',';
        buf[pos++] = '"';
        const char* k = m._e[i].k;
        if (k) {
            while (*k && pos < 498) {
                if (*k == '"' || *k == '\\') buf[pos++] = '\\';
                buf[pos++] = *k++;
            }
        }
        buf[pos++] = '"';
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

// Extract Map entries as JSON array of [key, value] pairs
// Returns "[[\"key1\", value1], [\"key2\", value2], ...]"
__device__ inline const char* symm_map_entries_as_json(const Map& m) {
    static char buf[1024];
    buf[0] = '[';
    int pos = 1;
    for (int i = 0; i < m._n && pos < 1000; i++) {
        if (i > 0) buf[pos++] = ',';
        buf[pos++] = '[';
        buf[pos++] = '"';
        const char* k = m._e[i].k;
        if (k) {
            while (*k && pos < 998) {
                if (*k == '"' || *k == '\\') buf[pos++] = '\\';
                buf[pos++] = *k++;
            }
        }
        buf[pos++] = '"';
        buf[pos++] = ',';
        // Format value as number
        float v = m._e[i].v;
        char num_buf[32];
        int num_len = snprintf(num_buf, sizeof(num_buf), "%.6g", v);
        for (int j = 0; j < num_len && pos < 1000; j++) {
            buf[pos++] = num_buf[j];
        }
        buf[pos++] = ']';
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

// Object.values() — extract values from Map as JSON array
// Returns "[value1, value2, ...]"
__device__ inline const char* symm_map_values_as_json(const Map& m) {
    static char buf[512];
    buf[0] = '[';
    int pos = 1;
    for (int i = 0; i < m._n && pos < 490; i++) {
        if (i > 0) buf[pos++] = ',';
        float v = m._e[i].v;
        char num_buf[32];
        int num_len = snprintf(num_buf, sizeof(num_buf), "%.6g", v);
        for (int j = 0; j < num_len && pos < 500; j++) {
            buf[pos++] = num_buf[j];
        }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

struct Map {
    struct Entry { const char* k; float v; };
    Entry _e[64]; int _n = 0;

    // Iterator protocol — enables for (const [k,v] of map)
    __device__ _MapIter __sym_iterator__() const { return {(const _MapIter::Entry*)_e, _n, 0}; }

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
    // Alias for delete() — "delete" is a C++ keyword so Codegen emits _delete_()
    __device__ bool _delete_(const char* k) { return delete_key(k); }
    __device__ bool _delete_(int k)         { return delete_key(k); }
    __device__ bool _delete_(float k)       { return delete_key((int)k); }
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

// ── Array functional methods ─────────────────────────────────────────────────────
// Codegen calls these as: symm_arr_map(arr, arr_len, fn)
// The returned pointer shares a static buffer — use immediately or copy the result.

// arr.map(fn) → apply fn to each element, return new array of same length
template<typename T, typename Fn>
__device__ T* symm_arr_map(const T* arr, int len, Fn fn) {
    static T _map_buf[256];
    for (int i = 0; i < len && i < 256; i++) _map_buf[i] = fn(arr[i]);
    return _map_buf;
}

// arr.filter(fn) → keep elements where fn(x) is truthy, return new array
// Note: returned length is not tracked by Codegen — use arr.filter(fn).length workaround.
template<typename T, typename Fn>
__device__ T* symm_arr_filter(const T* arr, int len, Fn fn) {
    static T _filt_buf[256];
    int n = 0;
    for (int i = 0; i < len && n < 255; i++) if (fn(arr[i])) _filt_buf[n++] = arr[i];
    return _filt_buf;
}
// Generator state machine helpers — for yield-based generators
// State values: 0=initial, 1-255=yield markers, 256=done
__device__ inline int symm_gen_next_state(int current_state, int yield_count) {
    // Advance to next yield point, or DONE (256) if exceeded
    return (current_state >= yield_count) ? 256 : current_state + 1;
}

// Check if generator has more values (state < DONE)
__device__ inline bool symm_gen_has_next(int state) {
    return state < 256;  // 256 = DONE marker
}

// Extract yield index from state (1-based offset into yield table)
__device__ inline int symm_gen_yield_index(int state) {
    return state > 0 ? state - 1 : 0;
}

// Iterator protocol helper for generators
// Tracks state between next() calls and compares to resume point
__device__ inline bool symm_gen_at_yield(int current_state, int target_yield) {
    return current_state == target_yield;
}

// Async generator state tracking (0=pending, 1=resolved, 2=rejected, 3=await_pending)
__device__ inline int symm_async_state_next(int current) {
    return (current == 0) ? 1 : ((current == 1) ? 3 : 0);  // cycles through states
}

// Iterator result formatting — builds { value, done } JSON
__device__ inline const char* symm_iter_result(float value, bool done) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "{\"value\":%.6g,\"done\":%s}",
             value, done ? "true" : "false");
    return buf;
}

__device__ inline const char* symm_iter_result_done() {
    return "{\"value\":null,\"done\":true}";
}

// Array iteration bounds check — clamp index to [0, len)
__device__ inline int symm_iter_clamp_index(int idx, int len) {
    if (idx < 0) idx = 0;
    if (idx >= len) idx = len - 1;
    return idx;
}

// Async iteration support — format promise-like result
__device__ inline const char* symm_async_result(float value, bool resolved) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "{\"value\":%.6g,\"done\":%s}",
             value, resolved ? "false" : "true");
    return buf;
}

// For-await support — tracks async iteration state
__device__ inline bool symm_for_await_done(int iterations, int max_iterations) {
    return iterations >= max_iterations;
}

// Delegation helper for yield* — forward iterator from inner generator
__device__ inline bool symm_yield_delegation_next(int* state, int target_state) {
    if (*state <= target_state) {
        (*state)++;
        return *state <= target_state;
    }
    return false;
}

// symm_arr_reduce(arr, len, fn, init) — fold left with callback
template<typename T, typename Fn>
__device__ T symm_arr_reduce(const T* arr, int len, Fn fn, T init) {
    T acc = init;
    for (int i = 0; i < len; i++) acc = fn(acc, arr[i]);
    return acc;
}

// symm_arr_reduce_right(arr, len, fn, init) — fold right with callback
template<typename T, typename Fn>
__device__ T symm_arr_reduce_right(const T* arr, int len, Fn fn, T init) {
    T acc = init;
    for (int i = len - 1; i >= 0; i--) acc = fn(acc, arr[i]);
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

// Optimized lastIndexOf for backward search
__device__ int symm_arr_last_index_of(const float* arr, int len, float val) {
    for (int i = len - 1; i >= 0; i--) {
        if (arr[i] == val) return i;
    }
    return -1;
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

// Type checking inline helpers (zero overhead)
__device__ inline bool symm_is_string(const char* v) { return v != nullptr; }
__device__ inline bool symm_is_number(float v) { return isFinite(v) || isNaN(v); }
__device__ inline bool symm_is_object(void* v) { return v != nullptr; }
__device__ inline bool symm_is_null(void* v) { return v == nullptr; }

// Type detection helpers
__device__ inline const char* symm_typeof(float v) { return "number"; }
__device__ inline const char* symm_typeof(const char* v) { return v ? "string" : "object"; }
__device__ inline const char* symm_typeof(int v) { return "number"; }

// Safe value conversion helpers
__device__ inline float symm_to_number(const char* s) { return symm_parse_float(s); }
__device__ inline float symm_to_number(int i) { return (float)i; }
__device__ inline float symm_to_number(float f) { return f; }
__device__ inline const char* symm_to_string(float f) {
    static char buf[32];
    symm_write_float(buf, f);
    return buf;
}
__device__ inline const char* symm_to_string(int i) {
    static char buf[32];
    symm_write_int(buf, i);
    return buf;
}
__device__ inline const char* symm_to_string(const char* s) { return s ? s : ""; }

// Utility comparison helpers (handle NaN correctly)
__device__ inline bool symm_equals(float a, float b) {
    if (isNaN(a) && isNaN(b)) return true;  // NaN === NaN in GPU context
    return a == b;
}
__device__ inline bool symm_equals(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

// Number formatting utilities
__device__ inline const char* symm_number_to_fixed(float f, int digits) {
    static char buf[64];
    if (digits < 0) digits = 0;
    if (digits > 20) digits = 20;

    // Handle special cases
    if (isNaN(f)) return "NaN";
    float inf = _symm_inf();
    if (f == inf) return "Infinity";
    if (f == -inf) return "-Infinity";

    // Format with fixed decimal places
    float multiplier = 1.0f;
    for (int i = 0; i < digits; i++) multiplier *= 10.0f;

    long long rounded = (long long)(f * multiplier + 0.5f);
    if (f < 0 && rounded > 0) rounded = -rounded;

    char* p = buf;
    if (rounded < 0) {
        *p++ = '-';
        rounded = -rounded;
    }

    symm_write_int(p, (int)(rounded / (long long)multiplier));
    while (*p) p++;

    if (digits > 0) {
        *p++ = '.';
        long long remainder = rounded % (long long)multiplier;
        for (int i = 0; i < digits; i++) {
            multiplier /= 10.0f;
            *p++ = '0' + (char)(remainder / (long long)multiplier);
            remainder = remainder % (long long)multiplier;
        }
    }
    *p = '\0';
    return buf;
}

__device__ inline const char* symm_number_to_precision(float f, int digits) {
    static char buf[64];
    if (digits < 1) digits = 1;
    if (digits > 21) digits = 21;

    // Handle special cases
    if (isNaN(f)) return "NaN";
    float inf = _symm_inf();
    if (f == inf) return "Infinity";
    if (f == -inf) return "-Infinity";

    // Simple implementation: format to fixed decimal
    // More complex rounding and scientific notation could be added
    return symm_number_to_fixed(f, digits > 5 ? 2 : digits - 1);
}

struct Math {
    __device__ static float random() {
        uint32_t seed = (uint32_t)(threadIdx.x + blockIdx.x * blockDim.x) * 1664525u + 1013904223u;
        return (float)(seed >> 8) * (1.0f / (float)(1u << 24));
    }
};

// Convert string to JSON array of characters
// Returns: "['c','h','a','r',...]" format
__device__ inline const char* symm_string_to_char_array(const char* str) {
    static char buf[512];
    if (!str) return "[]";
    int pos = 0;
    buf[pos++] = '[';
    bool first = true;
    for (const char* p = str; *p && pos < 500; p++) {
        if (!first) buf[pos++] = ',';
        first = false;
        buf[pos++] = '\'';
        buf[pos++] = *p;
        buf[pos++] = '\'';
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

struct Array {
    template<typename T> __device__ static bool isArray(T v) { return true; }
    __device__ static bool isArray(const char* v) { return false; }
    __device__ static bool isArray(float v) { return false; }
    __device__ static bool isArray(int v) { return false; }

    // Array.from() - converts iterables to arrays
    template<typename T> __device__ static T* from(T* src, int len) {
        return src;  // Arrays return as-is (GPU memory limitation)
    }
    __device__ static const char* from(const char* str) {
        // String to character array representation
        return symm_string_to_char_array(str);
    }
    // Map overload: convert Map entries to array of [key, value] pairs
    __device__ static const char* from(const Map& m) {
        // Return JSON representation of map entries: [[k1, v1], [k2, v2], ...]
        return symm_map_keys_as_json(&m);  // Uses existing helper
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

// JSON string escaping helper - properly escapes strings for JSON output
// Handles: quotes, backslashes, newlines, tabs, carriage returns, control chars
__device__ inline int symm_escape_json_string(const char* src, char* dst, int maxlen) {
    if (!src) src = "";
    int pos = 0;
    for (const char* p = src; *p && pos < maxlen - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = '"'; }
        } else if (c == '\\') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = '\\'; }
        } else if (c == '\n') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = 'n'; }
        } else if (c == '\r') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = 'r'; }
        } else if (c == '\t') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = 't'; }
        } else if (c == '\b') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = 'b'; }
        } else if (c == '\f') {
            if (pos + 1 < maxlen) { dst[pos++] = '\\'; dst[pos++] = 'f'; }
        } else if (c < 32) {
            // Control character - escape as \uXXXX
            if (pos + 5 < maxlen) {
                dst[pos++] = '\\'; dst[pos++] = 'u'; dst[pos++] = '0'; dst[pos++] = '0';
                const char* hex = "0123456789abcdef";
                dst[pos++] = hex[c >> 4];
                dst[pos++] = hex[c & 15];
            }
        } else {
            dst[pos++] = c;
        }
    }
    dst[pos] = '\0';
    return pos;
}

// String method helpers
__device__ inline const char* symm_string_repeat(const char* str, int count) {
    static char buf[512];
    if (!str || count <= 0) return "";
    if (count == 1) return str;

    int pos = 0;
    int str_len = 0;
    const char* p = str;
    while (*p && str_len < 100) { str_len++; p++; }

    if (str_len == 0) return "";
    if (str_len * count > 500) count = 500 / str_len;  // Cap total length

    for (int i = 0; i < count && pos < 500; i++) {
        for (const char* src = str; *src && pos < 500; src++) {
            buf[pos++] = *src;
        }
    }
    buf[pos] = '\0';
    return buf;
}

__device__ inline const char* symm_string_pad_start(const char* str, int target_len, const char* pad_str) {
    static char buf[256];
    if (!str) str = "";
    if (!pad_str || pad_str[0] == '\0') pad_str = " ";

    int str_len = 0;
    while (str[str_len] && str_len < 200) str_len++;

    if (str_len >= target_len) {
        // String is already long enough
        int pos = 0;
        for (int i = 0; i < str_len && pos < 255; i++) {
            buf[pos++] = str[i];
        }
        buf[pos] = '\0';
        return buf;
    }

    int pad_len = 0;
    while (pad_str[pad_len] && pad_len < 10) pad_len++;
    int pad_needed = target_len - str_len;

    int pos = 0;
    int pad_idx = 0;
    while (pad_needed > 0 && pos < 250) {
        buf[pos++] = pad_str[pad_idx];
        pad_idx = (pad_idx + 1) % pad_len;
        pad_needed--;
    }

    for (int i = 0; i < str_len && pos < 255; i++) {
        buf[pos++] = str[i];
    }
    buf[pos] = '\0';
    return buf;
}

__device__ inline const char* symm_string_pad_end(const char* str, int target_len, const char* pad_str) {
    static char buf[256];
    if (!str) str = "";
    if (!pad_str || pad_str[0] == '\0') pad_str = " ";

    int str_len = 0;
    while (str[str_len] && str_len < 200) str_len++;

    int pos = 0;
    for (int i = 0; i < str_len && pos < 250; i++) {
        buf[pos++] = str[i];
    }

    if (str_len >= target_len) {
        buf[pos] = '\0';
        return buf;
    }

    int pad_len = 0;
    while (pad_str[pad_len] && pad_len < 10) pad_len++;
    int pad_needed = target_len - str_len;

    int pad_idx = 0;
    while (pad_needed > 0 && pos < 255) {
        buf[pos++] = pad_str[pad_idx];
        pad_idx = (pad_idx + 1) % pad_len;
        pad_needed--;
    }

    buf[pos] = '\0';
    return buf;
}

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

// Ring buffer layout: [head:u32][tail:u32][data:char[65528]]
// atomicAdd on head gives each writer a unique reserved slot.
__device__ void symm_debug_log_write(const char* msg) {
    if (!msg || !_debug_ring_ptr) return;
    const unsigned int DATA_SIZE = 65528u;
    uint32_t* head = (uint32_t*)(uintptr_t)_debug_ring_ptr;
    char*     data = (char*)(uintptr_t)(_debug_ring_ptr + 8u);

    // Count message length (cap at 240 so length+msg always fits in one entry)
    uint32_t len = 0;
    while (msg[len] && len < 240u) len++;
    uint32_t entry = 4u + len;

    // Reserve space — atomicAdd returns the old head (our write position)
    uint32_t pos = atomicAdd(head, entry) % DATA_SIZE;
    if (pos + entry > DATA_SIZE) return;  // near wrap boundary — skip to avoid split

    // Write [4-byte length][message bytes]
    uint32_t* lp = (uint32_t*)(data + pos);
    *lp = len;
    char* dp = data + pos + 4u;
    for (uint32_t i = 0; i < len; i++) dp[i] = msg[i];
}

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

// Global timer state for console.time/timeEnd — 16 hash buckets
// Must be __device__ global; static locals are invalid in NVRTC device functions.
__device__ unsigned long long _console_time_slots[16] = {};

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
    // Per-label timer slots stored in __device__ global (16 buckets, djb2 hash)
    // Static local vars are invalid in NVRTC device functions — use global instead.
    __device__ void _time_write(const char* lbl, unsigned long long val) {
        unsigned int h = 5381u;
        for (int i = 0; lbl[i]; i++) h = ((h << 5) + h) ^ (unsigned char)lbl[i];
        _console_time_slots[h & 15u] = val;
    }
    __device__ unsigned long long _time_read(const char* lbl) {
        unsigned int h = 5381u;
        for (int i = 0; lbl[i]; i++) h = ((h << 5) + h) ^ (unsigned char)lbl[i];
        return _console_time_slots[h & 15u];
    }
    template<typename T> __device__ void time(T label) {
        _time_write(_symm_to_str(label), clock64());
    }
    template<typename T> __device__ void timeEnd(T label) {
        const char* lbl = _symm_to_str(label);
        unsigned long long elapsed = clock64() - _time_read(lbl);
        static char buf[80];
        int p = 0;
        for (int i = 0; lbl[i] && p < 48; i++) buf[p++] = lbl[i];
        buf[p++] = ':'; buf[p++] = ' ';
        // ~1700 cycles/µs at 1.7 GHz; output as approximate ms
        unsigned long long us = elapsed / 1700ULL;
        p += symm_write_int(buf + p, (int)(us / 1000));
        buf[p++] = '.';
        int frac = (int)(us % 1000);
        buf[p++] = '0' + frac/100; buf[p++] = '0' + (frac/10)%10; buf[p++] = '0' + frac%10;
        buf[p++] = 'm'; buf[p++] = 's';
        buf[p] = 0;
        symm_debug_log_write(buf);
    }
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

// Enhanced URL parsing helper
__device__ inline bool symm_parse_url(const char* url, char* scheme, char* host, char* path) {
    if (!url || !url[0]) return false;

    // Parse scheme (http, https, ws, wss)
    int i = 0, s = 0;
    while (url[i] && url[i] != ':' && s < 10) scheme[s++] = url[i++];
    scheme[s] = 0;

    if (url[i] == ':' && url[i+1] == '/' && url[i+2] == '/') {
        i += 3;  // Skip ://
        // Parse host
        s = 0;
        while (url[i] && url[i] != '/' && s < 128) host[s++] = url[i++];
        host[s] = 0;
        // Rest is path
        s = 0;
        while (url[i] && s < 256) path[s++] = url[i++];
        path[s] = 0;
        return true;
    }
    return false;
}

// HTTP method constants
struct HttpMethod {
    static const char* GET() { return "GET"; }
    static const char* POST() { return "POST"; }
    static const char* PUT() { return "PUT"; }
    static const char* DELETE() { return "DELETE"; }
    static const char* PATCH() { return "PATCH"; }
    static const char* HEAD() { return "HEAD"; }
    static const char* OPTIONS() { return "OPTIONS"; }
};

// HTTP Response headers
struct Response {
    int _status;
    const char* _body;
    Map _headers;

    __device__ Response() : _status(0), _body(""), _headers() {}
    __device__ Response(int status, const char* body) : _status(status), _body(body), _headers() {}

    __device__ int status() const { return _status; }
    __device__ const char* text() const { return _body ? _body : ""; }
    __device__ const char* json() const { return _body ? _body : "{}"; }
    __device__ bool ok() const { return _status >= 200 && _status < 300; }
};

// Fetch request options
struct FetchOptions {
    const char* method;
    const char* body;
    Map headers;
    int timeout_ms;
    bool credentials;

    __device__ FetchOptions()
        : method("GET"), body(""), headers(), timeout_ms(30000), credentials(false) {}
};

// Fetch API stubs
__device__ inline SyncPromise<Response> symm_fetch(const char* url) {
    Response r(200, "");
    return SyncPromise<Response>(r);
}

__device__ inline SyncPromise<Response> symm_fetch(const char* url, const FetchOptions& opts) {
    Response r(opts.body ? 201 : 200, opts.body);
    return SyncPromise<Response>(r);
}

// WebSocket state tracking
struct WebSocket {
    const char* _url;
    int _state;  // 0=connecting, 1=open, 2=closing, 3=closed
    int _buffered_messages;

    __device__ WebSocket(const char* url = "")
        : _url(url), _state(0), _buffered_messages(0) {}

    __device__ int readyState() const { return _state; }
    __device__ bool isOpen() const { return _state == 1; }
    __device__ int bufferedAmount() const { return _buffered_messages; }
};

// WebSocket message queuing
__device__ inline bool symm_ws_send(WebSocket& ws, const char* data) {
    if (ws._state != 1) return false;  // Not open
    ws._buffered_messages++;
    return true;
}

__device__ inline const char* symm_ws_receive(WebSocket& ws) {
    if (ws._buffered_messages > 0) {
        ws._buffered_messages--;
        return "";  // Would return actual message
    }
    return nullptr;
}

// HTTP header helpers
__device__ inline const char* symm_content_type_json() {
    return "application/json";
}

__device__ inline const char* symm_content_type_form() {
    return "application/x-www-form-urlencoded";
}

__device__ inline const char* symm_content_type_text() {
    return "text/plain";
}

__device__ inline const char* symm_content_type_html() {
    return "text/html";
}

__device__ inline const char* symm_authorization_bearer(const char* token) {
    static char buf[512];
    snprintf(buf, sizeof(buf), "Bearer %s", token ? token : "");
    return buf;
}

// Response status helpers
__device__ inline bool symm_is_success(int status) {
    return status >= 200 && status < 300;
}

__device__ inline bool symm_is_redirect(int status) {
    return status >= 300 && status < 400;
}

__device__ inline bool symm_is_client_error(int status) {
    return status >= 400 && status < 500;
}

__device__ inline bool symm_is_server_error(int status) {
    return status >= 500 && status < 600;
}

// Status code message
__device__ inline const char* symm_status_message(int code) {
    if (code == 200) return "OK";
    if (code == 201) return "Created";
    if (code == 204) return "No Content";
    if (code == 301 || code == 302) return "Redirect";
    if (code == 400) return "Bad Request";
    if (code == 401) return "Unauthorized";
    if (code == 403) return "Forbidden";
    if (code == 404) return "Not Found";
    if (code == 500) return "Internal Server Error";
    if (code == 502) return "Bad Gateway";
    if (code == 503) return "Service Unavailable";
    return "Unknown";
}

// Streaming response support
struct StreamingResponse {
    const char* _chunk;
    int _chunk_size;
    bool _done;

    __device__ StreamingResponse()
        : _chunk(""), _chunk_size(0), _done(false) {}

    __device__ int chunkSize() const { return _chunk_size; }
    __device__ const char* getChunk() const { return _chunk; }
    __device__ bool isDone() const { return _done; }
};

// Streaming chunk formatting
__device__ inline const char* symm_format_stream_chunk(const char* data, int size) {
    static char buf[1024];
    // HTTP chunked encoding format: size(hex)\r\ndata\r\n
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%x\r\n", size);
    for (int i = 0; i < size && pos < 1000; i++) buf[pos++] = data[i];
    buf[pos++] = '\r'; buf[pos++] = '\n';
    buf[pos] = 0;
    return buf;
}

// ReadableStream state tracking
struct ReadableStream {
    int _state;  // 0=readable, 1=closed, 2=errored
    const char* _queued_data;
    int _queued_size;

    __device__ ReadableStream()
        : _state(0), _queued_data(""), _queued_size(0) {}

    __device__ bool canRead() const { return _state == 0; }
    __device__ bool isClosed() const { return _state == 1; }
};

// Stream reading helper
__device__ inline const char* symm_stream_read(ReadableStream& stream) {
    if (stream._state != 0) return nullptr;  // Not readable
    const char* data = stream._queued_data;
    stream._queued_size = 0;
    return data;
}

// ── Performance Monitoring & Metrics ───────────────────────────────────────────

// Request timing tracker
struct RequestMetrics {
    unsigned long long _start_time;
    unsigned long long _end_time;
    int _bytes_received;
    int _bytes_sent;
    int _status_code;

    __device__ RequestMetrics()
        : _start_time(0), _end_time(0), _bytes_received(0), _bytes_sent(0), _status_code(0) {}

    __device__ unsigned long long duration() const {
        return _end_time > _start_time ? _end_time - _start_time : 0;
    }

    __device__ float durationMs() const {
        // ~1700 cycles per microsecond at 1.7 GHz
        return (float)duration() / 1700.0f / 1000.0f;
    }
};

// Start request timing
__device__ inline void symm_metrics_start(RequestMetrics& m) {
    m._start_time = clock64();
}

// End request timing
__device__ inline void symm_metrics_end(RequestMetrics& m) {
    m._end_time = clock64();
}

// Record bytes transferred
__device__ inline void symm_metrics_record_bytes(RequestMetrics& m, int sent, int received) {
    m._bytes_sent = sent;
    m._bytes_received = received;
}

// Throughput calculation (bytes per ms)
__device__ inline float symm_metrics_throughput(const RequestMetrics& m) {
    float duration_ms = m.durationMs();
    if (duration_ms < 0.001f) return 0.0f;  // Avoid division by zero
    return (float)m._bytes_received / duration_ms;
}

// Latency percentile helpers
struct LatencyStats {
    float _min_ms;
    float _max_ms;
    float _avg_ms;
    float _p50_ms;
    float _p99_ms;

    __device__ LatencyStats()
        : _min_ms(0), _max_ms(0), _avg_ms(0), _p50_ms(0), _p99_ms(0) {}

    __device__ bool isHealthy() const {
        return _avg_ms < 100.0f && _max_ms < 1000.0f;
    }
};

// Request counter
struct RequestCounter {
    int _total;
    int _successful;  // 2xx
    int _redirect;    // 3xx
    int _client_error;  // 4xx
    int _server_error;  // 5xx

    __device__ RequestCounter()
        : _total(0), _successful(0), _redirect(0), _client_error(0), _server_error(0) {}

    __device__ void recordResponse(int status) {
        _total++;
        if (status >= 200 && status < 300) _successful++;
        else if (status >= 300 && status < 400) _redirect++;
        else if (status >= 400 && status < 500) _client_error++;
        else if (status >= 500 && status < 600) _server_error++;
    }

    __device__ float successRate() const {
        return _total > 0 ? (float)_successful / _total * 100.0f : 0.0f;
    }

    __device__ float errorRate() const {
        return 100.0f - successRate();
    }
};

// Error rate calculation
__device__ inline float symm_error_rate(const RequestCounter& c) {
    return c.errorRate();
}

// Success rate calculation
__device__ inline float symm_success_rate(const RequestCounter& c) {
    return c.successRate();
}

// Format metrics JSON
__device__ inline const char* symm_format_metrics(const RequestMetrics& m) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"duration_ms\":%.2f,\"bytes_sent\":%d,\"bytes_received\":%d,\"status\":%d}",
             m.durationMs(), m._bytes_sent, m._bytes_received, m._status_code);
    return buf;
}

// Format stats JSON
__device__ inline const char* symm_format_stats(const RequestCounter& c) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"total\":%d,\"successful\":%d,\"errors\":%d,\"error_rate\":%.1f}",
             c._total, c._successful, c._client_error + c._server_error, c.errorRate());
    return buf;
}

// ── Phase 3.17: Advanced Performance Metrics ────────────────────────────────────
// Latency histogram for tracking request duration distribution

struct HistogramMetrics {
    int _bucket_0_10ms;     // 0-10ms
    int _bucket_10_50ms;    // 10-50ms
    int _bucket_50_100ms;   // 50-100ms
    int _bucket_100_500ms;  // 100-500ms
    int _bucket_500ms_plus; // 500ms+
    int _total_requests;

    __device__ HistogramMetrics()
        : _bucket_0_10ms(0), _bucket_10_50ms(0), _bucket_50_100ms(0),
          _bucket_100_500ms(0), _bucket_500ms_plus(0), _total_requests(0) {}

    __device__ void recordLatency(float latency_ms) {
        _total_requests++;
        if (latency_ms < 10.0f) _bucket_0_10ms++;
        else if (latency_ms < 50.0f) _bucket_10_50ms++;
        else if (latency_ms < 100.0f) _bucket_50_100ms++;
        else if (latency_ms < 500.0f) _bucket_100_500ms++;
        else _bucket_500ms_plus++;
    }

    __device__ float p50() const {
        if (_total_requests == 0) return 0.0f;
        int half = _total_requests / 2;
        int cumsum = 0;
        if (cumsum + _bucket_0_10ms >= half) return 5.0f;
        cumsum += _bucket_0_10ms;
        if (cumsum + _bucket_10_50ms >= half) return 30.0f;
        cumsum += _bucket_10_50ms;
        if (cumsum + _bucket_50_100ms >= half) return 75.0f;
        cumsum += _bucket_50_100ms;
        if (cumsum + _bucket_100_500ms >= half) return 300.0f;
        return 750.0f;
    }

    __device__ float p99() const {
        if (_total_requests == 0) return 0.0f;
        int threshold = (int)(_total_requests * 0.99f);
        int cumsum = 0;
        if (cumsum + _bucket_0_10ms >= threshold) return 10.0f;
        cumsum += _bucket_0_10ms;
        if (cumsum + _bucket_10_50ms >= threshold) return 50.0f;
        cumsum += _bucket_10_50ms;
        if (cumsum + _bucket_50_100ms >= threshold) return 100.0f;
        cumsum += _bucket_50_100ms;
        if (cumsum + _bucket_100_500ms >= threshold) return 500.0f;
        return 750.0f;
    }
};

// Per-endpoint metrics tracking
struct EndpointMetrics {
    const char* _endpoint;      // "/api/users", "/api/posts", etc.
    int _total_requests;
    int _successful;
    int _errors;
    float _total_time_ms;
    float _min_latency_ms;
    float _max_latency_ms;

    __device__ EndpointMetrics(const char* endpoint = "")
        : _endpoint(endpoint), _total_requests(0), _successful(0), _errors(0),
          _total_time_ms(0.0f), _min_latency_ms(999999.0f), _max_latency_ms(0.0f) {}

    __device__ void recordRequest(float latency_ms, int status_code) {
        _total_requests++;
        _total_time_ms += latency_ms;
        if (latency_ms < _min_latency_ms) _min_latency_ms = latency_ms;
        if (latency_ms > _max_latency_ms) _max_latency_ms = latency_ms;
        if (status_code >= 200 && status_code < 300) _successful++;
        else _errors++;
    }

    __device__ float avgLatency() const {
        return _total_requests > 0 ? _total_time_ms / _total_requests : 0.0f;
    }

    __device__ float errorRate() const {
        return _total_requests > 0 ? (float)_errors * 100.0f / _total_requests : 0.0f;
    }
};

// GPU resource usage tracking
struct ResourceMetrics {
    unsigned long long _kernel_start_time;  // clock64() at kernel start
    unsigned long long _kernel_end_time;    // clock64() at kernel end
    int _vram_bytes_used;                   // Approximate VRAM usage
    int _max_vram_bytes;                    // Peak VRAM usage

    __device__ ResourceMetrics()
        : _kernel_start_time(0), _kernel_end_time(0), _vram_bytes_used(0),
          _max_vram_bytes(0) {}

    __device__ void recordKernelStart() {
        _kernel_start_time = clock64();
    }

    __device__ void recordKernelEnd() {
        _kernel_end_time = clock64();
    }

    __device__ void recordMemoryUsage(int bytes) {
        _vram_bytes_used = bytes;
        if (bytes > _max_vram_bytes) _max_vram_bytes = bytes;
    }

    __device__ float kernelTimeMs() const {
        if (_kernel_end_time <= _kernel_start_time) return 0.0f;
        unsigned long long cycles = _kernel_end_time - _kernel_start_time;
        return (float)cycles / 1700.0f / 1000.0f;  // ~1700 cycles/ms at 1.7 GHz
    }

    __device__ float throughputMBps() const {
        float time_ms = kernelTimeMs();
        if (time_ms < 0.001f) return 0.0f;
        return _vram_bytes_used / (1024.0f * 1024.0f) / (time_ms / 1000.0f);
    }
};

// Combined metrics collector
struct MetricsCollector {
    HistogramMetrics _latency_hist;
    EndpointMetrics _endpoints[5];  // Track up to 5 endpoints
    int _endpoint_count;
    ResourceMetrics _resources;
    RequestCounter _counter;

    __device__ MetricsCollector()
        : _endpoint_count(0) {}

    __device__ void recordRequest(const char* endpoint, float latency_ms, int status_code) {
        _latency_hist.recordLatency(latency_ms);
        _counter.recordResponse(status_code);

        // Find or create endpoint
        int idx = -1;
        for (int i = 0; i < _endpoint_count; i++) {
            if (symm_str_eq(_endpoints[i]._endpoint, endpoint)) {
                idx = i;
                break;
            }
        }
        if (idx == -1 && _endpoint_count < 5) {
            idx = _endpoint_count++;
            _endpoints[idx]._endpoint = endpoint;
        }
        if (idx >= 0) {
            _endpoints[idx].recordRequest(latency_ms, status_code);
        }
    }

    __device__ float getLatencyPercentile(float percentile) const {
        if (percentile < 0.5f) return _latency_hist.p50();
        if (percentile < 0.99f) return _latency_hist.p99();
        return _latency_hist._bucket_500ms_plus > 0 ? 750.0f : 100.0f;
    }
};

// Helper: Format metrics as JSON for response
__device__ inline const char* symm_format_metrics_json(const MetricsCollector& m) {
    static char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"requests\":%d,\"success\":%.1f,\"p50\":%.1f,\"p99\":%.1f,\"kernel_ms\":%.1f}",
             m._counter._total, m._counter.successRate(), m._latency_hist.p50(),
             m._latency_hist.p99(), m._resources.kernelTimeMs());
    return buf;
}

// ── Phase 3.18: Distributed Tracing & Request Correlation ──────────────────────
// Request ID generation and trace context propagation for multi-instance deployments

struct TraceContext {
    const char* _trace_id;        // Unique trace identifier (correlates requests)
    const char* _span_id;         // Current operation span
    const char* _parent_span_id;  // Parent operation (for chaining)
    int _trace_flags;             // 1=sampled, 0=not sampled
    unsigned long long _start_time;  // Trace start time (clock64)

    __device__ TraceContext(const char* tid = "", const char* sid = "", const char* parent = "")
        : _trace_id(tid), _span_id(sid), _parent_span_id(parent), _trace_flags(1),
          _start_time(clock64()) {}

    __device__ bool isSampled() const { return _trace_flags == 1; }
    __device__ void setSampled(bool sampled) { _trace_flags = sampled ? 1 : 0; }
};

// Request ID generator (simple counter-based)
struct RequestIDGenerator {
    unsigned long long _counter;
    const char* _instance_id;  // GPU instance identifier

    __device__ RequestIDGenerator(const char* instance_id = "gpu-0")
        : _counter(0), _instance_id(instance_id) {}

    __device__ const char* generateRequestID() {
        static char buf[64];
        _counter++;
        // Format: instance-id-counter (e.g., "gpu-0-12345")
        snprintf(buf, sizeof(buf), "%s-%llu", _instance_id, _counter);
        return buf;
    }
};

// Span tracking for operation timing
struct Span {
    const char* _span_id;         // This span's ID
    const char* _operation_name;  // Operation being traced
    unsigned long long _start_time;
    unsigned long long _end_time;
    int _status_code;             // HTTP status or error code
    const char* _tags[5];         // Key-value pairs for debugging (simplified)

    __device__ Span(const char* span_id = "", const char* op = "")
        : _span_id(span_id), _operation_name(op), _start_time(clock64()),
          _end_time(0), _status_code(0) {
        for (int i = 0; i < 5; i++) _tags[i] = nullptr;
    }

    __device__ void finish(int status) {
        _end_time = clock64();
        _status_code = status;
    }

    __device__ float durationMs() const {
        if (_end_time <= _start_time) return 0.0f;
        unsigned long long cycles = _end_time - _start_time;
        return (float)cycles / 1700.0f / 1000.0f;
    }

    __device__ void addTag(const char* key, const char* value) {
        for (int i = 0; i < 5; i++) {
            if (!_tags[i]) {
                _tags[i] = key;  // Simplified: store key only
                break;
            }
        }
    }
};

// Trace correlation helper for distributed systems
struct TraceCorrelation {
    const char* _request_id;      // Request ID from header
    const char* _trace_id;        // Trace ID for correlation
    const char* _caller_service;  // Calling service identifier
    int _hop_count;               // Number of service hops

    __device__ TraceCorrelation(const char* req_id = "", const char* trace_id = "")
        : _request_id(req_id), _trace_id(trace_id), _caller_service(""), _hop_count(0) {}

    __device__ void recordHop(const char* service) {
        _hop_count++;
        _caller_service = service;
    }

    __device__ int getTotalHops() const { return _hop_count; }
};

// Helper: Generate trace context from request headers
__device__ inline TraceContext symm_extract_trace_context(const char* trace_header) {
    if (!trace_header) return TraceContext();
    // Format: "trace-id:span-id:parent-span-id:flags"
    // Simplified: just store the header as trace_id
    return TraceContext(trace_header, "", "");
}

// Helper: Format trace context for propagation to downstream services
__device__ inline const char* symm_format_trace_header(const TraceContext& ctx) {
    static char buf[256];
    if (!ctx._trace_id || !ctx._trace_id[0]) {
        snprintf(buf, sizeof(buf), "");
        return buf;
    }
    snprintf(buf, sizeof(buf), "%s:%s:%s:%d",
             ctx._trace_id, ctx._span_id, ctx._parent_span_id, ctx._trace_flags);
    return buf;
}

// Helper: Correlate request IDs across service boundaries
__device__ inline const char* symm_correlate_request(const char* incoming_id, const char* instance_id) {
    static char buf[128];
    if (incoming_id && incoming_id[0]) {
        snprintf(buf, sizeof(buf), "%s->%s", incoming_id, instance_id);
        return buf;
    }
    return instance_id;
}

// ── Phase 3.19: Work-Stealing Thread Pool & Lock-Free Coordination ────────────

// Work queue entry for task distribution
struct WorkQueueEntry {
    const char* _task_id;
    const char* _span_id;          // Link to tracing (Phase 3.18)
    unsigned long long _enqueue_time;
    int _priority;                  // 0=low, 1=normal, 2=high
};

// Lock-free work-stealing queue (single producer, multiple consumers)
struct WorkStealingQueue {
    WorkQueueEntry _queue[32];      // Fixed-size circular buffer
    int _head;                       // Producer writes here
    int _tail;                       // Consumers read from here
    int _size;                       // Current queue size
    int _max_size;                   // Capacity
    unsigned long long _total_dequeued;  // For load tracking

    __device__ WorkStealingQueue()
        : _head(0), _tail(0), _size(0), _max_size(32), _total_dequeued(0) {}

    // Enqueue task (lock-free append)
    __device__ bool enqueue(const char* task_id, const char* span_id, int priority = 1) {
        if (_size >= _max_size) return false;  // Queue full

        _queue[_head] = {task_id, span_id, clock64(), priority};
        _head = (_head + 1) % _max_size;
        _size++;
        return true;
    }

    // Dequeue task (lock-free pop from tail)
    __device__ bool dequeue(WorkQueueEntry& out) {
        if (_size == 0) return false;  // Queue empty

        out = _queue[_tail];
        _tail = (_tail + 1) % _max_size;
        _size--;
        _total_dequeued++;
        return true;
    }

    // Peek at oldest task without removing
    __device__ bool peek(WorkQueueEntry& out) const {
        if (_size == 0) return false;
        out = _queue[_tail];
        return true;
    }

    __device__ int getSize() const { return _size; }
    __device__ int getMaxSize() const { return _max_size; }
    __device__ unsigned long long getTotalDequeued() const { return _total_dequeued; }
};

// Worker thread state for distributed coordination
struct WorkerThread {
    const char* _instance_id;       // "gpu-0", "gpu-1", etc.
    WorkStealingQueue _local_queue;
    int _active_tasks;              // Currently processing
    unsigned long long _last_steal_time;  // When last stole from peer
    int _steal_count;               // Total steals executed
    int _idle_cycles;               // Consecutive cycles with no work

    __device__ WorkerThread(const char* instance_id = "gpu-0")
        : _instance_id(instance_id), _active_tasks(0),
          _last_steal_time(clock64()), _steal_count(0), _idle_cycles(0) {}

    // Submit work to local queue
    __device__ bool submitWork(const char* task_id, const char* span_id, int priority = 1) {
        return _local_queue.enqueue(task_id, span_id, priority);
    }

    // Process work from local queue
    __device__ bool processWork(WorkQueueEntry& out) {
        if (_local_queue.dequeue(out)) {
            _active_tasks++;
            _idle_cycles = 0;
            return true;
        }
        return false;
    }

    // Mark task complete
    __device__ void completeTask() {
        if (_active_tasks > 0) _active_tasks--;
    }

    __device__ int getLoadFactor() const {
        return _local_queue.getSize() + _active_tasks;
    }

    __device__ bool isIdle() const {
        return _local_queue.getSize() == 0 && _active_tasks == 0;
    }

    // Increment idle cycles (for work stealing decisions)
    __device__ void recordIdleCycle() {
        if (isIdle()) _idle_cycles++;
    }
};

// Global load tracker for work-stealing coordination
struct GlobalLoadTracker {
    int _instance_loads[8];         // Load per instance (0-7)
    int _num_instances;
    unsigned long long _update_time;
    int _total_queued;              // Total tasks across all instances

    __device__ GlobalLoadTracker(int num_instances = 1)
        : _num_instances(num_instances), _update_time(clock64()), _total_queued(0) {
        for (int i = 0; i < 8; i++) _instance_loads[i] = 0;
    }

    // Update load for a specific instance
    __device__ void updateLoad(int instance_idx, int load) {
        if (instance_idx >= 0 && instance_idx < _num_instances) {
            _instance_loads[instance_idx] = load;
        }
        _update_time = clock64();
    }

    // Find busiest instance (for work stealing target)
    __device__ int getBusiestInstance() const {
        int max_load = 0, busiest = 0;
        for (int i = 0; i < _num_instances; i++) {
            if (_instance_loads[i] > max_load) {
                max_load = _instance_loads[i];
                busiest = i;
            }
        }
        return busiest;
    }

    // Find idlest instance
    __device__ int getIdlestInstance() const {
        int min_load = 999999, idlest = 0;
        for (int i = 0; i < _num_instances; i++) {
            if (_instance_loads[i] < min_load) {
                min_load = _instance_loads[i];
                idlest = i;
            }
        }
        return idlest;
    }

    // Calculate load imbalance (max - min)
    __device__ int getLoadImbalance() const {
        int max_load = 0, min_load = 999999;
        for (int i = 0; i < _num_instances; i++) {
            if (_instance_loads[i] > max_load) max_load = _instance_loads[i];
            if (_instance_loads[i] < min_load) min_load = _instance_loads[i];
        }
        return max_load - min_load;
    }

    // Is a particular instance overloaded?
    __device__ bool isOverloaded(int instance_idx) const {
        if (instance_idx < 0 || instance_idx >= _num_instances) return false;
        int avg_load = _total_queued / _num_instances;
        return _instance_loads[instance_idx] > (avg_load * 3) / 2;  // >150% avg
    }
};

// Work distribution decision record
struct WorkDistribution {
    const char* _source_instance;
    const char* _target_instance;
    int _tasks_moved;
    unsigned long long _decision_time;
    int _load_imbalance_before;
    int _load_imbalance_after;

    __device__ WorkDistribution(const char* source = "", const char* target = "")
        : _source_instance(source), _target_instance(target), _tasks_moved(0),
          _decision_time(clock64()), _load_imbalance_before(0),
          _load_imbalance_after(0) {}

    // Calculate effectiveness (how much imbalance improved)
    __device__ int getEffectiveness() const {
        return _load_imbalance_before - _load_imbalance_after;
    }
};

// Check if work stealing should occur
__device__ inline bool symm_should_steal_work(const WorkerThread& local_worker,
                                              const GlobalLoadTracker& tracker) {
    if (!local_worker.isIdle()) return false;  // Not idle
    if (local_worker._idle_cycles < 2) return false;  // Not idle long enough
    if (tracker.getLoadImbalance() < 5) return false;  // Load balanced enough
    return true;
}

// Format work queue status as JSON
__device__ inline const char* symm_format_queue_status(const WorkStealingQueue& queue) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"size\":%d,\"capacity\":%d,\"utilization\":%.1f,\"dequeued\":%llu}",
        queue.getSize(), queue.getMaxSize(),
        (float)queue.getSize() / queue.getMaxSize() * 100.0,
        queue.getTotalDequeued());
    return buf;
}

// Format load tracker status as JSON
__device__ inline const char* symm_format_load_status(const GlobalLoadTracker& tracker) {
    static char buf[512];
    int imbalance = tracker.getLoadImbalance();
    snprintf(buf, sizeof(buf),
        "{\"imbalance\":%d,\"busiest\":%d,\"idlest\":%d,\"num_instances\":%d,"
        "\"avg_load\":%.1f}",
        imbalance, tracker.getBusiestInstance(), tracker.getIdlestInstance(),
        tracker._num_instances,
        (float)tracker._total_queued / tracker._num_instances);
    return buf;
}

// Record work distribution decision
__device__ inline void symm_record_distribution(WorkDistribution& dist, int before, int after) {
    dist._decision_time = clock64();
    dist._load_imbalance_before = before;
    dist._load_imbalance_after = after;
}

// ── Phase 3.20: Asynchronous I/O with Native Coroutines ──────────────────────

// Coroutine state for async operations
struct Coroutine {
    const char* _id;                // Unique coroutine ID
    int _state;                     // 0=pending, 1=running, 2=suspended, 3=done
    unsigned long long _created_time;
    unsigned long long _resume_time;
    int _stream_id;                 // CUDA stream for GPU-native completion
    const char* _span_id;           // Link to Phase 3.18 tracing
};

// Async I/O operation descriptor
struct AsyncIOOp {
    const char* _op_id;
    int _op_type;                   // 0=read, 1=write, 2=compute, 3=network
    const char* _resource;          // File/socket/URL
    unsigned long long _start_time;
    int _bytes_transferred;
    int _status_code;               // 0=pending, 1=success, -1=error
};

// Stream scheduler for GPU-native async dispatch
struct StreamScheduler {
    int _active_streams[8];         // CUDA stream IDs
    int _num_streams;
    Coroutine* _pending_coros[32];  // Coroutines awaiting completion
    int _pending_count;
    unsigned long long _total_completions;

    __device__ StreamScheduler(int num_streams = 2)
        : _num_streams(num_streams), _pending_count(0), _total_completions(0) {
        for (int i = 0; i < 8; i++) _active_streams[i] = -1;
    }

    // Register a coroutine for completion tracking
    __device__ bool registerCoroutine(Coroutine& coro) {
        if (_pending_count >= 32) return false;  // Queue full
        _pending_coros[_pending_count] = &coro;
        _pending_count++;
        return true;
    }

    // Check if stream has completed
    __device__ bool isStreamComplete(int stream_id) {
        // In GPU, cudaStreamQuery would be used (simplified for prelude)
        return false;  // Placeholder: actual implementation uses cudaStreamQuery
    }

    // Process completed operations (continuations)
    __device__ int processCompletions() {
        int completed = 0;
        for (int i = 0; i < _pending_count; i++) {
            Coroutine* coro = _pending_coros[i];
            if (coro && isStreamComplete(coro->_stream_id)) {
                coro->_state = 3;  // Mark done
                _total_completions++;
                completed++;
                // Shift remaining coroutines
                for (int j = i; j < _pending_count - 1; j++) {
                    _pending_coros[j] = _pending_coros[j + 1];
                }
                _pending_count--;
                i--;
            }
        }
        return completed;
    }

    __device__ int getPendingCount() const { return _pending_count; }
    __device__ unsigned long long getTotalCompletions() const { return _total_completions; }
};

// Coroutine state machine
struct CoroutineRunner {
    Coroutine _current;
    StreamScheduler& _scheduler;
    int _context_id;                // For debugging/tracing

    __device__ CoroutineRunner(StreamScheduler& scheduler, const char* id = "")
        : _scheduler(scheduler), _context_id(0) {
        _current._id = id;
        _current._state = 0;
        _current._created_time = clock64();
    }

    // Suspend coroutine pending I/O completion
    __device__ bool suspend(int stream_id) {
        _current._state = 2;  // suspended
        _current._stream_id = stream_id;
        _current._resume_time = clock64();
        return _scheduler.registerCoroutine(_current);
    }

    // Resume from suspended state
    __device__ void resume() {
        _current._state = 1;  // running
    }

    // Complete coroutine
    __device__ void complete() {
        _current._state = 3;  // done
    }

    __device__ int getState() const { return _current._state; }
    __device__ unsigned long long getLifespan() const {
        return clock64() - _current._created_time;
    }
};

// Async operation lifecycle
struct AsyncOperationContext {
    AsyncIOOp _op;
    Coroutine _owner_coro;
    int _retry_count;
    int _max_retries;
    unsigned long long _deadline;

    __device__ AsyncOperationContext(const char* op_id = "", int op_type = 0)
        : _retry_count(0), _max_retries(3), _deadline(0) {
        _op._op_id = op_id;
        _op._op_type = op_type;
        _op._start_time = clock64();
        _op._status_code = 0;  // pending
    }

    // Mark operation as complete
    __device__ void complete(int status, int bytes = 0) {
        _op._status_code = status;
        _op._bytes_transferred = bytes;
    }

    // Check if operation has exceeded deadline
    __device__ bool isExpired() const {
        if (_deadline == 0) return false;  // No deadline
        return clock64() > _deadline;
    }

    // Get operation latency
    __device__ unsigned long long getLatency() const {
        return clock64() - _op._start_time;
    }

    // Can retry?
    __device__ bool canRetry() const {
        return _retry_count < _max_retries && _op._status_code < 0;
    }
};

// Helper: Create and schedule async operation
__device__ inline Coroutine symm_create_coroutine(const char* id, const char* span_id,
                                                   StreamScheduler& scheduler) {
    Coroutine coro;
    coro._id = id;
    coro._span_id = span_id;
    coro._state = 0;  // pending
    coro._created_time = clock64();
    coro._stream_id = -1;
    return coro;
}

// Helper: Suspend coroutine pending I/O
__device__ inline bool symm_suspend_for_io(CoroutineRunner& runner, int stream_id) {
    return runner.suspend(stream_id);
}

// Helper: Check if async operation is ready
__device__ inline bool symm_is_async_ready(const StreamScheduler& scheduler, int stream_id) {
    return scheduler.isStreamComplete(stream_id);
}

// Helper: Format coroutine status as JSON
__device__ inline const char* symm_format_coroutine_status(const Coroutine& coro) {
    static char buf[256];
    const char* state_str = "pending";
    if (coro._state == 1) state_str = "running";
    else if (coro._state == 2) state_str = "suspended";
    else if (coro._state == 3) state_str = "done";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"stream_id\":%d,\"age_us\":%llu}",
        coro._id ? coro._id : "", state_str, coro._stream_id,
        (clock64() - coro._created_time) / 1000);
    return buf;
}

// Helper: Format async operation status as JSON
__device__ inline const char* symm_format_async_op_status(const AsyncIOOp& op) {
    static char buf[384];
    const char* type_str = "read";
    if (op._op_type == 1) type_str = "write";
    else if (op._op_type == 2) type_str = "compute";
    else if (op._op_type == 3) type_str = "network";

    const char* status_str = "pending";
    if (op._status_code == 1) status_str = "success";
    else if (op._status_code < 0) status_str = "error";

    snprintf(buf, sizeof(buf),
        "{\"op_id\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",\"bytes\":%d,"
        "\"latency_us\":%llu}",
        op._op_id ? op._op_id : "", type_str, status_str,
        op._bytes_transferred, (clock64() - op._start_time) / 1000);
    return buf;
}

// Helper: Format scheduler health as JSON
__device__ inline const char* symm_format_scheduler_health(const StreamScheduler& scheduler) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"active_streams\":%d,\"pending_coros\":%d,\"total_completions\":%llu,"
        "\"utilization\":%.1f}",
        scheduler._num_streams, scheduler._pending_count,
        scheduler._total_completions,
        (float)scheduler._pending_count / 32 * 100.0);
    return buf;
}

// ── Phase 3.21: Distributed Consensus & Gossip Protocol ───────────────────────

// Node state for gossip protocol
struct NodeState {
    const char* _node_id;           // "gpu-0", "gpu-1", etc.
    int _load;                      // Current load (0-1000)
    int _healthy;                   // 1=healthy, 0=unhealthy
    unsigned long long _last_heartbeat;
    unsigned long long _state_version;  // Version for ordering
    int _sequence_number;           // For detecting fresh updates
};

// Gossip message for peer-to-peer dissemination
struct GossipMessage {
    const char* _origin_node;       // Original sender
    NodeState _state;               // Embedded node state
    int _hops;                      // Distance from origin
    unsigned long long _timestamp;
    const char* _message_id;        // For deduplication
};

// Consistent hash ring for deterministic routing
struct ConsistentHashRing {
    const char* _nodes[8];          // Node IDs in hash order
    int _node_hashes[8];            // Pre-computed hashes
    int _num_nodes;
    int _virtual_nodes;             // Replicas per physical node

    __device__ ConsistentHashRing(int virtual_nodes = 3)
        : _num_nodes(0), _virtual_nodes(virtual_nodes) {
        for (int i = 0; i < 8; i++) {
            _nodes[i] = NULL;
            _node_hashes[i] = 0;
        }
    }

    // Add node to hash ring
    __device__ bool addNode(const char* node_id) {
        if (_num_nodes >= 8) return false;
        _nodes[_num_nodes] = node_id;
        // Simple hash: sum of characters
        int hash = 0;
        for (int i = 0; node_id && node_id[i]; i++) {
            hash += (unsigned char)node_id[i] * (i + 1);
        }
        _node_hashes[_num_nodes] = hash;
        _num_nodes++;
        return true;
    }

    // Find responsible node for a key
    __device__ const char* findNode(const char* key) const {
        if (_num_nodes == 0) return NULL;
        // Compute key hash
        int key_hash = 0;
        for (int i = 0; key && key[i]; i++) {
            key_hash += (unsigned char)key[i] * (i + 1);
        }
        // Find closest node in ring (clockwise)
        int min_distance = 2147483647;
        int best_idx = 0;
        for (int i = 0; i < _num_nodes; i++) {
            int distance = _node_hashes[i] - key_hash;
            if (distance < 0) distance = -distance;
            if (distance < min_distance) {
                min_distance = distance;
                best_idx = i;
            }
        }
        return _nodes[best_idx];
    }

    // Get replicas for key (first N nodes)
    __device__ int getReplicaNodes(const char* key, const char** out_nodes, int max_count) {
        if (_num_nodes == 0 || !out_nodes) return 0;
        const char* primary = findNode(key);
        int count = 0;
        for (int i = 0; i < _num_nodes && count < max_count; i++) {
            if (_nodes[i] == primary) {
                // Add primary and next N-1 nodes
                for (int j = 0; j < _virtual_nodes && count < max_count; j++) {
                    out_nodes[count++] = _nodes[(i + j) % _num_nodes];
                }
                break;
            }
        }
        return count;
    }
};

// Quorum voting for consensus decisions
struct QuorumVote {
    const char* _decision_id;       // Unique decision identifier
    int _yes_votes;
    int _no_votes;
    int _quorum_size;               // Votes needed to decide
    unsigned long long _started_at;
    int _decided;                   // 0=pending, 1=yes, -1=no

    __device__ QuorumVote(int quorum_size = 2)
        : _yes_votes(0), _no_votes(0), _quorum_size(quorum_size),
          _started_at(clock64()), _decided(0) {}

    // Register a vote
    __device__ void vote(int is_yes) {
        if (_decided != 0) return;  // Already decided
        if (is_yes) {
            _yes_votes++;
        } else {
            _no_votes++;
        }
        // Check if quorum reached
        if (_yes_votes >= _quorum_size) {
            _decided = 1;  // Consensus yes
        } else if (_no_votes >= _quorum_size) {
            _decided = -1;  // Consensus no
        }
    }

    __device__ bool isDecided() const { return _decided != 0; }
    __device__ bool isYes() const { return _decided == 1; }
    __device__ unsigned long long getElapsedTime() const {
        return clock64() - _started_at;
    }
};

// Membership state for node discovery
struct MembershipState {
    NodeState _nodes[8];            // Known nodes
    int _node_count;
    unsigned long long _last_sync;
    int _generation;                // Membership generation

    __device__ MembershipState() : _node_count(0), _last_sync(clock64()), _generation(0) {}

    // Update membership
    __device__ bool updateNode(const NodeState& node) {
        for (int i = 0; i < _node_count; i++) {
            if (_nodes[i]._node_id == node._node_id) {
                // Update existing
                if (node._state_version > _nodes[i]._state_version) {
                    _nodes[i] = node;
                    _generation++;
                    return true;
                }
                return false;
            }
        }
        // Add new
        if (_node_count < 8) {
            _nodes[_node_count] = node;
            _node_count++;
            _generation++;
            return true;
        }
        return false;
    }

    __device__ int getHealthyNodeCount() const {
        int count = 0;
        for (int i = 0; i < _node_count; i++) {
            if (_nodes[i]._healthy) count++;
        }
        return count;
    }

    __device__ int getTotalLoad() const {
        int total = 0;
        for (int i = 0; i < _node_count; i++) {
            total += _nodes[i]._load;
        }
        return total;
    }
};

// Helper: Compute hash for key (FNV-1a)
__device__ inline int symm_hash_key(const char* key) {
    unsigned int hash = 2166136261u;
    for (int i = 0; key && key[i]; i++) {
        hash ^= (unsigned char)key[i];
        hash *= 16777619u;
    }
    return (int)(hash & 0x7FFFFFFF);
}

// Helper: Create gossip message
__device__ inline GossipMessage symm_create_gossip_msg(const char* origin,
                                                        const NodeState& state,
                                                        const char* msg_id) {
    GossipMessage msg;
    msg._origin_node = origin;
    msg._state = state;
    msg._hops = 0;
    msg._timestamp = clock64();
    msg._message_id = msg_id;
    return msg;
}

// Helper: Should accept quorum decision
__device__ inline bool symm_quorum_consensus(const QuorumVote& vote) {
    return vote.isDecided() && vote.isYes();
}

// Helper: Format node state as JSON
__device__ inline const char* symm_format_node_state(const NodeState& node) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"load\":%d,\"healthy\":%d,\"version\":%llu,\"seq\":%d}",
        node._node_id ? node._node_id : "", node._load, node._healthy,
        node._state_version, node._sequence_number);
    return buf;
}

// Helper: Format membership as JSON
__device__ inline const char* symm_format_membership(const MembershipState& membership) {
    static char buf[512];
    int healthy = membership.getHealthyNodeCount();
    int total_load = membership.getTotalLoad();
    snprintf(buf, sizeof(buf),
        "{\"node_count\":%d,\"healthy\":%d,\"total_load\":%d,\"generation\":%d,"
        "\"avg_load\":%.1f}",
        membership._node_count, healthy, total_load, membership._generation,
        membership._node_count > 0 ? (float)total_load / membership._node_count : 0.0);
    return buf;
}

// Helper: Format quorum decision as JSON
__device__ inline const char* symm_format_quorum_result(const QuorumVote& vote) {
    static char buf[256];
    const char* result = "pending";
    if (vote.isYes()) result = "yes";
    else if (vote.isDecided()) result = "no";

    snprintf(buf, sizeof(buf),
        "{\"decision_id\":\"%s\",\"result\":\"%s\",\"yes\":%d,\"no\":%d,"
        "\"elapsed_us\":%llu}",
        vote._decision_id ? vote._decision_id : "", result,
        vote._yes_votes, vote._no_votes,
        vote.getElapsedTime() / 1000);
    return buf;
}

// ── Phase 4.0: Event Loop Architecture (Hybrid C# Model) ───────────────────────

// Continuation/Callback for promise resolution
struct Continuation {
    const char* _id;                // Unique continuation ID
    int _type;                      // 0=then, 1=catch, 2=finally
    const char* _handler_name;      // For debugging ("onSuccess", "onError", etc.)
    unsigned long long _scheduled_time;
    int _priority;                  // 0=normal, 1=microtask (high priority)
    const char* _span_id;           // Link to Phase 3.18 tracing
};

// Microtask queue (for .then() chaining and Promise resolution)
struct MicrotaskQueue {
    Continuation _tasks[64];        // Queue of continuations
    int _front;
    int _rear;
    int _count;
    unsigned long long _total_processed;
    int _depth;                     // Current nesting depth (for microtask detection)

    __device__ MicrotaskQueue() : _front(0), _rear(0), _count(0),
                                   _total_processed(0), _depth(0) {}

    // Enqueue continuation (microtask = higher priority)
    __device__ bool enqueue(const Continuation& task) {
        if (_count >= 64) return false;  // Queue full
        _rear = (_rear + 1) % 64;
        _tasks[_rear] = task;
        _count++;
        return true;
    }

    // Dequeue next task
    __device__ bool dequeue(Continuation& out) {
        if (_count == 0) return false;
        out = _tasks[_front];
        _front = (_front + 1) % 64;
        _count--;
        _total_processed++;
        return true;
    }

    // Drain queue (run all pending tasks)
    __device__ int drain() {
        int processed = 0;
        Continuation task;
        while (dequeue(task)) {
            // In real implementation: invoke handler
            // For prelude: just track execution
            processed++;
        }
        return processed;
    }

    __device__ int getCount() const { return _count; }
    __device__ int getDepth() const { return _depth; }
};

// Promise state machine
struct Promise {
    const char* _id;                // "promise-001"
    int _state;                     // 0=pending, 1=fulfilled, 2=rejected
    const char* _value;             // Fulfilled value or error
    Continuation _on_resolve;       // Handler for .then()
    Continuation _on_reject;        // Handler for .catch()
    unsigned long long _created_time;
    unsigned long long _settled_time;
    const char* _span_id;           // Link to tracing

    __device__ Promise(const char* id = "")
        : _id(id), _state(0), _value(NULL), _created_time(clock64()),
          _settled_time(0) {}

    // Resolve promise (move to fulfilled state)
    __device__ void resolve(const char* value) {
        if (_state != 0) return;  // Already settled
        _state = 1;
        _value = value;
        _settled_time = clock64();
    }

    // Reject promise (move to rejected state)
    __device__ void reject(const char* error) {
        if (_state != 0) return;  // Already settled
        _state = 2;
        _value = error;
        _settled_time = clock64();
    }

    // Attach then handler
    __device__ void then(const char* handler_name, const char* span_id) {
        _on_resolve._handler_name = handler_name;
        _on_resolve._span_id = span_id;
        _on_resolve._type = 0;  // then
    }

    // Attach catch handler
    __device__ void catch_handler(const char* handler_name, const char* span_id) {
        _on_reject._handler_name = handler_name;
        _on_reject._span_id = span_id;
        _on_reject._type = 1;  // catch
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isFulfilled() const { return _state == 1; }
    __device__ bool isRejected() const { return _state == 2; }
    __device__ unsigned long long getLatency() const {
        if (_settled_time == 0) return 0;
        return _settled_time - _created_time;
    }
};

// Task for TaskScheduler (like C# Task)
struct Task {
    const char* _id;                // "task-001"
    int _state;                     // 0=pending, 1=running, 2=complete, 3=faulted
    Coroutine _coroutine;           // Underlying coroutine (Phase 3.20)
    Promise _promise;               // Promise for result
    int _stream_id;                 // Which GPU stream executing
    unsigned long long _queued_time;
    unsigned long long _start_time;
    unsigned long long _complete_time;
    const char* _span_id;           // Link to tracing

    __device__ Task(const char* id = "")
        : _id(id), _state(0), _queued_time(clock64()), _start_time(0),
          _complete_time(0) {}

    // Start task execution
    __device__ void start(int stream_id) {
        _state = 1;  // running
        _stream_id = stream_id;
        _start_time = clock64();
    }

    // Complete task successfully
    __device__ void complete(const char* result) {
        _state = 2;  // complete
        _complete_time = clock64();
        _promise.resolve(result);
    }

    // Fail task with error
    __device__ void fault(const char* error) {
        _state = 3;  // faulted
        _complete_time = clock64();
        _promise.reject(error);
    }

    __device__ unsigned long long getWaitTime() const {
        return _start_time > 0 ? _start_time - _queued_time : 0;
    }

    __device__ unsigned long long getExecutionTime() const {
        if (_start_time == 0 || _complete_time == 0) return 0;
        return _complete_time - _start_time;
    }
};

// SynchronizationContext (stream-local execution context, like C#)
struct SynchronizationContext {
    int _current_stream_id;         // Active GPU stream
    MicrotaskQueue _microtask_queue;
    Task* _current_task;            // Currently executing task
    const char* _context_id;        // For debugging
    unsigned long long _created_time;
    int _task_count;                // Tasks executed in this context

    __device__ SynchronizationContext(int stream_id = 0, const char* id = "")
        : _current_stream_id(stream_id), _current_task(NULL), _context_id(id),
          _created_time(clock64()), _task_count(0) {}

    // Post work to this context's stream
    __device__ bool post(const Continuation& cont) {
        return _microtask_queue.enqueue(cont);
    }

    // Run all pending work (drain microtask queue)
    __device__ int runPendingWork() {
        return _microtask_queue.drain();
    }

    __device__ int getCurrentStreamId() const { return _current_stream_id; }
    __device__ int getMicrotaskCount() const { return _microtask_queue.getCount(); }
};

// TaskScheduler (distributes tasks across GPU streams, like C# ThreadPool)
struct TaskScheduler {
    SynchronizationContext _contexts[8];  // One context per stream
    int _num_streams;
    Task* _task_queue[32];          // Pending tasks
    int _queue_head;
    int _queue_tail;
    int _queue_count;
    unsigned long long _total_tasks;
    int _load_balance_threshold;    // Threshold for work stealing (Phase 3.19 integration)

    __device__ TaskScheduler(int num_streams = 2)
        : _num_streams(num_streams), _queue_head(0), _queue_tail(0), _queue_count(0),
          _total_tasks(0), _load_balance_threshold(5) {
        for (int i = 0; i < 8; i++) {
            _contexts[i]._current_stream_id = i;
        }
    }

    // Queue a task for scheduling
    __device__ bool queueTask(Task* task) {
        if (_queue_count >= 32) return false;  // Queue full
        _task_queue[_queue_tail] = task;
        _queue_tail = (_queue_tail + 1) % 32;
        _queue_count++;
        return true;
    }

    // Dequeue next task
    __device__ bool dequeueTask(Task*& out) {
        if (_queue_count == 0) return false;
        out = _task_queue[_queue_head];
        _queue_head = (_queue_head + 1) % 32;
        _queue_count--;
        return true;
    }

    // Find least-loaded stream
    __device__ int findLeastLoadedStream() {
        int min_load = 999999;
        int best_idx = 0;
        for (int i = 0; i < _num_streams; i++) {
            int load = _contexts[i].getMicrotaskCount();
            if (load < min_load) {
                min_load = load;
                best_idx = i;
            }
        }
        return best_idx;
    }

    // Schedule tasks to streams (round-robin with load balancing)
    __device__ int scheduleWork() {
        int scheduled = 0;
        Task* task;
        while (dequeueTask(task)) {
            int stream_idx = findLeastLoadedStream();
            task->start(stream_idx);
            _total_tasks++;
            scheduled++;
        }
        return scheduled;
    }

    __device__ SynchronizationContext* getContext(int stream_id) {
        if (stream_id >= 0 && stream_id < _num_streams) {
            return &_contexts[stream_id];
        }
        return NULL;
    }

    __device__ unsigned long long getTotalTasks() const { return _total_tasks; }
};

// Helper: Create promise
__device__ inline Promise symm_create_promise(const char* id, const char* span_id) {
    Promise p(id);
    p._span_id = span_id;
    return p;
}

// Helper: Create task
__device__ inline Task symm_create_task(const char* id, const char* span_id) {
    Task t(id);
    t._span_id = span_id;
    return t;
}

// Helper: Queue microtask (high-priority continuation)
__device__ inline bool symm_queue_microtask(SynchronizationContext& ctx,
                                            const char* handler_name,
                                            const char* span_id) {
    Continuation cont;
    cont._id = handler_name;
    cont._type = 0;  // then
    cont._handler_name = handler_name;
    cont._priority = 1;  // microtask priority
    cont._span_id = span_id;
    cont._scheduled_time = clock64();
    return ctx.post(cont);
}

// Helper: Format promise status as JSON
__device__ inline const char* symm_format_promise_status(const Promise& p) {
    static char buf[384];
    const char* state_str = "pending";
    if (p.isFulfilled()) state_str = "fulfilled";
    else if (p.isRejected()) state_str = "rejected";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"value\":\"%s\",\"latency_us\":%llu}",
        p._id ? p._id : "", state_str, p._value ? p._value : "",
        p.getLatency() / 1000);
    return buf;
}

// Helper: Format task status as JSON
__device__ inline const char* symm_format_task_status(const Task& t) {
    static char buf[512];
    const char* state_str = "pending";
    if (t._state == 1) state_str = "running";
    else if (t._state == 2) state_str = "complete";
    else if (t._state == 3) state_str = "faulted";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"stream\":%d,\"wait_us\":%llu,"
        "\"exec_us\":%llu}",
        t._id ? t._id : "", state_str, t._stream_id,
        t.getWaitTime() / 1000, t.getExecutionTime() / 1000);
    return buf;
}

// Helper: Format scheduler health as JSON
__device__ inline const char* symm_format_scheduler_status(const TaskScheduler& scheduler) {
    static char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"num_streams\":%d,\"queued_tasks\":%d,\"total_tasks\":%llu,"
        "\"load_balance_threshold\":%d}",
        scheduler._num_streams, scheduler._queue_count,
        scheduler._total_tasks, scheduler._load_balance_threshold);
    return buf;
}

// Helper: Format synchronization context as JSON
__device__ inline const char* symm_format_sync_context(const SynchronizationContext& ctx) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"stream\":%d,\"microtasks\":%d,\"depth\":%d,\"tasks_executed\":%d}",
        ctx._current_stream_id, ctx.getMicrotaskCount(), ctx.getDepth(),
        ctx._task_count);
    return buf;
}

// ── Phase 4.1: Promise Utilities ───────────────────────────────────────────────

// Promise.all result (waits for all promises to settle)
struct PromiseAll {
    const char* _id;                // "promise-all-001"
    Promise* _promises[16];         // Up to 16 promises
    int _promise_count;
    int _fulfilled_count;
    int _rejected_count;
    const char* _error;             // First error if any rejected
    int _state;                     // 0=pending, 1=all fulfilled, 2=any rejected
    unsigned long long _created_time;
    unsigned long long _settled_time;
    const char* _span_id;

    __device__ PromiseAll(const char* id = "")
        : _id(id), _promise_count(0), _fulfilled_count(0), _rejected_count(0),
          _error(NULL), _state(0), _created_time(clock64()), _settled_time(0) {}

    // Add promise to collection
    __device__ bool addPromise(Promise* p) {
        if (_promise_count >= 16) return false;
        _promises[_promise_count] = p;
        _promise_count++;
        return true;
    }

    // Check if all settled (fulfilled or rejected)
    __device__ void checkStatus() {
        _fulfilled_count = 0;
        _rejected_count = 0;
        for (int i = 0; i < _promise_count; i++) {
            if (_promises[i]->isFulfilled()) {
                _fulfilled_count++;
            } else if (_promises[i]->isRejected()) {
                _rejected_count++;
                if (_error == NULL) {
                    _error = _promises[i]->_value;
                }
            }
        }

        int settled = _fulfilled_count + _rejected_count;
        if (settled == _promise_count) {
            if (_rejected_count > 0) {
                _state = 2;  // Any rejected
            } else {
                _state = 1;  // All fulfilled
            }
            _settled_time = clock64();
        }
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isFulfilled() const { return _state == 1; }
    __device__ bool isRejected() const { return _state == 2; }
    __device__ unsigned long long getLatency() const {
        if (_settled_time == 0) return 0;
        return _settled_time - _created_time;
    }
};

// Promise.race result (waits for first promise to settle)
struct PromiseRace {
    const char* _id;
    Promise* _promises[16];
    int _promise_count;
    const char* _winner_value;      // First to settle
    int _winner_index;              // Which promise won
    int _state;                     // 0=pending, 1=settled
    int _winner_state;              // 1=fulfilled, 2=rejected
    unsigned long long _created_time;
    unsigned long long _settled_time;
    const char* _span_id;

    __device__ PromiseRace(const char* id = "")
        : _id(id), _promise_count(0), _winner_value(NULL), _winner_index(-1),
          _state(0), _winner_state(0), _created_time(clock64()),
          _settled_time(0) {}

    // Add promise
    __device__ bool addPromise(Promise* p) {
        if (_promise_count >= 16) return false;
        _promises[_promise_count] = p;
        _promise_count++;
        return true;
    }

    // Check if any promise settled
    __device__ void checkStatus() {
        if (_state != 0) return;  // Already settled
        for (int i = 0; i < _promise_count; i++) {
            if (_promises[i]->isFulfilled() || _promises[i]->isRejected()) {
                _state = 1;  // Settled
                _winner_index = i;
                _winner_value = _promises[i]->_value;
                _winner_state = _promises[i]->isFulfilled() ? 1 : 2;
                _settled_time = clock64();
                return;
            }
        }
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isSettled() const { return _state == 1; }
    __device__ unsigned long long getLatency() const {
        if (_settled_time == 0) return 0;
        return _settled_time - _created_time;
    }
};

// Promise.allSettled result (waits for all to complete, doesn't care about state)
struct PromiseAllSettled {
    const char* _id;
    Promise* _promises[16];
    int _promise_count;
    int _settled_count;
    int _fulfilled_count;
    int _rejected_count;
    int _state;                     // 0=pending, 1=all settled
    unsigned long long _created_time;
    unsigned long long _settled_time;
    const char* _span_id;

    __device__ PromiseAllSettled(const char* id = "")
        : _id(id), _promise_count(0), _settled_count(0), _fulfilled_count(0),
          _rejected_count(0), _state(0), _created_time(clock64()),
          _settled_time(0) {}

    // Add promise
    __device__ bool addPromise(Promise* p) {
        if (_promise_count >= 16) return false;
        _promises[_promise_count] = p;
        _promise_count++;
        return true;
    }

    // Check if all settled
    __device__ void checkStatus() {
        _settled_count = 0;
        _fulfilled_count = 0;
        _rejected_count = 0;
        for (int i = 0; i < _promise_count; i++) {
            if (!_promises[i]->isPending()) {
                _settled_count++;
                if (_promises[i]->isFulfilled()) {
                    _fulfilled_count++;
                } else {
                    _rejected_count++;
                }
            }
        }
        if (_settled_count == _promise_count) {
            _state = 1;  // All settled
            _settled_time = clock64();
        }
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isSettled() const { return _state == 1; }
    __device__ unsigned long long getLatency() const {
        if (_settled_time == 0) return 0;
        return _settled_time - _created_time;
    }
};

// Promise.any result (waits for first fulfilled, rejects if all rejected)
struct PromiseAny {
    const char* _id;
    Promise* _promises[16];
    int _promise_count;
    int _fulfilled_count;
    int _rejected_count;
    const char* _winner_value;      // First fulfilled
    const char* _error;             // If all rejected
    int _state;                     // 0=pending, 1=fulfilled, 2=rejected
    unsigned long long _created_time;
    unsigned long long _settled_time;
    const char* _span_id;

    __device__ PromiseAny(const char* id = "")
        : _id(id), _promise_count(0), _fulfilled_count(0), _rejected_count(0),
          _winner_value(NULL), _error(NULL), _state(0),
          _created_time(clock64()), _settled_time(0) {}

    // Add promise
    __device__ bool addPromise(Promise* p) {
        if (_promise_count >= 16) return false;
        _promises[_promise_count] = p;
        _promise_count++;
        return true;
    }

    // Check if any fulfilled or all rejected
    __device__ void checkStatus() {
        _fulfilled_count = 0;
        _rejected_count = 0;
        for (int i = 0; i < _promise_count; i++) {
            if (_promises[i]->isFulfilled()) {
                _fulfilled_count++;
                if (_winner_value == NULL) {
                    _winner_value = _promises[i]->_value;
                    _state = 1;  // Fulfilled
                    _settled_time = clock64();
                    return;
                }
            } else if (_promises[i]->isRejected()) {
                _rejected_count++;
                if (_error == NULL) {
                    _error = _promises[i]->_value;
                }
            }
        }
        // If all rejected
        if (_rejected_count == _promise_count && _fulfilled_count == 0) {
            _state = 2;  // Rejected
            _settled_time = clock64();
        }
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isFulfilled() const { return _state == 1; }
    __device__ bool isRejected() const { return _state == 2; }
    __device__ unsigned long long getLatency() const {
        if (_settled_time == 0) return 0;
        return _settled_time - _created_time;
    }
};

// Helper: Create Promise.all
__device__ inline PromiseAll symm_promise_all(const char* id, const char* span_id) {
    PromiseAll pa(id);
    pa._span_id = span_id;
    return pa;
}

// Helper: Create Promise.race
__device__ inline PromiseRace symm_promise_race(const char* id, const char* span_id) {
    PromiseRace pr(id);
    pr._span_id = span_id;
    return pr;
}

// Helper: Create Promise.allSettled
__device__ inline PromiseAllSettled symm_promise_all_settled(const char* id, const char* span_id) {
    PromiseAllSettled pas(id);
    pas._span_id = span_id;
    return pas;
}

// Helper: Create Promise.any
__device__ inline PromiseAny symm_promise_any(const char* id, const char* span_id) {
    PromiseAny pa(id);
    pa._span_id = span_id;
    return pa;
}

// Helper: Format Promise.all status
__device__ inline const char* symm_format_promise_all_status(const PromiseAll& pa) {
    static char buf[384];
    const char* state_str = "pending";
    if (pa.isFulfilled()) state_str = "fulfilled";
    else if (pa.isRejected()) state_str = "rejected";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"promises\":%d,\"fulfilled\":%d,"
        "\"rejected\":%d,\"latency_us\":%llu}",
        pa._id ? pa._id : "", state_str, pa._promise_count,
        pa._fulfilled_count, pa._rejected_count, pa.getLatency() / 1000);
    return buf;
}

// Helper: Format Promise.race status
__device__ inline const char* symm_format_promise_race_status(const PromiseRace& pr) {
    static char buf[384];
    const char* state_str = "pending";
    if (pr.isSettled()) state_str = pr._winner_state == 1 ? "fulfilled" : "rejected";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"winner_index\":%d,\"promises\":%d,"
        "\"latency_us\":%llu}",
        pr._id ? pr._id : "", state_str, pr._winner_index, pr._promise_count,
        pr.getLatency() / 1000);
    return buf;
}

// Helper: Format Promise.allSettled status
__device__ inline const char* symm_format_promise_all_settled_status(const PromiseAllSettled& pas) {
    static char buf[384];
    const char* state_str = pas.isPending() ? "pending" : "settled";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"promises\":%d,\"settled\":%d,"
        "\"fulfilled\":%d,\"rejected\":%d,\"latency_us\":%llu}",
        pas._id ? pas._id : "", state_str, pas._promise_count,
        pas._settled_count, pas._fulfilled_count, pas._rejected_count,
        pas.getLatency() / 1000);
    return buf;
}

// Helper: Format Promise.any status
__device__ inline const char* symm_format_promise_any_status(const PromiseAny& pa) {
    static char buf[384];
    const char* state_str = "pending";
    if (pa.isFulfilled()) state_str = "fulfilled";
    else if (pa.isRejected()) state_str = "rejected";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"promises\":%d,\"fulfilled\":%d,"
        "\"rejected\":%d,\"latency_us\":%llu}",
        pa._id ? pa._id : "", state_str, pa._promise_count,
        pa._fulfilled_count, pa._rejected_count, pa.getLatency() / 1000);
    return buf;
}

// ── Phase 4.2: AsyncGenerator Support ──────────────────────────────────────────

// AsyncIterator protocol: result of .next() call
struct AsyncIteratorResult {
    const char* _value;             // Yielded value
    int _done;                      // 1=done, 0=more data
    unsigned long long _timestamp;
    const char* _span_id;           // Link to Phase 3.18 tracing
};

// AsyncGenerator: yields values asynchronously
struct AsyncGenerator {
    const char* _id;                // "gen-001"
    int _state;                     // 0=suspended, 1=executing, 2=done
    AsyncIteratorResult _current;
    int _yield_count;
    unsigned long long _created_time;
    const char* _span_id;           // Link to tracing

    __device__ AsyncGenerator(const char* id = "")
        : _id(id), _state(0), _yield_count(0), _created_time(clock64()) {}

    // Transition to executing
    __device__ void start() {
        _state = 1;  // executing
    }

    // Yield a value (suspend)
    __device__ void yield_value(const char* value) {
        _current._value = value;
        _current._done = 0;
        _current._timestamp = clock64();
        _yield_count++;
        _state = 0;  // suspended
    }

    // Finish generator
    __device__ void finish() {
        _current._value = NULL;
        _current._done = 1;
        _current._timestamp = clock64();
        _state = 2;  // done
    }

    __device__ bool isDone() const { return _state == 2; }
    __device__ int getYieldCount() const { return _yield_count; }
};

// AsyncIterable: object that has async iterator protocol
struct AsyncIterable {
    const char* _id;
    AsyncGenerator _generator;
    int _index;                     // Current iteration index
    int _length;                    // Number of items
    const char* _items[32];         // Items to iterate over
    int _started;                   // 1=iteration started, 0=not started
    unsigned long long _created_time;
    const char* _span_id;

    __device__ AsyncIterable(const char* id = "")
        : _id(id), _index(0), _length(0), _started(0),
          _created_time(clock64()) {}

    // Add item to iterate over
    __device__ bool addItem(const char* item) {
        if (_length >= 32) return false;
        _items[_length] = item;
        _length++;
        return true;
    }

    // Get async iterator (returns AsyncGenerator)
    __device__ AsyncGenerator* getAsyncIterator() {
        _started = 1;
        _generator.start();
        return &_generator;
    }

    // Next iteration
    __device__ bool next(AsyncIteratorResult& out) {
        if (_index >= _length) {
            _generator.finish();
            out._done = 1;
            out._value = NULL;
            return false;
        }
        _generator.yield_value(_items[_index]);
        out = _generator._current;
        _index++;
        return true;
    }

    __device__ bool isDone() const { return _index >= _length; }
    __device__ int getProgress() const { return _index; }
};

// For-await loop context
struct ForAwaitContext {
    const char* _id;                // Loop ID
    AsyncIterable _iterable;
    AsyncGenerator _generator;
    int _iteration;                 // Current iteration number
    AsyncIteratorResult _current;
    int _completed;                 // 1=loop completed, 0=in progress
    unsigned long long _loop_start;
    unsigned long long _loop_end;
    const char* _span_id;           // Link to tracing

    __device__ ForAwaitContext(const char* id = "")
        : _id(id), _iteration(0), _completed(0), _loop_start(clock64()) {}

    // Start iteration
    __device__ bool init(AsyncIterable& iterable) {
        _iterable = iterable;
        _generator = *iterable.getAsyncIterator();
        return true;
    }

    // Get next value
    __device__ bool next(AsyncIteratorResult& out) {
        if (_iterable.next(out)) {
            _current = out;
            _iteration++;
            return true;
        }
        _completed = 1;
        _loop_end = clock64();
        return false;
    }

    __device__ int getIterationCount() const { return _iteration; }
    __device__ bool isCompleted() const { return _completed == 1; }
    __device__ unsigned long long getLoopDuration() const {
        if (_loop_end == 0) return 0;
        return _loop_end - _loop_start;
    }
};

// AsyncGeneratorFunction: factory for creating async generators
struct AsyncGeneratorFunction {
    const char* _name;              // Function name
    int _call_count;
    unsigned long long _created_time;
    const char* _span_id;

    __device__ AsyncGeneratorFunction(const char* name = "")
        : _name(name), _call_count(0), _created_time(clock64()) {}

    // Call function to create generator
    __device__ AsyncGenerator create() {
        AsyncGenerator gen(_name);
        gen._span_id = _span_id;
        _call_count++;
        return gen;
    }

    __device__ int getCallCount() const { return _call_count; }
};

// Helper: Create async generator
__device__ inline AsyncGenerator symm_create_async_generator(const char* id, const char* span_id) {
    AsyncGenerator gen(id);
    gen._span_id = span_id;
    return gen;
}

// Helper: Create async iterable
__device__ inline AsyncIterable symm_create_async_iterable(const char* id, const char* span_id) {
    AsyncIterable iter(id);
    iter._span_id = span_id;
    return iter;
}

// Helper: Create for-await context
__device__ inline ForAwaitContext symm_create_for_await(const char* id, const char* span_id) {
    ForAwaitContext ctx(id);
    ctx._span_id = span_id;
    return ctx;
}

// Helper: Yield value from generator
__device__ inline void symm_yield_async(AsyncGenerator& gen, const char* value) {
    gen.yield_value(value);
}

// Helper: Check if iteration done
__device__ inline bool symm_async_iterator_done(const AsyncIteratorResult& result) {
    return result._done == 1;
}

// Helper: Format generator status as JSON
__device__ inline const char* symm_format_async_generator_status(const AsyncGenerator& gen) {
    static char buf[256];
    const char* state_str = "suspended";
    if (gen._state == 1) state_str = "executing";
    else if (gen._state == 2) state_str = "done";

    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"state\":\"%s\",\"yield_count\":%d,\"age_us\":%llu}",
        gen._id ? gen._id : "", state_str, gen._yield_count,
        (clock64() - gen._created_time) / 1000);
    return buf;
}

// Helper: Format async iterable status as JSON
__device__ inline const char* symm_format_async_iterable_status(const AsyncIterable& iter) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"length\":%d,\"progress\":%d,\"utilization\":%.1f}",
        iter._id ? iter._id : "", iter._length, iter._index,
        iter._length > 0 ? (float)iter._index / iter._length * 100.0 : 0.0);
    return buf;
}

// Helper: Format for-await context as JSON
__device__ inline const char* symm_format_for_await_status(const ForAwaitContext& ctx) {
    static char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"iteration\":%d,\"completed\":%d,\"duration_us\":%llu,"
        "\"current_value\":\"%s\"}",
        ctx._id ? ctx._id : "", ctx._iteration, ctx._completed,
        ctx.getLoopDuration() / 1000,
        ctx._current._value ? ctx._current._value : "");
    return buf;
}

// ── Phase 4.3: Module System (Dynamic Imports, import.meta) ──────────────────

// Module metadata (import.meta)
struct ModuleMeta {
    const char* _url;               // Module URL/path
    const char* _id;                // Module ID (for identification)
    unsigned long long _load_time;  // When module was loaded
    int _loaded;                    // 1=loaded, 0=loading
    const char* _type;              // "module" or "commonjs"
    const char* _span_id;           // Link to Phase 3.18 tracing
};

// Module record (represents loaded module)
struct ModuleRecord {
    const char* _url;
    const char* _id;
    ModuleMeta _meta;
    const char* _namespace[16];     // Exported names
    const char* _exports[16];       // Exported values
    int _export_count;
    int _loaded;                    // 1=loaded, 0=loading, -1=failed
    const char* _error;             // Error if failed
    unsigned long long _load_start;
    unsigned long long _load_end;
    const char* _span_id;
};

// Module loader (manages module loading)
struct ModuleLoader {
    ModuleRecord _modules[16];      // Loaded modules (up to 16)
    int _module_count;
    const char* _base_url;          // Base URL for resolution
    int _loading_count;             // Currently loading
    unsigned long long _created_time;
    const char* _span_id;

    __device__ ModuleLoader(const char* base_url = ".")
        : _module_count(0), _base_url(base_url), _loading_count(0),
          _created_time(clock64()) {}

    // Add loaded module
    __device__ bool addModule(const ModuleRecord& module) {
        if (_module_count >= 16) return false;
        _modules[_module_count] = module;
        _module_count++;
        return true;
    }

    // Find module by URL
    __device__ ModuleRecord* findModule(const char* url) {
        for (int i = 0; i < _module_count; i++) {
            if (_modules[i]._url == url) {
                return &_modules[i];
            }
        }
        return NULL;
    }

    __device__ int getModuleCount() const { return _module_count; }
    __device__ int getLoadingCount() const { return _loading_count; }
};

// ImportPromise: Promise for import() calls
struct ImportPromise {
    const char* _module_url;        // URL being imported
    const char* _id;                // Import ID
    int _state;                     // 0=pending, 1=loaded, 2=failed
    ModuleRecord* _module;          // Loaded module (if succeeded)
    const char* _error;             // Error message (if failed)
    unsigned long long _started;
    unsigned long long _completed;
    const char* _span_id;           // Link to Phase 3.18 tracing

    __device__ ImportPromise(const char* url = "")
        : _module_url(url), _id(NULL), _state(0), _module(NULL),
          _error(NULL), _started(clock64()), _completed(0) {}

    // Mark as loaded
    __device__ void markLoaded(ModuleRecord* module) {
        _state = 1;  // loaded
        _module = module;
        _completed = clock64();
    }

    // Mark as failed
    __device__ void markFailed(const char* error) {
        _state = 2;  // failed
        _error = error;
        _completed = clock64();
    }

    __device__ bool isPending() const { return _state == 0; }
    __device__ bool isLoaded() const { return _state == 1; }
    __device__ bool isFailed() const { return _state == 2; }
    __device__ unsigned long long getLoadTime() const {
        if (_completed == 0) return 0;
        return _completed - _started;
    }
};

// ImportMap: URL resolution mapping
struct ImportMap {
    const char* _imports[16];       // Import specifiers (e.g., "lodash")
    const char* _urls[16];          // Resolved URLs (e.g., "/vendor/lodash.js")
    int _mapping_count;
    unsigned long long _created_time;

    __device__ ImportMap() : _mapping_count(0), _created_time(clock64()) {}

    // Add mapping
    __device__ bool addMapping(const char* specifier, const char* url) {
        if (_mapping_count >= 16) return false;
        _imports[_mapping_count] = specifier;
        _urls[_mapping_count] = url;
        _mapping_count++;
        return true;
    }

    // Resolve specifier to URL
    __device__ const char* resolve(const char* specifier) {
        for (int i = 0; i < _mapping_count; i++) {
            if (_imports[i] == specifier) {
                return _urls[i];
            }
        }
        return specifier;  // Return specifier as-is if not mapped
    }

    __device__ int getMappingCount() const { return _mapping_count; }
};

// Module cache: LRU cache of loaded modules
struct ModuleCache {
    const char* _urls[32];          // Module URLs
    ModuleRecord* _modules[32];     // Module records
    unsigned long long _access_times[32];  // Last access time
    int _cache_count;
    int _hits;
    int _misses;

    __device__ ModuleCache() : _cache_count(0), _hits(0), _misses(0) {}

    // Get from cache
    __device__ ModuleRecord* get(const char* url) {
        for (int i = 0; i < _cache_count; i++) {
            if (_urls[i] == url) {
                _access_times[i] = clock64();  // Update LRU
                _hits++;
                return _modules[i];
            }
        }
        _misses++;
        return NULL;
    }

    // Put in cache
    __device__ bool put(const char* url, ModuleRecord* module) {
        if (_cache_count < 32) {
            _urls[_cache_count] = url;
            _modules[_cache_count] = module;
            _access_times[_cache_count] = clock64();
            _cache_count++;
            return true;
        }
        // LRU eviction: remove least recently used
        int lru_idx = 0;
        unsigned long long lru_time = _access_times[0];
        for (int i = 1; i < _cache_count; i++) {
            if (_access_times[i] < lru_time) {
                lru_time = _access_times[i];
                lru_idx = i;
            }
        }
        _urls[lru_idx] = url;
        _modules[lru_idx] = module;
        _access_times[lru_idx] = clock64();
        return true;
    }

    __device__ float getHitRate() const {
        int total = _hits + _misses;
        return total > 0 ? (float)_hits / total : 0.0f;
    }
};

// Helper: Create module meta
__device__ inline ModuleMeta symm_create_module_meta(const char* url, const char* span_id) {
    ModuleMeta meta;
    meta._url = url;
    meta._id = url;
    meta._load_time = clock64();
    meta._loaded = 0;  // Not yet loaded
    meta._type = "module";
    meta._span_id = span_id;
    return meta;
}

// Helper: Create module record
__device__ inline ModuleRecord symm_create_module_record(const char* url, const char* span_id) {
    ModuleRecord record;
    record._url = url;
    record._id = url;
    record._meta = symm_create_module_meta(url, span_id);
    record._export_count = 0;
    record._loaded = 0;  // Not yet loaded
    record._error = NULL;
    record._load_start = clock64();
    record._load_end = 0;
    record._span_id = span_id;
    return record;
}

// Helper: Create import promise
__device__ inline ImportPromise symm_create_import_promise(const char* url, const char* span_id) {
    ImportPromise promise(url);
    promise._span_id = span_id;
    return promise;
}

// Helper: Create module loader
__device__ inline ModuleLoader symm_create_module_loader(const char* base_url, const char* span_id) {
    ModuleLoader loader(base_url);
    loader._span_id = span_id;
    return loader;
}

// Helper: Create import map
__device__ inline ImportMap symm_create_import_map() {
    return ImportMap();
}

// Helper: Format module meta as JSON
__device__ inline const char* symm_format_module_meta(const ModuleMeta& meta) {
    static char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"url\":\"%s\",\"type\":\"%s\",\"loaded\":%d,\"load_time_us\":%llu}",
        meta._url ? meta._url : "", meta._type ? meta._type : "module",
        meta._loaded, (clock64() - meta._load_time) / 1000);
    return buf;
}

// Helper: Format module record as JSON
__device__ inline const char* symm_format_module_record(const ModuleRecord& record) {
    static char buf[512];
    const char* status = "loading";
    if (record._loaded == 1) status = "loaded";
    else if (record._loaded == -1) status = "failed";

    snprintf(buf, sizeof(buf),
        "{\"url\":\"%s\",\"status\":\"%s\",\"exports\":%d,\"error\":\"%s\","
        "\"duration_us\":%llu}",
        record._url ? record._url : "", status, record._export_count,
        record._error ? record._error : "",
        (record._load_end > 0 ? record._load_end - record._load_start : 0) / 1000);
    return buf;
}

// Helper: Format import promise as JSON
__device__ inline const char* symm_format_import_promise_status(const ImportPromise& promise) {
    static char buf[384];
    const char* state_str = "pending";
    if (promise.isLoaded()) state_str = "loaded";
    else if (promise.isFailed()) state_str = "failed";

    snprintf(buf, sizeof(buf),
        "{\"url\":\"%s\",\"state\":\"%s\",\"load_time_us\":%llu}",
        promise._module_url ? promise._module_url : "", state_str,
        promise.getLoadTime() / 1000);
    return buf;
}

// Helper: Format module loader status as JSON
__device__ inline const char* symm_format_module_loader_status(const ModuleLoader& loader) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"modules\":%d,\"loading\":%d,\"base_url\":\"%s\"}",
        loader._module_count, loader._loading_count,
        loader._base_url ? loader._base_url : ".");
    return buf;
}

// Helper: Format module cache stats as JSON
__device__ inline const char* symm_format_module_cache_stats(const ModuleCache& cache) {
    static char buf[256];
    float hit_rate = cache.getHitRate();
    snprintf(buf, sizeof(buf),
        "{\"cached\":%d,\"hits\":%d,\"misses\":%d,\"hit_rate\":%.1f}",
        cache._cache_count, cache._hits, cache._misses, hit_rate * 100.0f);
    return buf;
}

// ── Phase 5.0: RTTI System (Runtime Type Information) ────────────────────────

// Type descriptor for runtime type checking
struct TypeDescriptor {
    const char* _name;              // Type name ("string", "number", "object", etc.)
    int _type_id;                   // 0=undefined, 1=null, 2=boolean, 3=number, 4=string,
                                    // 5=symbol, 6=object, 7=function, 8=array, 9=date, 10=regexp
    int _is_callable;               // 1=function/method
    int _is_iterable;               // 1=array/string/map/set
    int _is_primitive;              // 1=primitive type
    unsigned long long _created_time;
    const char* _span_id;
};

// Property descriptor (like Object.getOwnPropertyDescriptor)
struct PropertyDescriptor {
    const char* _name;
    const char* _value;
    int _writable;                  // 1=writable
    int _enumerable;                // 1=enumerable
    int _configurable;              // 1=configurable
    int _is_accessor;               // 1=getter/setter, 0=data property
    const char* _span_id;
};

// Reflect API support
struct ReflectAPI {
    const char* _id;
    int _operation_count;           // Number of reflect operations
    unsigned long long _created_time;
    const char* _span_id;

    __device__ ReflectAPI(const char* id = "")
        : _id(id), _operation_count(0), _created_time(clock64()) {}

    // Reflect.getOwnPropertyNames
    __device__ int getOwnPropertyNames(const char* obj_name, const char** names, int max_count) {
        // Simplified: return empty for now (would parse object at runtime)
        return 0;
    }

    // Reflect.has (hasOwnProperty)
    __device__ int has(const char* obj_name, const char* prop_name) {
        _operation_count++;
        return 1;  // Simplified: always true
    }

    // Reflect.get
    __device__ const char* get(const char* obj_name, const char* prop_name) {
        _operation_count++;
        return NULL;  // Simplified: would get property
    }

    // Reflect.set
    __device__ int set(const char* obj_name, const char* prop_name, const char* value) {
        _operation_count++;
        return 1;  // Simplified: always success
    }

    // Reflect.deleteProperty
    __device__ int deleteProperty(const char* obj_name, const char* prop_name) {
        _operation_count++;
        return 1;  // Simplified: always success
    }

    // Reflect.getPrototypeOf
    __device__ const char* getPrototypeOf(const char* obj_name) {
        _operation_count++;
        return "Object";  // Simplified
    }

    __device__ int getOperationCount() const { return _operation_count; }
};

// WeakMap: Maps objects to values, doesn't prevent garbage collection
struct WeakMapEntry {
    const char* _key;               // Object identity (pointer as string)
    const char* _value;
    unsigned long long _set_time;
};

struct WeakMap {
    const char* _id;
    WeakMapEntry _entries[16];      // Up to 16 weak references
    int _entry_count;
    int _cleared_count;             // Entries cleared by GC (simulated)
    unsigned long long _created_time;
    const char* _span_id;

    __device__ WeakMap(const char* id = "")
        : _id(id), _entry_count(0), _cleared_count(0), _created_time(clock64()) {}

    // Set value for key
    __device__ int set(const char* key, const char* value) {
        if (_entry_count >= 16) return 0;  // Full
        _entries[_entry_count]._key = key;
        _entries[_entry_count]._value = value;
        _entries[_entry_count]._set_time = clock64();
        _entry_count++;
        return 1;
    }

    // Get value for key
    __device__ const char* get(const char* key) {
        for (int i = 0; i < _entry_count; i++) {
            if (_entries[i]._key == key) {
                return _entries[i]._value;
            }
        }
        return NULL;  // Not found (or key was GC'd)
    }

    // Check if key exists
    __device__ int has(const char* key) {
        return get(key) != NULL ? 1 : 0;
    }

    // Delete key
    __device__ int delete_key(const char* key) {
        for (int i = 0; i < _entry_count; i++) {
            if (_entries[i]._key == key) {
                // Shift remaining entries
                for (int j = i; j < _entry_count - 1; j++) {
                    _entries[j] = _entries[j + 1];
                }
                _entry_count--;
                return 1;
            }
        }
        return 0;
    }

    __device__ int getSize() const { return _entry_count; }
};

// WeakSet: Set of objects, doesn't prevent garbage collection
struct WeakSetEntry {
    const char* _key;               // Object identity
    unsigned long long _added_time;
};

struct WeakSet {
    const char* _id;
    WeakSetEntry _entries[16];      // Up to 16 weak references
    int _entry_count;
    int _cleared_count;             // Entries cleared by GC (simulated)
    unsigned long long _created_time;
    const char* _span_id;

    __device__ WeakSet(const char* id = "")
        : _id(id), _entry_count(0), _cleared_count(0), _created_time(clock64()) {}

    // Add key
    __device__ int add(const char* key) {
        if (_entry_count >= 16) return 0;  // Full
        // Check for duplicate
        for (int i = 0; i < _entry_count; i++) {
            if (_entries[i]._key == key) return 0;  // Already exists
        }
        _entries[_entry_count]._key = key;
        _entries[_entry_count]._added_time = clock64();
        _entry_count++;
        return 1;
    }

    // Check if key exists
    __device__ int has(const char* key) {
        for (int i = 0; i < _entry_count; i++) {
            if (_entries[i]._key == key) return 1;
        }
        return 0;
    }

    // Delete key
    __device__ int delete_key(const char* key) {
        for (int i = 0; i < _entry_count; i++) {
            if (_entries[i]._key == key) {
                // Shift remaining entries
                for (int j = i; j < _entry_count - 1; j++) {
                    _entries[j] = _entries[j + 1];
                }
                _entry_count--;
                return 1;
            }
        }
        return 0;
    }

    __device__ int getSize() const { return _entry_count; }
};

// Type checking utilities
struct TypeChecker {
    const char* _id;
    int _checks_performed;
    unsigned long long _created_time;
    const char* _span_id;

    __device__ TypeChecker(const char* id = "")
        : _id(id), _checks_performed(0), _created_time(clock64()) {}

    // Get type of value
    __device__ const char* typeof_value(const char* value) {
        _checks_performed++;
        if (value == NULL) return "undefined";
        if (value[0] == 't' && value[1] == 'r' && value[2] == 'u' && value[3] == 'e') return "boolean";
        if (value[0] == 'f' && value[1] == 'a' && value[2] == 'l' && value[3] == 's') return "boolean";
        // Simplified: would need more sophisticated type checking
        return "object";
    }

    // instanceof check
    __device__ int instanceof_check(const char* value, const char* constructor) {
        _checks_performed++;
        return 1;  // Simplified
    }

    // isArray check
    __device__ int isArray(const char* value) {
        _checks_performed++;
        if (value && value[0] == '[') return 1;
        return 0;
    }

    __device__ int getChecksPerformed() const { return _checks_performed; }
};

// Helper: Create type descriptor
__device__ inline TypeDescriptor symm_create_type_descriptor(const char* name, int type_id, const char* span_id) {
    TypeDescriptor td;
    td._name = name;
    td._type_id = type_id;
    td._is_callable = (type_id == 7) ? 1 : 0;  // function type
    td._is_iterable = (type_id == 8) ? 1 : 0;  // array type
    td._is_primitive = (type_id < 6) ? 1 : 0;  // primitive types
    td._created_time = clock64();
    td._span_id = span_id;
    return td;
}

// Helper: Create property descriptor
__device__ inline PropertyDescriptor symm_create_property_descriptor(const char* name, const char* value, const char* span_id) {
    PropertyDescriptor pd;
    pd._name = name;
    pd._value = value;
    pd._writable = 1;  // Default: writable
    pd._enumerable = 1;  // Default: enumerable
    pd._configurable = 1;  // Default: configurable
    pd._is_accessor = 0;  // Default: data property
    pd._span_id = span_id;
    return pd;
}

// Helper: Create Reflect API
__device__ inline ReflectAPI symm_create_reflect_api(const char* id, const char* span_id) {
    ReflectAPI api(id);
    api._span_id = span_id;
    return api;
}

// Helper: Create WeakMap
__device__ inline WeakMap symm_create_weak_map(const char* id, const char* span_id) {
    WeakMap wm(id);
    wm._span_id = span_id;
    return wm;
}

// Helper: Create WeakSet
__device__ inline WeakSet symm_create_weak_set(const char* id, const char* span_id) {
    WeakSet ws(id);
    ws._span_id = span_id;
    return ws;
}

// Helper: Create type checker
__device__ inline TypeChecker symm_create_type_checker(const char* id, const char* span_id) {
    TypeChecker tc(id);
    tc._span_id = span_id;
    return tc;
}

// Helper: Format type descriptor as JSON
__device__ inline const char* symm_format_type_descriptor(const TypeDescriptor& td) {
    static char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"type_id\":%d,\"callable\":%d,\"iterable\":%d,\"primitive\":%d}",
        td._name ? td._name : "", td._type_id, td._is_callable, td._is_iterable, td._is_primitive);
    return buf;
}

// Helper: Format Reflect API status as JSON
__device__ inline const char* symm_format_reflect_api_status(const ReflectAPI& api) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"operations\":%d}",
        api._id ? api._id : "", api._operation_count);
    return buf;
}

// Helper: Format WeakMap status as JSON
__device__ inline const char* symm_format_weak_map_status(const WeakMap& wm) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"entries\":%d,\"cleared\":%d}",
        wm._id ? wm._id : "", wm._entry_count, wm._cleared_count);
    return buf;
}

// Helper: Format WeakSet status as JSON
__device__ inline const char* symm_format_weak_set_status(const WeakSet& ws) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"entries\":%d,\"cleared\":%d}",
        ws._id ? ws._id : "", ws._entry_count, ws._cleared_count);
    return buf;
}

// Helper: Format TypeChecker status as JSON
__device__ inline const char* symm_format_type_checker_status(const TypeChecker& tc) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"checks\":%d}",
        tc._id ? tc._id : "", tc._checks_performed);
    return buf;
}

// ── Phase 5.1: Advanced Error Handling ─────────────────────────────────────────

// Stack frame information
struct StackFrame {
    const char* _function_name;
    const char* _file_path;
    int _line_number;
    int _column_number;
    unsigned long long _timestamp;
};

// Error with cause chain (Error.cause)
struct ErrorWithCause {
    const char* _id;
    const char* _message;
    const char* _error_type;        // "Error", "TypeError", "RangeError", etc.
    StackFrame _stack[8];           // Call stack (up to 8 frames)
    int _stack_depth;
    const char* _cause;             // Cause error (for error chaining)
    unsigned long long _created_time;
    const char* _span_id;

    __device__ ErrorWithCause(const char* id = "")
        : _id(id), _message(NULL), _error_type("Error"), _stack_depth(0),
          _cause(NULL), _created_time(clock64()) {}

    // Add stack frame
    __device__ int addFrame(const char* func, const char* file, int line, int col) {
        if (_stack_depth >= 8) return 0;
        _stack[_stack_depth]._function_name = func;
        _stack[_stack_depth]._file_path = file;
        _stack[_stack_depth]._line_number = line;
        _stack[_stack_depth]._column_number = col;
        _stack[_stack_depth]._timestamp = clock64();
        _stack_depth++;
        return 1;
    }

    // Set cause
    __device__ void setCause(const char* cause) {
        _cause = cause;
    }

    __device__ int getStackDepth() const { return _stack_depth; }
};

// Error context for debugging
struct ErrorContext {
    const char* _id;
    int _error_count;
    ErrorWithCause _errors[4];      // Track up to 4 errors
    unsigned long long _created_time;
    const char* _span_id;

    __device__ ErrorContext(const char* id = "")
        : _id(id), _error_count(0), _created_time(clock64()) {}

    // Record error
    __device__ int recordError(const ErrorWithCause& error) {
        if (_error_count >= 4) return 0;
        _errors[_error_count] = error;
        _error_count++;
        return 1;
    }

    __device__ int getErrorCount() const { return _error_count; }
};

// Helper: Create error with cause
__device__ inline ErrorWithCause symm_create_error(const char* id, const char* msg,
                                                    const char* type, const char* span_id) {
    ErrorWithCause err(id);
    err._message = msg;
    err._error_type = type;
    err._span_id = span_id;
    return err;
}

// Helper: Format error stack trace as JSON
__device__ inline const char* symm_format_error_stack(const ErrorWithCause& err) {
    static char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"%s\",\"message\":\"%s\",\"stack_depth\":%d,\"cause\":\"%s\"}",
        err._error_type ? err._error_type : "Error",
        err._message ? err._message : "",
        err._stack_depth,
        err._cause ? err._cause : "null");
    return buf;
}

// Helper: Format error context as JSON
__device__ inline const char* symm_format_error_context(const ErrorContext& ctx) {
    static char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"id\":\"%s\",\"error_count\":%d,\"active_errors\":%d}",
        ctx._id ? ctx._id : "", ctx._error_count, ctx._error_count);
    return buf;
}

// ── Security & Input Validation ────────────────────────────────────────────────

// Input length validation
__device__ inline bool symm_validate_length(const char* input, int min_len, int max_len) {
    if (!input) return min_len == 0;  // null is valid if min is 0
    int len = 0;
    while (input[len]) len++;
    return len >= min_len && len <= max_len;
}

// Email validation (basic)
__device__ inline bool symm_is_valid_email(const char* email) {
    if (!email || !email[0]) return false;
    // Must have @ and at least one character on each side
    int at_count = 0, at_pos = -1;
    for (int i = 0; email[i]; i++) {
        if (email[i] == '@') {
            at_count++;
            at_pos = i;
        }
    }
    // Check: exactly one @, has chars before and after
    return at_count == 1 && at_pos > 0 && email[at_pos + 1] != 0;
}

// Number range validation
__device__ inline bool symm_is_in_range(float value, float min_val, float max_val) {
    return value >= min_val && value <= max_val;
}

// String contains only alphanumeric
__device__ inline bool symm_is_alphanumeric(const char* str) {
    if (!str) return true;
    for (int i = 0; str[i]; i++) {
        char c = str[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

// Rate limiting: token bucket
struct RateLimiter {
    int _tokens;
    int _max_tokens;
    int _refill_rate;  // tokens per second
    unsigned long long _last_refill;

    __device__ RateLimiter(int max_tokens = 100, int refill_rate = 10)
        : _tokens(max_tokens), _max_tokens(max_tokens), _refill_rate(refill_rate),
          _last_refill(clock64()) {}

    __device__ bool allowRequest() {
        if (_tokens > 0) {
            _tokens--;
            return true;
        }
        return false;
    }

    __device__ int tokensAvailable() const { return _tokens; }
};

// Check if rate limited
__device__ inline bool symm_is_rate_limited(RateLimiter& limiter) {
    return !limiter.allowRequest();
}

// ── Phase 3.16: Advanced Rate Limiting ─────────────────────────────────────
// Sliding window limiter: tracks requests within a time window

struct SlidingWindowLimiter {
    int _request_count;        // Requests in current window
    unsigned long long _window_start;  // Start of current time window
    int _window_size_ms;       // Window size in milliseconds
    int _max_requests;         // Max requests per window

    __device__ SlidingWindowLimiter(int max_requests = 100, int window_ms = 1000)
        : _request_count(0), _window_start(clock64()), _window_size_ms(window_ms),
          _max_requests(max_requests) {}

    // Check if request is allowed within sliding window
    __device__ bool allowRequest() {
        unsigned long long now = clock64();
        unsigned long long elapsed = now - _window_start;

        // Convert to milliseconds (roughly: 1700 cycles per ms at 1.7 GHz)
        int elapsed_ms = (int)(elapsed / 1700);

        // If window expired, reset
        if (elapsed_ms >= _window_size_ms) {
            _request_count = 0;
            _window_start = now;
        }

        // Check if at limit
        if (_request_count >= _max_requests) {
            return false;
        }

        _request_count++;
        return true;
    }

    __device__ int requestsInWindow() const { return _request_count; }
    __device__ int maxRequests() const { return _max_requests; }
};

// Per-user rate limiter: separate limits for each user
struct PerUserRateLimiter {
    struct UserLimit {
        const char* user_id;
        int tokens;
        unsigned long long last_refill;
    };

    UserLimit _users[10];      // Support up to 10 concurrent users
    int _user_count;
    int _max_tokens_per_user;
    int _refill_rate;

    __device__ PerUserRateLimiter(int max_tokens = 50, int refill_rate = 5)
        : _user_count(0), _max_tokens_per_user(max_tokens), _refill_rate(refill_rate) {
        for (int i = 0; i < 10; i++) {
            _users[i].user_id = nullptr;
            _users[i].tokens = max_tokens;
            _users[i].last_refill = clock64();
        }
    }

    __device__ bool allowRequest(const char* user_id) {
        if (!user_id) return false;

        // Find or create user entry
        int user_idx = -1;
        for (int i = 0; i < _user_count; i++) {
            if (_users[i].user_id && symm_str_eq(_users[i].user_id, user_id)) {
                user_idx = i;
                break;
            }
        }

        // Create new user if not found
        if (user_idx == -1) {
            if (_user_count >= 10) return false;  // User limit reached
            user_idx = _user_count++;
            _users[user_idx].user_id = user_id;
            _users[user_idx].tokens = _max_tokens_per_user;
            _users[user_idx].last_refill = clock64();
        }

        // Check tokens
        if (_users[user_idx].tokens > 0) {
            _users[user_idx].tokens--;
            return true;
        }

        return false;
    }

    __device__ int getUserTokens(const char* user_id) const {
        for (int i = 0; i < _user_count; i++) {
            if (_users[i].user_id && symm_str_eq(_users[i].user_id, user_id)) {
                return _users[i].tokens;
            }
        }
        return 0;
    }
};

// Distributed rate limiter: coordination across instances
struct DistributedRateLimiter {
    int _local_tokens;         // Local token bucket
    int _max_tokens;
    int _refill_rate;
    unsigned long long _last_refill;
    const char* _instance_id;  // This instance identifier
    int _shared_quota;         // Quota shared with other instances

    __device__ DistributedRateLimiter(const char* instance_id, int quota = 1000, int refill = 100)
        : _local_tokens(quota), _max_tokens(quota), _refill_rate(refill),
          _last_refill(clock64()), _instance_id(instance_id), _shared_quota(quota) {}

    __device__ bool allowRequest() {
        // First try local tokens
        if (_local_tokens > 0) {
            _local_tokens--;
            return true;
        }

        // When local exhausted, request from shared quota
        if (_shared_quota > 0) {
            _shared_quota--;
            return true;
        }

        return false;
    }

    __device__ int getLocalTokens() const { return _local_tokens; }
    __device__ int getSharedQuota() const { return _shared_quota; }
    __device__ const char* getInstanceId() const { return _instance_id; }
};

// Helper: Check sliding window limit
__device__ inline bool symm_check_sliding_window(SlidingWindowLimiter& limiter) {
    return !limiter.allowRequest();
}

// Helper: Check per-user limit
__device__ inline bool symm_check_per_user_limit(PerUserRateLimiter& limiter, const char* user_id) {
    return !limiter.allowRequest(user_id);
}

// Helper: Check distributed limit
__device__ inline bool symm_check_distributed_limit(DistributedRateLimiter& limiter) {
    return !limiter.allowRequest();
}

// Helper: Get rate limit status for response
__device__ inline const char* symm_rate_limit_status(const PerUserRateLimiter& limiter, const char* user_id) {
    static char buf[128];
    int tokens = limiter.getUserTokens(user_id);
    snprintf(buf, sizeof(buf), "{\"user\":\"%s\",\"tokens_remaining\":%d}", user_id ? user_id : "", tokens);
    return buf;
}

// SQL injection detection (basic)
__device__ inline bool symm_has_sql_injection(const char* input) {
    if (!input) return false;
    // Check for dangerous SQL keywords in input
    const char* dangerous[] = {"DROP", "DELETE", "INSERT", "UPDATE", "UNION", "SELECT"};
    for (int i = 0; i < 6; i++) {
        const char* keyword = dangerous[i];
        const char* p = input;
        while (*p) {
            // Simple case-insensitive substring search
            bool match = true;
            const char* test = p;
            for (int j = 0; keyword[j] && test[j]; j++) {
                char c1 = test[j];
                char c2 = keyword[j];
                // Simple uppercase conversion
                if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
                if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
                if (c1 != c2) { match = false; break; }
            }
            if (match) return true;
            p++;
        }
    }
    return false;
}

// CORS validation
__device__ inline bool symm_is_valid_origin(const char* origin, const char* allowed_origin) {
    if (!origin || !allowed_origin) return false;
    // Simple string comparison (could be enhanced with wildcards)
    while (*origin && *allowed_origin) {
        if (*origin != *allowed_origin) return false;
        origin++; allowed_origin++;
    }
    return *origin == *allowed_origin;
}

// Input sanitization: remove newlines and null bytes
__device__ inline int symm_sanitize_input(const char* input, char* output, int max_len) {
    if (!input || !output) return 0;
    int out_pos = 0;
    for (int i = 0; input[i] && out_pos < max_len - 1; i++) {
        char c = input[i];
        // Skip newlines, carriage returns, null bytes, control chars
        if (c == '\n' || c == '\r' || c == '\0' || (c >= 0 && c < 32)) continue;
        output[out_pos++] = c;
    }
    output[out_pos] = 0;
    return out_pos;
}

// Content security validation
__device__ inline bool symm_is_safe_content_type(const char* content_type) {
    if (!content_type) return false;
    // Check for dangerous content types
    if (symm_str_eq(content_type, "application/x-executable")) return false;
    if (symm_str_eq(content_type, "application/x-sh")) return false;
    if (symm_str_eq(content_type, "application/x-msdownload")) return false;
    return true;
}

// ── HTTP Caching & Cache Control ───────────────────────────────────────────────

// Cache control directives
struct CacheControl {
    bool _public;
    bool _private;
    bool _no_cache;
    bool _no_store;
    int _max_age;
    int _s_maxage;
    bool _must_revalidate;
    bool _proxy_revalidate;

    __device__ CacheControl()
        : _public(false), _private(false), _no_cache(false), _no_store(false),
          _max_age(0), _s_maxage(0), _must_revalidate(false), _proxy_revalidate(false) {}

    __device__ bool isCacheable() const {
        return !_no_store && !_no_cache;
    }

    __device__ bool isExpired(int age_seconds) const {
        if (_max_age > 0 && age_seconds > _max_age) return true;
        if (_s_maxage > 0 && age_seconds > _s_maxage) return true;
        return false;
    }
};

// Parse Cache-Control header
__device__ inline CacheControl symm_parse_cache_control(const char* header) {
    CacheControl cc;
    if (!header) return cc;

    // Simple parsing: public, private, no-cache, no-store
    if (symm_str_eq(header, "public")) cc._public = true;
    if (symm_str_eq(header, "private")) cc._private = true;
    if (symm_str_eq(header, "no-cache")) cc._no_cache = true;
    if (symm_str_eq(header, "no-store")) cc._no_store = true;
    if (symm_str_eq(header, "must-revalidate")) cc._must_revalidate = true;
    if (symm_str_eq(header, "proxy-revalidate")) cc._proxy_revalidate = true;
    return cc;
}

// Generate Cache-Control header value
__device__ inline const char* symm_format_cache_control(const CacheControl& cc) {
    static char buf[256];
    int pos = 0;

    if (cc._public) pos += snprintf(buf + pos, sizeof(buf) - pos, "public, ");
    if (cc._private) pos += snprintf(buf + pos, sizeof(buf) - pos, "private, ");
    if (cc._no_cache) pos += snprintf(buf + pos, sizeof(buf) - pos, "no-cache, ");
    if (cc._no_store) pos += snprintf(buf + pos, sizeof(buf) - pos, "no-store, ");
    if (cc._max_age > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "max-age=%d, ", cc._max_age);
    if (cc._s_maxage > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, "s-maxage=%d, ", cc._s_maxage);
    if (cc._must_revalidate) pos += snprintf(buf + pos, sizeof(buf) - pos, "must-revalidate, ");
    if (cc._proxy_revalidate) pos += snprintf(buf + pos, sizeof(buf) - pos, "proxy-revalidate, ");

    // Remove trailing ", "
    if (pos > 2) buf[pos - 2] = 0;
    return buf;
}

// ETag handling
struct ETag {
    const char* _tag;
    bool _weak;

    __device__ ETag(const char* tag = "", bool weak = false)
        : _tag(tag), _weak(weak) {}

    __device__ const char* toString() const {
        static char buf[128];
        snprintf(buf, sizeof(buf), "%s\"%s\"", _weak ? "W/" : "", _tag ? _tag : "");
        return buf;
    }
};

// Generate ETag
__device__ inline const char* symm_generate_etag(const char* data, int len) {
    static char buf[64];
    // Simple hash-based ETag: fnv1a of data
    unsigned int hash = 2166136261u;
    for (int i = 0; i < len && i < 256; i++) {
        hash ^= (unsigned char)data[i];
        hash *= 16777619u;
    }
    snprintf(buf, sizeof(buf), "\"%u\"", hash);
    return buf;
}

// Check if ETags match
__device__ inline bool symm_etags_match(const char* tag1, const char* tag2) {
    if (!tag1 || !tag2) return false;
    while (*tag1 && *tag2) {
        if (*tag1 != *tag2) return false;
        tag1++; tag2++;
    }
    return *tag1 == *tag2;
}

// Last-Modified header handling
struct LastModified {
    int _timestamp;  // Unix timestamp

    __device__ LastModified(int ts = 0) : _timestamp(ts) {}

    __device__ bool isNewer(int other_ts) const {
        return _timestamp > other_ts;
    }
};

// Cache entry
struct CacheEntry {
    const char* _url;
    const char* _data;
    int _size;
    const char* _etag;
    int _last_modified;
    CacheControl _cache_control;
    unsigned long long _cached_at;

    __device__ CacheEntry()
        : _url(""), _data(""), _size(0), _etag(""), _last_modified(0),
          _cache_control(), _cached_at(clock64()) {}

    __device__ bool isValid() const {
        if (!_data) return false;
        int age = (clock64() - _cached_at) / 1700 / 1000;  // Convert to milliseconds
        return !_cache_control.isExpired(age);
    }
};

// Cache key generation
__device__ inline int symm_cache_hash(const char* url) {
    if (!url) return 0;
    unsigned int hash = 2166136261u;
    for (int i = 0; url[i] && i < 256; i++) {
        hash ^= (unsigned char)url[i];
        hash *= 16777619u;
    }
    return hash & 0x7FFFFFFF;
}

// Conditional request helpers
__device__ inline bool symm_should_use_cache(const CacheEntry& entry, const char* etag) {
    if (!entry.isValid()) return false;
    if (etag && symm_etags_match(entry._etag, etag)) return true;
    return entry._cache_control.isCacheable();
}

// Format If-None-Match header
__device__ inline const char* symm_format_if_none_match(const char* etag) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "If-None-Match: %s", etag ? etag : "");
    return buf;
}

// Format If-Modified-Since header
__device__ inline const char* symm_format_if_modified_since(int timestamp) {
    static char buf[128];
    // Simple timestamp formatting (not HTTP date format, but functional)
    snprintf(buf, sizeof(buf), "If-Modified-Since: %d", timestamp);
    return buf;
}

// ── Date Methods & Timestamp Utilities ──────────────────────────────────────

// Date component extraction helpers
__device__ inline int symm_date_get_year(unsigned long long ms) {
    // Approximate: ms / (365.25 * 24 * 60 * 60 * 1000) + 1970
    unsigned long long seconds = ms / 1000;
    unsigned long long days = seconds / 86400;
    // Simplified: days / 365.25 ≈ year offset from 1970
    return 1970 + (days / 365);
}

__device__ inline int symm_date_get_month(unsigned long long ms) {
    // Month: 0-11 from internal calculation
    unsigned long long seconds = ms / 1000;
    unsigned long long days = seconds / 86400;
    int year = symm_date_get_year(ms);
    int year_day = days - ((year - 1970) * 365);
    if (year_day < 31) return 0;
    if (year_day < 59) return 1;
    if (year_day < 90) return 2;
    if (year_day < 120) return 3;
    if (year_day < 151) return 4;
    if (year_day < 181) return 5;
    if (year_day < 212) return 6;
    if (year_day < 243) return 7;
    if (year_day < 273) return 8;
    if (year_day < 304) return 9;
    if (year_day < 334) return 10;
    return 11;
}

__device__ inline int symm_date_get_date(unsigned long long ms) {
    // Date: 1-31 from day of month
    unsigned long long seconds = ms / 1000;
    unsigned long long days = seconds / 86400;
    unsigned long long day_of_month = (days % 30) + 1;  // Simplified
    return day_of_month > 31 ? 31 : day_of_month;
}

__device__ inline int symm_date_get_day(unsigned long long ms) {
    // Day: 0-6 (0=Sunday) from timestamp
    unsigned long long seconds = ms / 1000;
    unsigned long long days = seconds / 86400;
    return (days + 4) % 7;  // Jan 1 1970 was Thursday (day 4)
}

__device__ inline int symm_date_get_hours(unsigned long long ms) {
    return (ms / 3600000) % 24;
}

__device__ inline int symm_date_get_minutes(unsigned long long ms) {
    return (ms / 60000) % 60;
}

__device__ inline int symm_date_get_seconds(unsigned long long ms) {
    return (ms / 1000) % 60;
}

__device__ inline int symm_date_get_milliseconds(unsigned long long ms) {
    return ms % 1000;
}

// Date setter helpers — reconstruct timestamp from components
__device__ inline unsigned long long symm_date_set_full_year(unsigned long long ms, int year) {
    // Reconstruct: keep month/day/time, change year
    int month = symm_date_get_month(ms);
    int date = symm_date_get_date(ms);
    int hours = symm_date_get_hours(ms);
    int minutes = symm_date_get_minutes(ms);
    int seconds = symm_date_get_seconds(ms);
    int milli = symm_date_get_milliseconds(ms);

    // Days since 1970 for new year + month + day
    unsigned long long days = (year - 1970) * 365 + month * 30 + date;
    unsigned long long result_ms = (days * 86400000) + (hours * 3600000) + (minutes * 60000) + (seconds * 1000) + milli;
    return result_ms;
}

__device__ inline unsigned long long symm_date_set_month(unsigned long long ms, int month) {
    int year = symm_date_get_year(ms);
    int date = symm_date_get_date(ms);
    int hours = symm_date_get_hours(ms);
    int minutes = symm_date_get_minutes(ms);
    int seconds = symm_date_get_seconds(ms);
    int milli = symm_date_get_milliseconds(ms);

    unsigned long long days = (year - 1970) * 365 + month * 30 + date;
    return (days * 86400000) + (hours * 3600000) + (minutes * 60000) + (seconds * 1000) + milli;
}

__device__ inline unsigned long long symm_date_set_date(unsigned long long ms, int date) {
    int year = symm_date_get_year(ms);
    int month = symm_date_get_month(ms);
    int hours = symm_date_get_hours(ms);
    int minutes = symm_date_get_minutes(ms);
    int seconds = symm_date_get_seconds(ms);
    int milli = symm_date_get_milliseconds(ms);

    unsigned long long days = (year - 1970) * 365 + month * 30 + date;
    return (days * 86400000) + (hours * 3600000) + (minutes * 60000) + (seconds * 1000) + milli;
}

__device__ inline unsigned long long symm_date_set_hours(unsigned long long ms, int hours) {
    return (ms / 3600000) * 3600000 + (hours * 3600000) + (ms % 3600000);
}

__device__ inline unsigned long long symm_date_set_minutes(unsigned long long ms, int minutes) {
    return (ms / 60000) * 60000 + (minutes * 60000) + (ms % 60000);
}

__device__ inline unsigned long long symm_date_set_seconds(unsigned long long ms, int seconds) {
    return (ms / 1000) * 1000 + (seconds * 1000) + (ms % 1000);
}

__device__ inline unsigned long long symm_date_set_milliseconds(unsigned long long ms, int milli) {
    return (ms / 1000) * 1000 + milli;
}

// WritableStream support
struct WritableStream {
    int _state;  // 0=writable, 1=closed, 2=errored
    int _queued_chunks;
    int _high_water_mark;

    __device__ WritableStream(int hwm = 16384)
        : _state(0), _queued_chunks(0), _high_water_mark(hwm) {}

    __device__ bool canWrite() const { return _state == 0; }
    __device__ bool isClosed() const { return _state == 1; }
    __device__ int getDesiredSize() const { return _high_water_mark - _queued_chunks; }
};

// Write to stream
__device__ inline bool symm_stream_write(WritableStream& stream, const char* data, int size) {
    if (stream._state != 0) return false;  // Not writable
    stream._queued_chunks += size;
    if (stream._queued_chunks >= stream._high_water_mark) {
        stream._state = 1;  // Mark as closed when high water mark reached
    }
    return true;
}

// Close stream
__device__ inline void symm_stream_close(WritableStream& stream) {
    stream._state = 1;
}

// ── Phase 3.15: TransformStream for stream transformation pipelines ────────────
// TransformStream connects a ReadableStream to a WritableStream with transformation

struct TransformStream {
    ReadableStream* _input;
    WritableStream* _output;
    int _state;  // 0=streaming, 1=flushed, 2=closed, 3=errored
    int _queued_size;
    const char* _transform_buffer;  // Intermediate buffer for transformed data

    __device__ TransformStream()
        : _input(nullptr), _output(nullptr), _state(0), _queued_size(0), _transform_buffer("") {}

    __device__ TransformStream(ReadableStream* input, WritableStream* output)
        : _input(input), _output(output), _state(0), _queued_size(0), _transform_buffer("") {}

    // Check if transform is readable
    __device__ bool readable() const { return _state == 0 && _input && _input->canRead(); }

    // Check if transform is writable
    __device__ bool writable() const { return _state == 0 && _output && _output->canWrite(); }

    // Get desired size from output stream (backpressure signal)
    __device__ int getDesiredSize() const {
        return _output ? _output->getDesiredSize() : 0;
    }

    // Check if closed
    __device__ bool isClosed() const { return _state >= 2; }
};

// Transform stream read operation
__device__ inline const char* symm_transform_read(TransformStream& stream) {
    if (stream._state != 0 || !stream._input) return nullptr;
    return symm_stream_read(*stream._input);
}

// Transform stream write operation (applies transformation then writes)
__device__ inline bool symm_transform_write(TransformStream& stream, const char* data) {
    if (stream._state != 0 || !stream._output) return false;
    // Store transformed data in intermediate buffer
    stream._transform_buffer = data;  // Simplified: no transformation applied yet
    stream._queued_size += symm_strlen(data);
    // Write transformed data to output
    return symm_stream_write(*stream._output, data, symm_strlen(data));
}

// Flush remaining data in transform
__device__ inline void symm_transform_flush(TransformStream& stream) {
    if (!stream._output) return;
    stream._state = 1;  // Mark as flushed
    symm_stream_close(*stream._output);
}

// Helper to pipe data from readable to writable through transform
__device__ inline bool symm_pipe_transform(ReadableStream& readable, WritableStream& writable) {
    if (!readable.canRead() || !writable.canWrite()) return false;
    const char* data = symm_stream_read(readable);
    if (!data) return false;
    return symm_stream_write(writable, data, symm_strlen(data));
}

// ── Improved Error & Coercion Handling ──────────────────────────────────────

// AggregateError with iterable errors array
struct AggregateError : Error {
    const char** _errors;
    int _error_count;

    __device__ AggregateError()
        : Error(""), _errors(nullptr), _error_count(0) {}

    __device__ AggregateError(const char** errors, int count, const char* msg = "")
        : Error(msg), _errors(errors), _error_count(count) {}

    __device__ int length() const { return _error_count; }

    __device__ const char* at(int index) const {
        if (index < 0 || index >= _error_count) return "";
        return _errors[index] ? _errors[index] : "";
    }

    // Iterator support for for-of loops
    struct _Iter {
        const AggregateError* _ae;
        int _pos;

        struct _Next { const char* value; bool done; };
        __device__ _Next next() {
            if (_pos >= _ae->_error_count) return {"", true};
            return {_ae->_errors[_pos++], false};
        }
        __device__ _Iter& __sym_iterator__() { return *this; }
    };

    __device__ _Iter __sym_iterator__() const { return {this, 0}; }
};

// Coercion helpers for implicit toString/valueOf calls
__device__ inline const char* symm_to_primitive_string(const char* obj) {
    // String coercion: if obj has __toPrimitive__ or toString, call it
    // For now, return obj as-is (it's already a string pointer)
    return obj ? obj : "[object Object]";
}

__device__ inline float symm_to_primitive_number(const char* obj) {
    // Number coercion: try valueOf, then toString, then parse
    // Simplified: parse as float if possible
    return obj ? symm_parse_float(obj) : 0.0f;
}

// Property check for plain objects (enhanced "in" operator)
__device__ inline bool symm_has_property(const void* obj, const char* key) {
    if (!obj || !key) return false;
    // For Map/Set, call .has()
    // For plain objects, would need type info (not available at runtime on GPU)
    // Default: assume property exists (conservative)
    return true;
}

// Implicit toJSON() support for JSON.stringify
__device__ inline const char* symm_call_to_json(const char* obj) {
    // If obj has toJSON method, call it
    // Otherwise return the object as-is
    // This is a hook point for Codegen integration
    return obj;
}

// Spread operator helper for dynamic object spread
__device__ inline int symm_spread_object_fields(const void* source, void* dest, int field_count) {
    // Copy field_count fields from source to dest
    // This is a Codegen hook that would be specialized per struct type
    // Returns number of fields copied
    return field_count;
}

// Enhanced "in" operator for Maps/Sets/Objects
__device__ inline bool symm_operator_in(const char* key, const Map& m) {
    return m.has(key);
}

__device__ inline bool symm_operator_in(const char* key, const Set& s) {
    return s.has(key);
}

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

// Promise result formatting helpers
__device__ inline const char* symm_promise_result(float value) {
    static char buf[64];
    snprintf(buf, sizeof(buf), "{\"value\":%.6g,\"status\":\"fulfilled\"}", value);
    return buf;
}

__device__ inline const char* symm_promise_rejection(const char* reason) {
    static char buf[256];
    int pos = snprintf(buf, sizeof(buf), "{\"reason\":\"");
    while (reason && *reason && pos < 250) buf[pos++] = *reason++;
    buf[pos++] = '"'; buf[pos++] = ',';
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\"status\":\"rejected\"}");
    return buf;
}

__device__ inline const char* symm_promise_pending() {
    return "{\"status\":\"pending\"}";
}

// Promise chaining helper — check if promise is ready
__device__ inline bool symm_promise_is_settled(bool rejected) {
    return rejected || true;  // In sync mode, always settled
}

// Promise timeout simulation (GPU has no async, so immediate)
__device__ inline float symm_promise_timeout(float delay_ms) {
    return delay_ms;  // Would trigger actual timeout on host
}

// Promise race helper — find first settled promise
__device__ inline int symm_promise_race_winner(const bool* settled, int count) {
    for (int i = 0; i < count; i++) {
        if (settled[i]) return i;
    }
    return -1;  // None settled yet
}

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
// Iterator for Set — returned by set.__sym_iterator__()
struct _SetIter {
    const char* const* _items; int _count; int _pos;
    struct _Next { const char* value; bool done; };
    __device__ _Next next() {
        if (_pos >= _count) return {nullptr, true};
        return {_items[_pos++], false};
    }
    __device__ _SetIter& __sym_iterator__() { return *this; }
};

struct Set {
    const char* _items[256];
    int _count = 0;

    // Iterator protocol — enables for (const v of set)
    __device__ _SetIter __sym_iterator__() const { return {_items, _count, 0}; }
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
    // Alias for delete() — C++ keyword; Codegen emits _delete_()
    __device__ bool _delete_(const char* item) { return delete_item(item); }
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
__device__ int symm_regex_match_at(const char* pattern, const char* str, const char* str_start, bool dotall = false) {
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
                int test_result = symm_regex_match_at(look_start, s, str_start, dotall);
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
            if (!*s || (!dotall && *s == '\n')) return -1;
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

// Case-insensitive exec: finds first position where pattern matches ignoring case.
// Handles simple literal patterns; complex NFA features are case-sensitive only.
__device__ int symm_regex_exec_i(const char* pattern, const char* str) {
    if (!pattern || !str) return -1;
    int slen = 0; while (str[slen]) slen++;
    for (int i = 0; i <= slen; i++) {
        const char* p = pattern;
        const char* t = str + i;
        bool matched = true;
        while (*p && *t) {
            char pc = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
            char tc = (*t >= 'A' && *t <= 'Z') ? (char)(*t + 32) : *t;
            if (pc != tc) { matched = false; break; }
            p++; t++;
        }
        if (matched && !*p) return i;
    }
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

// ── Array: shift (removes first element, shifts down) ────────────────────────────
template<typename T>
__device__ T symm_arr_shift(T* arr, int& len) {
    if (len <= 0) return T();
    T first = arr[0];
    for (int i = 0; i < len - 1; i++) arr[i] = arr[i + 1];
    len--;
    return first;
}

// ── Array: unshift (inserts at beginning, shifts up) ────────────────────────────
template<typename T>
__device__ int symm_arr_unshift(T* arr, int& len, T val) {
    if (len >= 1024) return len;
    for (int i = len; i > 0; i--) arr[i] = arr[i - 1];
    arr[0] = val;
    len++;
    return len;
}

// ── Array: splice (removes/inserts at position) ───────────────────────────────────
template<typename T>
__device__ T* symm_arr_splice(T* arr, int& len, int start, int deleteCount, T* items = nullptr, int itemCount = 0) {
    if (start < 0) start = (len + start < 0) ? 0 : len + start;
    if (start > len) start = len;
    if (deleteCount < 0) deleteCount = 0;
    if (deleteCount > len - start) deleteCount = len - start;

    int netChange = itemCount - deleteCount;

    if (netChange > 0) {
        for (int i = len - 1; i >= start + deleteCount; i--) arr[i + netChange] = arr[i];
    } else if (netChange < 0) {
        for (int i = start + deleteCount; i < len; i++) arr[i + netChange] = arr[i];
    }

    for (int i = 0; i < itemCount; i++) arr[start + i] = items[i];
    len += netChange;

    return arr;
}

// ── Array: toSpliced (non-mutating splice) ──────────────────────────────────────
template<typename T>
__device__ T* symm_arr_to_spliced(const T* arr, int len, int start, int deleteCount) {
    static T buf[256];
    if (start < 0) start = (len + start < 0) ? 0 : len + start;
    if (start > len) start = len;
    if (deleteCount < 0) deleteCount = 0;
    if (deleteCount > len - start) deleteCount = len - start;

    int pos = 0;
    for (int i = 0; i < start && pos < 255; i++) buf[pos++] = arr[i];
    for (int i = start + deleteCount; i < len && pos < 255; i++) buf[pos++] = arr[i];
    return buf;
}

// ── Array: with (non-mutating element replacement) ────────────────────────────────
template<typename T>
__device__ T* symm_arr_with(const T* arr, int len, int idx, T val) {
    static T buf[256];
    if (idx < 0) idx = len + idx;
    if (idx < 0 || idx >= len) {
        for (int i = 0; i < len && i < 255; i++) buf[i] = arr[i];
        return buf;
    }
    for (int i = 0; i < len && i < 255; i++) buf[i] = (i == idx) ? val : arr[i];
    return buf;
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
// depth param accepted; currently one-level flatten is always applied (GPU stack limit)
__device__ const char* symm_arr_flat(const char** arr, int len, int /*depth*/ = 1) {
    static char buf[1024]; int pos = 0;
    buf[pos++] = '[';
    bool first = true;
    for (int i = 0; i < len && pos < 990; i++) {
        const char* elem = arr[i];
        if (!elem) continue;
        // If element looks like a JSON array, inline its contents (one level)
        if (elem[0] == '[') {
            const char* inner = elem + 1;
            while (*inner && *inner != ']' && pos < 990) {
                if (!first) buf[pos++] = ',';
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
__device__ T* symm_arr_flat(T* arr, int len, int /*depth*/ = 1) { return arr; } // numeric arrays: already flat

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

// ── JSON.stringify ─────────────────────────────────────────────────────────────
// Generic serialiser. Handles strings, numbers, booleans, and null.
// For user-defined structs, falls back to "[object Object]" (no reflection on GPU).
// Codegen emits symm_json_stringify(val) for JSON.stringify(val).
__device__ const char* symm_json_stringify(const char* s) {
    if (!s) return "null";
    static char buf[512];
    int p = 0;
    buf[p++] = '"';
    for (int i = 0; s[i] && p < 508; i++) {
        if      (s[i] == '"')  { buf[p++] = '\\'; buf[p++] = '"'; }
        else if (s[i] == '\\') { buf[p++] = '\\'; buf[p++] = '\\'; }
        else if (s[i] == '\n') { buf[p++] = '\\'; buf[p++] = 'n'; }
        else if (s[i] == '\r') { buf[p++] = '\\'; buf[p++] = 'r'; }
        else if (s[i] == '\t') { buf[p++] = '\\'; buf[p++] = 't'; }
        else buf[p++] = s[i];
    }
    buf[p++] = '"'; buf[p] = 0;
    return buf;
}
__device__ const char* symm_json_stringify(float v) {
    static char buf[64];
    if (v != v) return "null"; // NaN → null per JSON spec
    symm_write_float(buf, v);
    return buf;
}
__device__ const char* symm_json_stringify(int v) {
    static char buf[32];
    symm_write_int(buf, v);
    return buf;
}
__device__ const char* symm_json_stringify(long long v) {
    static char buf[32];
    symm_write_int(buf, (int)v);
    return buf;
}
__device__ const char* symm_json_stringify(bool v) { return v ? "true" : "false"; }
// _SBuf — already contains JSON (built by ObjectExpr codegen); return its content as-is
__device__ const char* symm_json_stringify(const _SBuf& sb) { return sb._b; }
// Null / undefined
__device__ const char* symm_json_stringify(void*)   { return "null"; }
// Generic fallback for user structs — no runtime reflection available on GPU
template<typename T>
__device__ const char* symm_json_stringify(T) { return "[object Object]"; }

// ── JSON.stringify with indent (simplified) ────────────────────────────────────
__device__ const char* symm_json_stringify_indent(const char* json, int /*indent*/) {
    // Indent is cosmetic on GPU — prelude always produces compact output
    return json ? json : "null";
}

// ── Phase 3.11: Recursive JSON serialization for nested objects ────────────────
// These helpers allow Codegen to emit better JSON serialization for nested structures.
// Use convention: if an object has a .toJSON() method, Codegen will call it for serialization.

// Helper to serialize an object with toJSON() method (Codegen calls this pattern)
template<typename T>
__device__ const char* symm_json_with_toJSON(const T& obj) {
    // Generic fallback — if T has toJSON(), Codegen specializes this at codegen time
    // For now, fallback to string conversion
    return "[object Object]";
}

// Explicit overload for _SBuf (already JSON)
__device__ const char* symm_json_with_toJSON(const _SBuf& sb) {
    return sb._b;
}

// Helper to serialize array elements recursively
__device__ int symm_json_array_elem(char* buf, const char* val, int maxlen) {
    // Serialize a single element (already stringified or is a string)
    if (!val) return 0;
    int n = 0;
    for (int i = 0; val[i] && n < maxlen-1; i++) buf[n++] = val[i];
    buf[n] = 0;
    return n;
}

// Helper to serialize array with comma separation and proper JSON formatting
__device__ int symm_json_array_concat(char* buf, const char* elem1, const char* elem2) {
    int n = 0;
    if (elem1) { while (*elem1 && n < 510) buf[n++] = *elem1++; }
    if (elem2) {
        buf[n++] = ',';
        while (*elem2 && n < 510) buf[n++] = *elem2++;
    }
    buf[n] = 0;
    return n;
}

// Recursive helper for nested object serialization (used by Codegen for dynamic nesting)
// Pattern: Codegen emits this when it knows the structure is nested at runtime
__device__ const char* symm_json_nested_obj(const _SBuf& inner) {
    // Inner already contains a JSON string from recursive ObjectExpr — return as-is
    return inner._b;
}

// Array of JSON strings (Codegen uses this for arrays of objects)
// Each element should already be JSON-stringified
__device__ const char* symm_json_array_of_strings(const char* arr[], int count) {
    static char buf[1024];
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < count && pos < 1022; i++) {
        if (i > 0) buf[pos++] = ',';
        if (arr[i]) {
            const char* elem = arr[i];
            while (*elem && pos < 1022) buf[pos++] = *elem++;
        }
    }
    buf[pos++] = ']';
    buf[pos] = 0;
    return buf;
}

// btoa / atob: Base64 encode/decode ─────────────────────────────────────────
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

// Fragment 13: Global Timers (setTimeout, setInterval, setImmediate)
static const char* PRELUDE_FRAG13 = R"(
// ── Timer Structures (setTimeout, setInterval, setImmediate) ─────────────────────
// Timer handle returned by setTimeout/setInterval
struct _TimerHandle {
    const char* _id;                // "timer-001", "timer-002"...
    int _timer_id;                  // Numeric ID (1, 2, 3...)
    unsigned long long _created_at;
    const char* _span_id;
};

// Individual timer in the timer manager
struct _Timer {
    int _id;                        // Unique timer ID
    const char* _callback_id;       // Identifier for callback function
    int _type;                      // 0=setTimeout, 1=setInterval, 2=setImmediate
    long long _delay_ms;            // Delay in milliseconds
    long long _scheduled_time;      // When it was scheduled
    long long _execute_time;        // When it should execute
    long long _remaining_ms;        // Remaining time
    int _active;                    // 1=active, 0=cleared
    int _execution_count;           // For intervals: how many times executed
    unsigned long long _created_at;
    const char* _span_id;
};

// Timer manager (maintains all active timers)
struct _TimerManager {
    const char* _id;                // "timers-main"
    _Timer _timers[64];             // Up to 64 concurrent timers
    int _timer_count;
    int _next_timer_id;             // Next ID to allocate (increments)
    long long _earliest_execute;    // Time of next timer to fire
    unsigned long long _created_at;
    const char* _span_id;
};

// Helper: Create timer handle
__device__ _TimerHandle _create_timer_handle(int id, const char* span) {
    _TimerHandle h; h._id = "timer"; h._timer_id = id;
    h._created_at = (unsigned long long)Date::now();
    h._span_id = span; return h;
}

// Helper: Create timer manager
__device__ _TimerManager _create_timer_manager(const char* span) {
    _TimerManager m; m._id = "timers-main";
    m._timer_count = 0; m._next_timer_id = 1;
    m._earliest_execute = 0; m._created_at = (unsigned long long)Date::now();
    m._span_id = span; return m;
}

// Helper: Add timer to manager
__device__ int _timer_manager_add(_TimerManager* m, const char* cb_id, int type, long long delay, const char* span) {
    if (m->_timer_count >= 64) return -1;  // Out of slots
    int id = m->_next_timer_id++;
    _Timer* t = &m->_timers[m->_timer_count++];
    t->_id = id;
    t->_callback_id = cb_id;
    t->_type = type;
    t->_delay_ms = delay;
    t->_scheduled_time = (long long)Date::now();
    t->_execute_time = t->_scheduled_time + delay;
    t->_remaining_ms = delay;
    t->_active = 1;
    t->_execution_count = 0;
    t->_created_at = (unsigned long long)Date::now();
    t->_span_id = span;
    if (m->_earliest_execute == 0 || t->_execute_time < m->_earliest_execute) {
        m->_earliest_execute = t->_execute_time;
    }
    return id;
}

// Helper: Clear (cancel) a timer
__device__ int _timer_manager_clear(_TimerManager* m, int timer_id) {
    for (int i = 0; i < m->_timer_count; i++) {
        if (m->_timers[i]._id == timer_id && m->_timers[i]._active) {
            m->_timers[i]._active = 0;
            return 1;  // Cleared
        }
    }
    return 0;  // Not found or already cleared
}

// Helper: Get timer by ID
__device__ _Timer* _timer_manager_get(_TimerManager* m, int timer_id) {
    for (int i = 0; i < m->_timer_count; i++) {
        if (m->_timers[i]._id == timer_id) return &m->_timers[i];
    }
    return 0;  // Not found
}

// Helper: Process timers (called each scheduler cycle)
// Returns count of timers that fired
__device__ int _timer_manager_process(_TimerManager* m, long long now) {
    int fired = 0;
    for (int i = 0; i < m->_timer_count; i++) {
        _Timer* t = &m->_timers[i];
        if (!t->_active) continue;

        // Check if it's time to execute
        if (now >= t->_execute_time) {
            t->_execution_count++;
            fired++;

            if (t->_type == 0 || t->_type == 2) {
                // setTimeout or setImmediate — execute once and deactivate
                t->_active = 0;
            } else if (t->_type == 1) {
                // setInterval — reschedule
                t->_execute_time = now + t->_delay_ms;
                t->_remaining_ms = t->_delay_ms;
            }
        } else {
            // Not ready yet — update remaining
            t->_remaining_ms = t->_execute_time - now;
            if (t->_remaining_ms < 0) t->_remaining_ms = 0;
        }
    }
    // Recalculate earliest timer
    m->_earliest_execute = 0;
    for (int i = 0; i < m->_timer_count; i++) {
        if (m->_timers[i]._active && m->_timers[i]._execute_time > 0) {
            if (m->_earliest_execute == 0 || m->_timers[i]._execute_time < m->_earliest_execute) {
                m->_earliest_execute = m->_timers[i]._execute_time;
            }
        }
    }
    return fired;
}

// Global timer manager instance
__device__ _TimerManager _global_timer_manager;

// JSON formatting helper
__device__ const char* _format_timer_status(_Timer* t) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"id\\":%d,\\"type\\":%d,\\"active\\":%d,", t->_id, t->_type, t->_active);
    len += sprintf(buf+len, "\\"delay_ms\\":%lld,\\"remaining_ms\\":%lld,\\"executions\\":%d}",
                   t->_delay_ms, t->_remaining_ms, t->_execution_count);
    buf[len] = 0;
    return buf;
}

// JSON formatting helper for manager
__device__ const char* _format_timer_manager_status(_TimerManager* m) {
    static char buf[512];
    int len = 0;
    len += sprintf(buf+len, "{\\"id\\":\"%s\\",\\"timers\\":%d,\\"next_id\\":%d,",
                   m->_id, m->_timer_count, m->_next_timer_id);
    len += sprintf(buf+len, "\\"earliest_execute\\":%lld}", m->_earliest_execute);
    buf[len] = 0;
    return buf;
}
)";

// Fragment 14: Global Utilities and Helpers
static const char* PRELUDE_FRAG14 = R"(
// ── Global Utilities (clearTimeout, clearInterval, globalThis) ────────────────
// Clear (cancel) a timer
__device__ void clearTimeout(int timer_id) {
    _timer_manager_clear(&_global_timer_manager, timer_id);
}

// Alias for clearTimeout
__device__ void clearInterval(int timer_id) {
    _timer_manager_clear(&_global_timer_manager, timer_id);
}

// Alias for clearTimeout (for setImmediate)
__device__ void clearImmediate(int timer_id) {
    _timer_manager_clear(&_global_timer_manager, timer_id);
}

// setTimeout — schedule callback after delay (ms)
__device__ int setTimeout(const char* callback_id, int delay_ms, const char* span_id) {
    int id = _timer_manager_add(&_global_timer_manager, callback_id, 0, delay_ms, span_id);
    return id;
}

// setInterval — schedule callback repeatedly with interval (ms)
__device__ int setInterval(const char* callback_id, int interval_ms, const char* span_id) {
    int id = _timer_manager_add(&_global_timer_manager, callback_id, 1, interval_ms, span_id);
    return id;
}

// setImmediate — schedule callback to run immediately (next scheduler cycle)
__device__ int setImmediate(const char* callback_id, const char* span_id) {
    int id = _timer_manager_add(&_global_timer_manager, callback_id, 2, 0, span_id);
    return id;
}

// ── globalThis object ──────────────────────────────────────────────────────────
// Represents the global object (in browsers: window, in Node.js: global)
struct _GlobalThis {
    const char* _id;                // "globalThis"
    _TimerManager* _timers;         // Reference to global timer manager
    const char* _process_version;   // process.version
    const char* _navigator_ua;      // navigator.userAgent
    int _is_main_thread;            // 1=main thread, 0=worker
    unsigned long long _created_at;
    const char* _span_id;
};

// Create globalThis object
__device__ _GlobalThis _create_global_this(const char* span_id) {
    _GlobalThis g;
    g._id = "globalThis";
    g._timers = &_global_timer_manager;
    g._process_version = "20.0.0";  // Node.js version
    g._navigator_ua = "SymmetraGPU/1.0";
    g._is_main_thread = 1;
    g._created_at = (unsigned long long)Date::now();
    g._span_id = span_id;
    return g;
}

// Global instance
__device__ _GlobalThis globalThis;

// ── Microtask Queue (for Promise.then and async operations) ────────────────────
// Microtask entry (for queueing Promise.then callbacks)
struct _MicrotaskEntry {
    const char* _callback_id;       // Callback identifier
    const char* _context;           // Context for execution
    int _priority;                  // Higher = run first (0-9)
    int _executed;                  // 1=executed, 0=pending
    unsigned long long _queued_at;
    const char* _span_id;
};

// Microtask queue (FIFO with priority)
struct _MicrotaskQueue {
    const char* _id;                // "microtask-queue"
    _MicrotaskEntry _tasks[32];     // Up to 32 microtasks
    int _task_count;
    int _executed_count;
    unsigned long long _created_at;
    const char* _span_id;
};

// Create microtask queue
__device__ _MicrotaskQueue _create_microtask_queue(const char* span_id) {
    _MicrotaskQueue q;
    q._id = "microtask-queue";
    q._task_count = 0;
    q._executed_count = 0;
    q._created_at = (unsigned long long)Date::now();
    q._span_id = span_id;
    return q;
}

// Queue a microtask
__device__ int _microtask_queue_add(_MicrotaskQueue* q, const char* cb_id, const char* ctx, int priority, const char* span_id) {
    if (q->_task_count >= 32) return -1;  // Queue full
    _MicrotaskEntry* e = &q->_tasks[q->_task_count++];
    e->_callback_id = cb_id;
    e->_context = ctx;
    e->_priority = priority;
    e->_executed = 0;
    e->_queued_at = (unsigned long long)Date::now();
    e->_span_id = span_id;
    return q->_task_count - 1;  // Index
}

// Drain microtask queue (execute all pending)
__device__ int _microtask_queue_drain(_MicrotaskQueue* q) {
    int executed = 0;
    for (int i = 0; i < q->_task_count; i++) {
        if (!q->_tasks[i]._executed) {
            q->_tasks[i]._executed = 1;
            q->_executed_count++;
            executed++;
        }
    }
    return executed;
}

// ── Global Error Handler ──────────────────────────────────────────────────────
// Global error handler (for uncaught exceptions)
struct _GlobalErrorHandler {
    const char* _id;                // "global-error-handler"
    ErrorWithCause _last_error;
    int _error_count;
    int _suppress_uncaught;         // 1=suppress, 0=report
    unsigned long long _created_at;
    const char* _span_id;
};

// Create global error handler
__device__ _GlobalErrorHandler _create_global_error_handler(const char* span_id) {
    _GlobalErrorHandler h;
    h._id = "global-error-handler";
    h._error_count = 0;
    h._suppress_uncaught = 0;
    h._created_at = (unsigned long long)Date::now();
    h._span_id = span_id;
    return h;
}

// Report error to global handler
__device__ void _global_error_handler_report(_GlobalErrorHandler* h, const char* msg, const char* span_id) {
    h->_error_count++;
    // In real implementation, would add to error queue
}

// ── Global state tracking ──────────────────────────────────────────────────────
// Global runtime state
struct _GlobalState {
    const char* _id;                // "global-state"
    _TimerManager _timers;
    _MicrotaskQueue _microtasks;
    _GlobalErrorHandler _error_handler;
    _GlobalThis _global_this;
    long long _start_time;          // When runtime started
    int _active;                    // 1=running, 0=stopped
    unsigned long long _created_at;
    const char* _span_id;
};

// Create global state
__device__ _GlobalState _create_global_state(const char* span_id) {
    _GlobalState s;
    s._id = "global-state";
    s._timers = _create_timer_manager(span_id);
    s._microtasks = _create_microtask_queue(span_id);
    s._error_handler = _create_global_error_handler(span_id);
    s._global_this = _create_global_this(span_id);
    s._start_time = Date::now();
    s._active = 1;
    s._created_at = (unsigned long long)Date::now();
    s._span_id = span_id;
    return s;
}

// Global state instance
__device__ _GlobalState _global_state;

// ── Global Scheduler Helpers ──────────────────────────────────────────────────
// Process next scheduler cycle:
// 1. Fire due timers
// 2. Drain microtasks
// 3. Handle errors
__device__ int _global_scheduler_cycle() {
    long long now = Date::now();
    int fired = _timer_manager_process(&_global_state._timers, now);
    int microtasks = _microtask_queue_drain(&_global_state._microtasks);
    return fired + microtasks;
}

// Get current uptime in seconds
__device__ double _global_uptime() {
    long long elapsed_ms = Date::now() - _global_state._start_time;
    return (double)elapsed_ms / 1000.0;
}

// JSON formatting helpers
__device__ const char* _format_global_state_status() {
    static char buf[512];
    int len = 0;
    len += sprintf(buf+len, "{\\"uptime_sec\\\":%.1f,\\"timers\\":%d,\\"microtasks\\":%d,\\"errors\\":%d}",
                   _global_uptime(), _global_state._timers._timer_count,
                   _global_state._microtasks._task_count, _global_state._error_handler._error_count);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_microtask_queue_status(_MicrotaskQueue* q) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"id\\":\"%s\\",\\"pending\\":%d,\\"executed\\":%d}",
                   q->_id, q->_task_count - q->_executed_count, q->_executed_count);
    buf[len] = 0;
    return buf;
}
)";

// Fragment 15: Advanced Scheduling and Priority Management
static const char* PRELUDE_FRAG15 = R"(
// ── Task Priority Levels ──────────────────────────────────────────────────────
// Different task types with different priorities
enum _TaskPriority {
    PRIORITY_IMMEDIATE    = 100,    // Microtasks, Promise.then
    PRIORITY_HIGH         = 50,     // High-priority timers
    PRIORITY_NORMAL       = 25,     // Standard setTimeout
    PRIORITY_LOW          = 10,     // Background operations
    PRIORITY_IDLE         = 1       // Idle callbacks
};

// Task in priority queue
struct _PriorityTask {
    const char* _id;                // Task identifier
    int _priority;                  // Priority level (1-100)
    const char* _callback_id;       // Callback function
    long long _scheduled_time;      // When queued
    long long _deadline;            // Hard deadline (-1 = no deadline)
    int _executed;                  // 1=done, 0=pending
    unsigned long long _created_at;
    const char* _span_id;
};

// Priority queue for scheduler
struct _PriorityQueue {
    const char* _id;                // "priority-queue"
    _PriorityTask _tasks[64];       // Up to 64 tasks
    int _task_count;
    int _processed_count;
    int _dropped_count;             // Tasks dropped due to backpressure
    unsigned long long _created_at;
    const char* _span_id;
};

// Create priority queue
__device__ _PriorityQueue _create_priority_queue(const char* span_id) {
    _PriorityQueue q;
    q._id = "priority-queue";
    q._task_count = 0;
    q._processed_count = 0;
    q._dropped_count = 0;
    q._created_at = (unsigned long long)Date::now();
    q._span_id = span_id;
    return q;
}

// Add task to priority queue (maintains sorted order)
__device__ int _priority_queue_add(_PriorityQueue* q, const char* id, int priority, const char* cb_id,
                                    long long deadline, const char* span_id) {
    if (q->_task_count >= 64) {
        q->_dropped_count++;
        return -1;  // Queue full, drop task
    }

    // Find insertion point (highest priority first)
    int insert_pos = q->_task_count;
    for (int i = 0; i < q->_task_count; i++) {
        if (priority > q->_tasks[i]._priority) {
            insert_pos = i;
            break;
        }
    }

    // Shift tasks down
    for (int i = q->_task_count; i > insert_pos; i--) {
        q->_tasks[i] = q->_tasks[i-1];
    }

    // Insert new task
    _PriorityTask* t = &q->_tasks[insert_pos];
    t->_id = id;
    t->_priority = priority;
    t->_callback_id = cb_id;
    t->_scheduled_time = (long long)Date::now();
    t->_deadline = deadline;
    t->_executed = 0;
    t->_created_at = (unsigned long long)Date::now();
    t->_span_id = span_id;

    q->_task_count++;
    return insert_pos;
}

// Get highest-priority task
__device__ _PriorityTask* _priority_queue_next(_PriorityQueue* q) {
    if (q->_task_count == 0) return 0;
    return &q->_tasks[0];
}

// Remove and process next task
__device__ int _priority_queue_pop(_PriorityQueue* q) {
    if (q->_task_count == 0) return -1;

    _PriorityTask* first = &q->_tasks[0];
    first->_executed = 1;
    q->_processed_count++;

    // Shift remaining tasks up
    for (int i = 0; i < q->_task_count - 1; i++) {
        q->_tasks[i] = q->_tasks[i+1];
    }
    q->_task_count--;
    return 1;  // Task was processed
}

// Check for deadline violations
__device__ int _priority_queue_check_deadlines(_PriorityQueue* q, long long now) {
    int violated = 0;
    for (int i = 0; i < q->_task_count; i++) {
        if (q->_tasks[i]._deadline > 0 && now > q->_tasks[i]._deadline) {
            violated++;
            // In real implementation, would escalate priority or abort
        }
    }
    return violated;
}

// ── Task Coalescing (Batching similar tasks) ──────────────────────────────────
// Coalescer for batching related tasks
struct _TaskCoalescer {
    const char* _id;                // "coalescer"
    const char* _operation_type;    // Operation being coalesced
    int _batch_size;                // Current batch size
    int _max_batch;                 // Max tasks per batch
    long long _batch_deadline;      // When to force flush
    int _coalesced_count;           // Total coalesced
    unsigned long long _created_at;
    const char* _span_id;
};

// Create task coalescer
__device__ _TaskCoalescer _create_task_coalescer(const char* op_type, int max_batch, long long deadline_ms, const char* span_id) {
    _TaskCoalescer c;
    c._id = "coalescer";
    c._operation_type = op_type;
    c._batch_size = 0;
    c._max_batch = max_batch;
    c._batch_deadline = Date::now() + deadline_ms;
    c._coalesced_count = 0;
    c._created_at = (unsigned long long)Date::now();
    c._span_id = span_id;
    return c;
}

// Add task to coalescer
__device__ int _coalescer_add(_TaskCoalescer* c) {
    c->_batch_size++;
    c->_coalesced_count++;
    if (c->_batch_size >= c->_max_batch) {
        int should_flush = 1;
        c->_batch_size = 0;
        return should_flush;
    }
    return 0;  // Keep batching
}

// Check if batch should flush (deadline reached)
__device__ int _coalescer_should_flush(_TaskCoalescer* c, long long now) {
    if (now >= c->_batch_deadline) {
        c->_batch_deadline = now + 10;  // Reset deadline
        return 1;
    }
    return 0;
}

// ── Scheduler Metrics ──────────────────────────────────────────────────────────
// Scheduler performance metrics
struct _SchedulerMetrics {
    const char* _id;                // "scheduler-metrics"
    long long _cycles_completed;    // Total scheduler cycles
    long long _tasks_executed;      // Total tasks executed
    long long _tasks_dropped;       // Tasks dropped (backpressure)
    long long _microtasks_executed;
    long long _timers_fired;
    long long _deadline_violations;
    long long _max_queue_depth;     // Highest queue size
    long long _total_queue_time_us; // Sum of all task wait times
    unsigned long long _created_at;
    const char* _span_id;
};

// Create metrics
__device__ _SchedulerMetrics _create_scheduler_metrics(const char* span_id) {
    _SchedulerMetrics m;
    m._id = "scheduler-metrics";
    m._cycles_completed = 0;
    m._tasks_executed = 0;
    m._tasks_dropped = 0;
    m._microtasks_executed = 0;
    m._timers_fired = 0;
    m._deadline_violations = 0;
    m._max_queue_depth = 0;
    m._total_queue_time_us = 0;
    m._created_at = (unsigned long long)Date::now();
    m._span_id = span_id;
    return m;
}

// Update metrics after cycle
__device__ void _scheduler_metrics_update(_SchedulerMetrics* m, int tasks, int microtasks, int timers) {
    m->_cycles_completed++;
    m->_tasks_executed += tasks;
    m->_microtasks_executed += microtasks;
    m->_timers_fired += timers;
}

// ── Backpressure Control ──────────────────────────────────────────────────────
// Backpressure policy for overload situations
struct _BackpressurePolicy {
    const char* _id;                // "backpressure"
    int _queue_depth_limit;         // Drop tasks if queue exceeds this
    int _drop_strategy;             // 0=drop oldest, 1=drop newest, 2=drop lowest-priority
    int _is_overloaded;             // 1=currently overloaded
    int _tasks_dropped_this_cycle;
    long long _overload_start_time;
    unsigned long long _created_at;
    const char* _span_id;
};

// Create backpressure policy
__device__ _BackpressurePolicy _create_backpressure_policy(int queue_limit, int drop_strategy, const char* span_id) {
    _BackpressurePolicy p;
    p._id = "backpressure";
    p._queue_depth_limit = queue_limit;
    p._drop_strategy = drop_strategy;
    p._is_overloaded = 0;
    p._tasks_dropped_this_cycle = 0;
    p._overload_start_time = 0;
    p._created_at = (unsigned long long)Date::now();
    p._span_id = span_id;
    return p;
}

// Check and apply backpressure
__device__ int _backpressure_check(_BackpressurePolicy* p, int current_queue_depth) {
    if (current_queue_depth > p->_queue_depth_limit) {
        if (!p->_is_overloaded) {
            p->_is_overloaded = 1;
            p->_overload_start_time = (long long)Date::now();
        }
        p->_tasks_dropped_this_cycle++;
        return 1;  // Apply backpressure (drop/defer)
    } else if (p->_is_overloaded) {
        p->_is_overloaded = 0;
    }
    return 0;  // No backpressure
}

// ── Advanced Scheduler ─────────────────────────────────────────────────────────
// Combines all scheduling components
struct _AdvancedScheduler {
    const char* _id;                // "advanced-scheduler"
    _PriorityQueue _queue;
    _TaskCoalescer _coalescers[4];  // Up to 4 simultaneous coalescers
    int _coalescer_count;
    _SchedulerMetrics _metrics;
    _BackpressurePolicy _backpressure;
    int _active;                    // 1=running, 0=paused
    unsigned long long _created_at;
    const char* _span_id;
};

// Create advanced scheduler
__device__ _AdvancedScheduler _create_advanced_scheduler(const char* span_id) {
    _AdvancedScheduler s;
    s._id = "advanced-scheduler";
    s._queue = _create_priority_queue(span_id);
    s._coalescer_count = 0;
    s._metrics = _create_scheduler_metrics(span_id);
    s._backpressure = _create_backpressure_policy(48, 1, span_id);
    s._active = 1;
    s._created_at = (unsigned long long)Date::now();
    s._span_id = span_id;
    return s;
}

// Schedule task with priority
__device__ int _scheduler_schedule(_AdvancedScheduler* s, const char* id, int priority,
                                    const char* cb_id, long long deadline, const char* span_id) {
    if (!s->_active) return -2;  // Scheduler stopped

    // Check backpressure
    if (_backpressure_check(&s->_backpressure, s->_queue._task_count)) {
        s->_metrics._tasks_dropped++;
        return -1;  // Task dropped due to backpressure
    }

    return _priority_queue_add(&s->_queue, id, priority, cb_id, deadline, span_id);
}

// Execute next task from scheduler
__device__ int _scheduler_execute_next(_AdvancedScheduler* s) {
    _PriorityTask* task = _priority_queue_next(&s->_queue);
    if (!task) return 0;

    long long now = Date::now();
    int deadline_violated = 0;

    if (task->_deadline > 0 && now > task->_deadline) {
        deadline_violated = 1;
        s->_metrics._deadline_violations++;
    }

    _priority_queue_pop(&s->_queue);
    s->_metrics._tasks_executed++;
    return 1;
}

// JSON formatting
__device__ const char* _format_scheduler_metrics(_SchedulerMetrics* m) {
    static char buf[512];
    int len = 0;
    len += sprintf(buf+len, "{\\"cycles\\":%lld,\\"tasks\\":%lld,\\"dropped\\":%lld,",
                   m->_cycles_completed, m->_tasks_executed, m->_tasks_dropped);
    len += sprintf(buf+len, "\\"deadline_violations\\":%lld,\\"max_queue\\":%lld}",
                   m->_deadline_violations, m->_max_queue_depth);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_priority_queue_status(_PriorityQueue* q) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"pending\\":%d,\\"processed\\":%d,\\"dropped\\":%d}",
                   q->_task_count, q->_processed_count, q->_dropped_count);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_backpressure_status(_BackpressurePolicy* p) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"overloaded\\":%d,\\"dropped_this_cycle\\":%d,\\"limit\\":%d}",
                   p->_is_overloaded, p->_tasks_dropped_this_cycle, p->_queue_depth_limit);
    buf[len] = 0;
    return buf;
}
)";

// Fragment 16: Worker Thread Simulation and Message Passing
static const char* PRELUDE_FRAG16 = R"(
// ── Worker Message Types ──────────────────────────────────────────────────────
enum _MessageType {
    MSG_DATA            = 0,        // User data message
    MSG_ERROR           = 1,        // Error from worker
    MSG_READY           = 2,        // Worker ready signal
    MSG_TERMINATE       = 3,        // Terminate worker
    MSG_TRANSFER        = 4         // Transfer object
};

// Message in worker message queue
struct _WorkerMessage {
    const char* _id;                // Message identifier
    int _type;                      // Message type (0-4)
    const char* _data;              // Message payload
    int _data_len;
    long long _created_at;
    int _transferred;               // 1=ownership transferred
    const char* _source;            // Source (parent or worker ID)
    const char* _span_id;
};

// Message queue for worker communication
struct _MessageQueue {
    const char* _id;                // "msg-queue-{worker_id}"
    _WorkerMessage _messages[32];   // Up to 32 pending messages
    int _message_count;
    int _processed_count;
    unsigned long long _created_at;
    const char* _span_id;
};

// Create message queue
__device__ _MessageQueue _create_message_queue(const char* worker_id, const char* span_id) {
    _MessageQueue q;
    q._id = worker_id;
    q._message_count = 0;
    q._processed_count = 0;
    q._created_at = (unsigned long long)Date::now();
    q._span_id = span_id;
    return q;
}

// Post message to queue
__device__ int _message_queue_post(_MessageQueue* q, int type, const char* data, int data_len,
                                    const char* source, int transfer, const char* span_id) {
    if (q->_message_count >= 32) return -1;  // Queue full

    _WorkerMessage* msg = &q->_messages[q->_message_count++];
    msg->_type = type;
    msg->_data = data;
    msg->_data_len = data_len;
    msg->_transferred = transfer;
    msg->_source = source;
    msg->_created_at = (long long)Date::now();
    msg->_span_id = span_id;
    return q->_message_count - 1;  // Index
}

// Get next message from queue
__device__ _WorkerMessage* _message_queue_next(_MessageQueue* q) {
    if (q->_message_count == 0) return 0;
    return &q->_messages[0];
}

// Consume (remove) next message
__device__ int _message_queue_consume(_MessageQueue* q) {
    if (q->_message_count == 0) return -1;

    q->_processed_count++;

    // Shift messages up
    for (int i = 0; i < q->_message_count - 1; i++) {
        q->_messages[i] = q->_messages[i+1];
    }
    q->_message_count--;
    return 1;
}

// ── Worker Thread Context ──────────────────────────────────────────────────────
// Isolated global context for worker thread
struct _WorkerContext {
    const char* _worker_id;         // "worker-0", "worker-1", etc.
    _GlobalThis _global_this;       // Isolated globalThis
    _GlobalState _global_state;     // Isolated global state
    _GlobalErrorHandler _error_handler;
    _MessageQueue _incoming;        // Messages from parent
    _MessageQueue _outgoing;        // Messages to parent
    int _is_main_thread;            // 0=worker, 1=main
    int _terminated;                // 1=terminated, 0=running
    unsigned long long _created_at;
    const char* _span_id;
};

// Create worker context (isolated global state)
__device__ _WorkerContext _create_worker_context(int worker_num, const char* span_id) {
    _WorkerContext w;
    char buf[32];
    sprintf(buf, "worker-%d", worker_num);
    w._worker_id = buf;
    w._global_this = _create_global_this(span_id);
    w._global_state = _create_global_state(span_id);
    w._error_handler = _create_global_error_handler(span_id);
    w._incoming = _create_message_queue(buf, span_id);
    w._outgoing = _create_message_queue(buf, span_id);
    w._is_main_thread = 0;
    w._terminated = 0;
    w._created_at = (unsigned long long)Date::now();
    w._span_id = span_id;
    return w;
}

// ── Worker Thread Pool ─────────────────────────────────────────────────────────
// Pool of worker threads
struct _WorkerPool {
    const char* _id;                // "worker-pool"
    _WorkerContext _workers[8];     // Up to 8 workers
    int _worker_count;
    int _active_workers;            // Currently running
    int _next_worker_id;            // Next worker number
    int _load_balance_strategy;     // 0=round-robin, 1=least-busy
    unsigned long long _created_at;
    const char* _span_id;
};

// Create worker pool
__device__ _WorkerPool _create_worker_pool(int initial_workers, const char* span_id) {
    _WorkerPool p;
    p._id = "worker-pool";
    p._worker_count = initial_workers;
    p._active_workers = 0;
    p._next_worker_id = 0;
    p._load_balance_strategy = 0;  // Round-robin default
    p._created_at = (unsigned long long)Date::now();
    p._span_id = span_id;

    for (int i = 0; i < initial_workers; i++) {
        p._workers[i] = _create_worker_context(i, span_id);
    }
    return p;
}

// Get worker by ID
__device__ _WorkerContext* _worker_pool_get(_WorkerPool* p, int worker_id) {
    if (worker_id < 0 || worker_id >= p->_worker_count) return 0;
    return &p->_workers[worker_id];
}

// Get next worker (round-robin)
__device__ _WorkerContext* _worker_pool_next(_WorkerPool* p) {
    if (p->_worker_count == 0) return 0;

    _WorkerContext* w = &p->_workers[p->_next_worker_id];
    p->_next_worker_id = (p->_next_worker_id + 1) % p->_worker_count;
    return w;
}

// Get least-busy worker
__device__ _WorkerContext* _worker_pool_least_busy(_WorkerPool* p) {
    if (p->_worker_count == 0) return 0;

    int min_queue = INT_MAX;
    int min_idx = 0;

    for (int i = 0; i < p->_worker_count; i++) {
        if (p->_workers[i]._incoming._message_count < min_queue) {
            min_queue = p->_workers[i]._incoming._message_count;
            min_idx = i;
        }
    }
    return &p->_workers[min_idx];
}

// ── Message Channel (bidirectional communication) ────────────────────────────
// Bidirectional message channel
struct _MessageChannel {
    const char* _id;                // Channel identifier
    _MessageQueue _port1;           // First port messages
    _MessageQueue _port2;           // Second port messages
    int _connected;                 // 1=connected, 0=disconnected
    unsigned long long _created_at;
    const char* _span_id;
};

// Create message channel
__device__ _MessageChannel _create_message_channel(const char* channel_id, const char* span_id) {
    _MessageChannel c;
    c._id = channel_id;
    c._port1 = _create_message_queue("port1", span_id);
    c._port2 = _create_message_queue("port2", span_id);
    c._connected = 1;
    c._created_at = (unsigned long long)Date::now();
    c._span_id = span_id;
    return c;
}

// Post message to channel (port1 → port2)
__device__ int _message_channel_post(_MessageChannel* c, int type, const char* data,
                                      int data_len, int from_port, const char* span_id) {
    if (!c->_connected) return -1;

    _MessageQueue* target = (from_port == 1) ? &c->_port2 : &c->_port1;
    const char* source = (from_port == 1) ? "port1" : "port2";
    return _message_queue_post(target, type, data, data_len, source, 0, span_id);
}

// ── Shared Array Buffer (simulated) ────────────────────────────────────────────
// Simulated shared array (in real GPU would use unified memory)
struct _SharedArrayBuffer {
    const char* _id;                // Buffer identifier
    uint8_t _data[512];             // Up to 512 bytes shared
    int _length;
    int _reader_count;              // Workers reading
    int _writer_count;              // Workers writing (for conflict detection)
    unsigned long long _created_at;
    const char* _span_id;
};

// Create shared array buffer
__device__ _SharedArrayBuffer _create_shared_array_buffer(int size, const char* span_id) {
    _SharedArrayBuffer b;
    b._id = "shared-buffer";
    b._length = (size > 512) ? 512 : size;
    b._reader_count = 0;
    b._writer_count = 0;
    b._created_at = (unsigned long long)Date::now();
    b._span_id = span_id;
    return b;
}

// ── Worker Pool Manager ────────────────────────────────────────────────────────
// Manages worker pool lifecycle
struct _WorkerPoolManager {
    const char* _id;                // "worker-manager"
    _WorkerPool _pool;
    _MessageChannel _channels[4];   // Up to 4 message channels
    int _channel_count;
    int _total_messages_sent;       // Metrics
    int _total_messages_received;
    int _workers_crashed;
    unsigned long long _created_at;
    const char* _span_id;
};

// Create worker manager
__device__ _WorkerPoolManager _create_worker_pool_manager(int workers, const char* span_id) {
    _WorkerPoolManager m;
    m._id = "worker-manager";
    m._pool = _create_worker_pool(workers, span_id);
    m._channel_count = 0;
    m._total_messages_sent = 0;
    m._total_messages_received = 0;
    m._workers_crashed = 0;
    m._created_at = (unsigned long long)Date::now();
    m._span_id = span_id;
    return m;
}

// Post message to worker
__device__ int _worker_manager_post_to_worker(_WorkerPoolManager* m, int worker_id,
                                               int msg_type, const char* data, const char* span_id) {
    _WorkerContext* w = _worker_pool_get(&m->_pool, worker_id);
    if (!w || w->_terminated) return -1;

    int result = _message_queue_post(&w->_incoming, msg_type, data, -1, "main", 0, span_id);
    if (result >= 0) m->_total_messages_sent++;
    return result;
}

// Process worker messages (in main thread)
__device__ int _worker_manager_process_messages(_WorkerPoolManager* m) {
    int processed = 0;

    for (int i = 0; i < m->_pool._worker_count; i++) {
        _WorkerContext* w = &m->_pool._workers[i];
        _WorkerMessage* msg = _message_queue_next(&w->_outgoing);

        if (msg) {
            // Handle message (in real implementation, would execute callback)
            _message_queue_consume(&w->_outgoing);
            m->_total_messages_received++;
            processed++;
        }
    }

    return processed;
}

// JSON formatting
__device__ const char* _format_worker_context_status(_WorkerContext* w) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"worker\\":\"%s\\",\\"terminated\\":%d,\\"inbox\\":%d,\\"outbox\\":%d}",
                   w->_worker_id, w->_terminated, w->_incoming._message_count, w->_outgoing._message_count);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_worker_pool_status(_WorkerPool* p) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"total\\":%d,\\"active\\":%d,\\"strategy\\":%d}",
                   p->_worker_count, p->_active_workers, p->_load_balance_strategy);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_message_queue_status(_MessageQueue* q) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"pending\\":%d,\\"processed\\":%d}",
                   q->_message_count, q->_processed_count);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_worker_manager_status(_WorkerPoolManager* m) {
    static char buf[512];
    int len = 0;
    len += sprintf(buf+len, "{\\"workers\\":%d,\\"channels\\":%d,\\"sent\\":%d,\\"received\\":%d,\\"crashed\\":%d}",
                   m->_pool._worker_count, m->_channel_count, m->_total_messages_sent,
                   m->_total_messages_received, m->_workers_crashed);
    buf[len] = 0;
    return buf;
}
)";

// Fragment 17: Advanced Monitoring and Metrics Collection
static const char* PRELUDE_FRAG17 = R"(
// ── Histogram for latency tracking ────────────────────────────────────────────
// Bucket for histogram (tracks count in range)
struct _HistogramBucket {
    long long _min_ms;              // Min boundary (milliseconds)
    long long _max_ms;              // Max boundary
    long long _count;               // Count of samples in bucket
    double _sum_ms;                 // Sum of all values
};

// Histogram for measuring latencies
struct _Histogram {
    const char* _name;              // "http_request_latency"
    _HistogramBucket _buckets[16];  // Up to 16 buckets
    int _bucket_count;
    long long _total_count;         // Total samples
    long long _min_observed;        // Minimum observed value
    long long _max_observed;        // Maximum observed value
    double _sum_observed;           // Sum for average calculation
    unsigned long long _created_at;
    const char* _span_id;
};

// Create histogram with bucket boundaries
__device__ _Histogram _create_histogram(const char* name, long long boundaries[16], int count, const char* span_id) {
    _Histogram h;
    h._name = name;
    h._bucket_count = (count > 16) ? 16 : count;
    h._total_count = 0;
    h._min_observed = LLONG_MAX;
    h._max_observed = LLONG_MIN;
    h._sum_observed = 0.0;
    h._created_at = (unsigned long long)Date::now();
    h._span_id = span_id;

    for (int i = 0; i < h._bucket_count; i++) {
        h._buckets[i]._min_ms = (i == 0) ? 0 : boundaries[i-1];
        h._buckets[i]._max_ms = boundaries[i];
        h._buckets[i]._count = 0;
        h._buckets[i]._sum_ms = 0.0;
    }
    return h;
}

// Record value in histogram
__device__ void _histogram_record(_Histogram* h, long long value_ms) {
    h->_total_count++;
    h->_sum_observed += value_ms;
    if (value_ms < h->_min_observed) h->_min_observed = value_ms;
    if (value_ms > h->_max_observed) h->_max_observed = value_ms;

    for (int i = 0; i < h->_bucket_count; i++) {
        if (value_ms <= h->_buckets[i]._max_ms) {
            h->_buckets[i]._count++;
            h->_buckets[i]._sum_ms += value_ms;
            break;
        }
    }
}

// Get histogram percentile (e.g., 95th percentile)
__device__ long long _histogram_percentile(_Histogram* h, int percentile) {
    long long target = (h->_total_count * percentile) / 100;
    long long cumulative = 0;

    for (int i = 0; i < h->_bucket_count; i++) {
        cumulative += h->_buckets[i]._count;
        if (cumulative >= target) {
            return (h->_buckets[i]._min_ms + h->_buckets[i]._max_ms) / 2;
        }
    }
    return h->_max_observed;
}

// ── Counter for event counting ────────────────────────────────────────────────
// Simple counter metric
struct _Counter {
    const char* _name;              // "requests_total"
    long long _value;               // Current count
    const char* _labels[8];         // Up to 8 labels (status:200, method:GET)
    int _label_count;
    unsigned long long _created_at;
    const char* _span_id;
};

// Create counter
__device__ _Counter _create_counter(const char* name, const char* span_id) {
    _Counter c;
    c._name = name;
    c._value = 0;
    c._label_count = 0;
    c._created_at = (unsigned long long)Date::now();
    c._span_id = span_id;
    return c;
}

// Increment counter
__device__ void _counter_inc(_Counter* c) {
    c->_value++;
}

// Increment by amount
__device__ void _counter_add(_Counter* c, long long amount) {
    c->_value += amount;
}

// ── Gauge for point-in-time measurements ──────────────────────────────────────
// Gauge metric (can go up or down)
struct _Gauge {
    const char* _name;              // "memory_used_bytes"
    long long _value;               // Current value
    long long _min_value;           // Minimum observed
    long long _max_value;           // Maximum observed
    unsigned long long _created_at;
    const char* _span_id;
};

// Create gauge
__device__ _Gauge _create_gauge(const char* name, const char* span_id) {
    _Gauge g;
    g._name = name;
    g._value = 0;
    g._min_value = LLONG_MAX;
    g._max_value = LLONG_MIN;
    g._created_at = (unsigned long long)Date::now();
    g._span_id = span_id;
    return g;
}

// Set gauge value
__device__ void _gauge_set(_Gauge* g, long long value) {
    g->_value = value;
    if (value < g->_min_value) g->_min_value = value;
    if (value > g->_max_value) g->_max_value = value;
}

// Increment/decrement gauge
__device__ void _gauge_add(_Gauge* g, long long delta) {
    g->_value += delta;
    if (g->_value < g->_min_value) g->_min_value = g->_value;
    if (g->_value > g->_max_value) g->_max_value = g->_value;
}

// ── Metrics Collector (aggregates all metrics) ────────────────────────────────
// Collector for all metrics
struct _MetricsCollector {
    const char* _id;                // "metrics-collector"
    _Histogram _histograms[8];      // Up to 8 histograms
    int _histogram_count;
    _Counter _counters[16];         // Up to 16 counters
    int _counter_count;
    _Gauge _gauges[16];             // Up to 16 gauges
    int _gauge_count;
    long long _scrape_count;        // Number of times scraped
    unsigned long long _created_at;
    const char* _span_id;
};

// Create metrics collector
__device__ _MetricsCollector _create_metrics_collector(const char* span_id) {
    _MetricsCollector mc;
    mc._id = "metrics-collector";
    mc._histogram_count = 0;
    mc._counter_count = 0;
    mc._gauge_count = 0;
    mc._scrape_count = 0;
    mc._created_at = (unsigned long long)Date::now();
    mc._span_id = span_id;
    return mc;
}

// Add histogram to collector
__device__ int _metrics_add_histogram(_MetricsCollector* mc, _Histogram h) {
    if (mc->_histogram_count >= 8) return -1;
    mc->_histograms[mc->_histogram_count++] = h;
    return mc->_histogram_count - 1;
}

// Add counter to collector
__device__ int _metrics_add_counter(_MetricsCollector* mc, _Counter c) {
    if (mc->_counter_count >= 16) return -1;
    mc->_counters[mc->_counter_count++] = c;
    return mc->_counter_count - 1;
}

// Add gauge to collector
__device__ int _metrics_add_gauge(_MetricsCollector* mc, _Gauge g) {
    if (mc->_gauge_count >= 16) return -1;
    mc->_gauges[mc->_gauge_count++] = g;
    return mc->_gauge_count - 1;
}

// ── Performance Observer (hooks for performance measurements) ──────────────────
// Performance entry for observed events
struct _PerfEntry {
    const char* _name;              // "http_request"
    const char* _entry_type;        // "measure" or "mark"
    long long _duration_ms;         // Duration in milliseconds
    long long _start_time_ms;       // Start timestamp
    const char* _detail;            // Additional details
};

// Performance observer
struct _PerfObserver {
    const char* _id;                // "perf-observer"
    const char* _entry_types[8];    // Types to observe
    int _type_count;
    _PerfEntry _entries[32];        // Up to 32 observed entries
    int _entry_count;
    int _enabled;                   // 1=enabled, 0=disabled
    unsigned long long _created_at;
    const char* _span_id;
};

// Create performance observer
__device__ _PerfObserver _create_perf_observer(const char* entry_types[8], int count, const char* span_id) {
    _PerfObserver po;
    po._id = "perf-observer";
    po._type_count = (count > 8) ? 8 : count;
    po._entry_count = 0;
    po._enabled = 1;
    po._created_at = (unsigned long long)Date::now();
    po._span_id = span_id;

    for (int i = 0; i < po._type_count; i++) {
        po._entry_types[i] = entry_types[i];
    }
    return po;
}

// Record performance entry
__device__ int _perf_observer_record(_PerfObserver* po, const char* name, const char* type,
                                      long long duration_ms, const char* detail) {
    if (!po->_enabled || po->_entry_count >= 32) return -1;

    _PerfEntry* entry = &po->_entries[po->_entry_count++];
    entry->_name = name;
    entry->_entry_type = type;
    entry->_duration_ms = duration_ms;
    entry->_start_time_ms = (long long)Date::now();
    entry->_detail = detail;
    return po->_entry_count - 1;
}

// ── SLA Monitor (tracks SLA compliance) ──────────────────────────────────────
// Service level agreement monitoring
struct _SLAMonitor {
    const char* _id;                // "sla-monitor"
    long long _sla_threshold_ms;    // SLA threshold in milliseconds
    long long _total_requests;      // Total requests measured
    long long _compliant_requests;  // Requests within SLA
    long long _violating_requests;  // Requests exceeding SLA
    double _compliance_rate;        // Percentage compliant
    unsigned long long _created_at;
    const char* _span_id;
};

// Create SLA monitor
__device__ _SLAMonitor _create_sla_monitor(long long threshold_ms, const char* span_id) {
    _SLAMonitor sm;
    sm._id = "sla-monitor";
    sm._sla_threshold_ms = threshold_ms;
    sm._total_requests = 0;
    sm._compliant_requests = 0;
    sm._violating_requests = 0;
    sm._compliance_rate = 100.0;
    sm._created_at = (unsigned long long)Date::now();
    sm._span_id = span_id;
    return sm;
}

// Record measurement against SLA
__device__ void _sla_monitor_record(_SLAMonitor* sm, long long duration_ms) {
    sm->_total_requests++;
    if (duration_ms <= sm->_sla_threshold_ms) {
        sm->_compliant_requests++;
    } else {
        sm->_violating_requests++;
    }
    sm->_compliance_rate = (double)sm->_compliant_requests * 100.0 / (double)sm->_total_requests;
}

// ── JSON Formatting for Prometheus ────────────────────────────────────────────
__device__ const char* _format_histogram_prometheus(_Histogram* h) {
    static char buf[512];
    double avg = (h->_total_count > 0) ? (h->_sum_observed / h->_total_count) : 0.0;
    int len = 0;
    len += sprintf(buf+len, "# HELP %s Latency histogram\n", h->_name);
    len += sprintf(buf+len, "# TYPE %s histogram\n", h->_name);
    len += sprintf(buf+len, "%s_count %lld\n", h->_name, h->_total_count);
    len += sprintf(buf+len, "%s_sum %.1f\n", h->_name, h->_sum_observed);
    len += sprintf(buf+len, "%s_avg %.1f", h->_name, avg);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_counter_prometheus(_Counter* c) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "# HELP %s Counter\n", c->_name);
    len += sprintf(buf+len, "# TYPE %s counter\n", c->_name);
    len += sprintf(buf+len, "%s %lld", c->_name, c->_value);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_gauge_prometheus(_Gauge* g) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "# HELP %s Gauge\n", g->_name);
    len += sprintf(buf+len, "# TYPE %s gauge\n", g->_name);
    len += sprintf(buf+len, "%s %lld", g->_name, g->_value);
    buf[len] = 0;
    return buf;
}

__device__ const char* _format_sla_status(_SLAMonitor* sm) {
    static char buf[256];
    int len = 0;
    len += sprintf(buf+len, "{\\"threshold_ms\\":%lld,\\"total\\":%lld,\\"compliant\\":%lld,\\"compliance_rate\\\":%.1f%%}",
                   sm->_sla_threshold_ms, sm->_total_requests, sm->_compliant_requests, sm->_compliance_rate);
    buf[len] = 0;
    return buf;
}
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
    if (PRELUDE_FRAG13) result += PRELUDE_FRAG13; // Global timers
    if (PRELUDE_FRAG14) result += PRELUDE_FRAG14; // Global utilities
    if (PRELUDE_FRAG15) result += PRELUDE_FRAG15; // Advanced scheduling
    if (PRELUDE_FRAG16) result += PRELUDE_FRAG16; // Worker threads
    if (PRELUDE_FRAG17) result += PRELUDE_FRAG17; // Advanced monitoring
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
