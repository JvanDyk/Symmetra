#include "UrlParser.h"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace symm {

// ── Percent encoding ──────────────────────────────────────────────────────────

static char hexChar(int n) {
    return (n < 10) ? ('0' + n) : ('A' + n - 10);
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string UrlSearchParams::encode(const std::string& s) {
    // application/x-www-form-urlencoded: unreserved chars pass through, rest encoded
    static const char* safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::strchr(safe, c)) {
            out += c;
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hexChar(c >> 4);
            out += hexChar(c & 0xF);
        }
    }
    return out;
}

std::string UrlSearchParams::decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hexVal(s[i+1]);
            int lo = hexVal(s[i+2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// ── UrlSearchParams ───────────────────────────────────────────────────────────

void UrlSearchParams::parseInto(const std::string& qs,
                                 std::vector<std::pair<std::string,std::string>>& out) {
    std::string q = qs;
    if (!q.empty() && q[0] == '?') q = q.substr(1);

    size_t pos = 0;
    while (pos < q.size()) {
        size_t amp = q.find('&', pos);
        if (amp == std::string::npos) amp = q.size();
        std::string pair = q.substr(pos, amp - pos);
        pos = amp + 1;

        if (pair.empty()) continue;
        size_t eq = pair.find('=');
        if (eq == std::string::npos) {
            out.push_back({ decode(pair), "" });
        } else {
            out.push_back({ decode(pair.substr(0, eq)),
                            decode(pair.substr(eq + 1)) });
        }
    }
}

UrlSearchParams::UrlSearchParams(const std::string& queryString) {
    parseInto(queryString, params_);
}

std::string UrlSearchParams::get(const std::string& name) const {
    for (auto& p : params_)
        if (p.first == name) return p.second;
    return "";
}

std::vector<std::string> UrlSearchParams::getAll(const std::string& name) const {
    std::vector<std::string> out;
    for (auto& p : params_)
        if (p.first == name) out.push_back(p.second);
    return out;
}

bool UrlSearchParams::has(const std::string& name) const {
    for (auto& p : params_)
        if (p.first == name) return true;
    return false;
}

void UrlSearchParams::set(const std::string& name, const std::string& value) {
    for (auto& p : params_) {
        if (p.first == name) { p.second = value; return; }
    }
    params_.push_back({ name, value });
}

void UrlSearchParams::append(const std::string& name, const std::string& value) {
    params_.push_back({ name, value });
}

void UrlSearchParams::remove(const std::string& name) {
    params_.erase(std::remove_if(params_.begin(), params_.end(),
        [&name](const auto& p) { return p.first == name; }), params_.end());
}

std::string UrlSearchParams::toString() const {
    std::string out;
    for (size_t i = 0; i < params_.size(); ++i) {
        if (i > 0) out += '&';
        out += encode(params_[i].first) + '=' + encode(params_[i].second);
    }
    return out;
}

// ── UrlParser ─────────────────────────────────────────────────────────────────

std::string UrlParser::normaliseScheme(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string UrlParser::percentDecode(const std::string& s) {
    return UrlSearchParams::decode(s);  // same algorithm
}

ParsedUrl UrlParser::parse(const std::string& urlStr,
                            const std::string& /*base*/) {
    ParsedUrl r;
    std::string s = urlStr;

    // ── Scheme ────────────────────────────────────────────────────────────────
    size_t schemeEnd = s.find("://");
    if (schemeEnd == std::string::npos) {
        r.valid = false;
        return r;
    }
    r.protocol = normaliseScheme(s.substr(0, schemeEnd)) + ':';
    s = s.substr(schemeEnd + 3);  // skip "://"

    // ── Authority (user:pass@host:port) ───────────────────────────────────────
    size_t pathStart = s.find('/');
    std::string authority = (pathStart != std::string::npos)
                          ? s.substr(0, pathStart)
                          : s;
    s = (pathStart != std::string::npos) ? s.substr(pathStart) : "/";

    // userinfo
    size_t atPos = authority.rfind('@');
    if (atPos != std::string::npos) {
        std::string userinfo = authority.substr(0, atPos);
        authority = authority.substr(atPos + 1);
        size_t colon = userinfo.find(':');
        if (colon != std::string::npos) {
            r.username = percentDecode(userinfo.substr(0, colon));
            r.password = percentDecode(userinfo.substr(colon + 1));
        } else {
            r.username = percentDecode(userinfo);
        }
    }

    // host + port
    size_t bracketClose = authority.rfind(']');   // IPv6
    size_t colonPos     = authority.rfind(':');
    if (colonPos != std::string::npos &&
        (bracketClose == std::string::npos || colonPos > bracketClose)) {
        r.hostname = authority.substr(0, colonPos);
        r.port     = authority.substr(colonPos + 1);
    } else {
        r.hostname = authority;
    }
    r.host = r.port.empty() ? r.hostname : r.hostname + ':' + r.port;

    // ── Path ──────────────────────────────────────────────────────────────────
    size_t qPos = s.find('?');
    size_t hPos = s.find('#');
    r.pathname  = s.substr(0, std::min(qPos, hPos));

    if (qPos != std::string::npos) {
        size_t qEnd = (hPos != std::string::npos && hPos > qPos) ? hPos : s.size();
        r.search = s.substr(qPos, qEnd - qPos);
    }
    if (hPos != std::string::npos) {
        r.hash = s.substr(hPos);
    }

    r.origin = r.protocol + "//" + r.host;
    r.href   = r.origin + r.pathname + r.search + r.hash;
    if (!r.username.empty()) {
        r.href = r.protocol + "//" + r.username +
                 (r.password.empty() ? "" : ":" + r.password) +
                 "@" + r.host + r.pathname + r.search + r.hash;
    }
    r.valid = true;
    return r;
}

} // namespace symm
