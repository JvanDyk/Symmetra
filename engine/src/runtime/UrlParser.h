#pragma once
#include <string>
#include <vector>
#include <utility>

namespace symm {

// ParsedUrl — result of URL parsing, mirrors the WHATWG URL standard.
struct ParsedUrl {
    std::string href;       // full serialised URL
    std::string protocol;   // e.g. "https:"
    std::string username;
    std::string password;
    std::string hostname;   // e.g. "example.com"
    std::string port;       // e.g. "8080" (empty if default)
    std::string host;       // hostname + port if non-default
    std::string origin;     // protocol + "//" + host
    std::string pathname;   // e.g. "/path/to/page"
    std::string search;     // e.g. "?q=1" (includes leading ?)
    std::string hash;       // e.g. "#section" (includes leading #)

    bool valid = false;
};

// UrlParser — CPU-side WHATWG URL parser.
//
// Parsing happens on CPU (URL construction is not a hot-path operation).
// The parsed fields are small strings that the compiler copies into the
// handler's VRAM context buffer via the standard SymmCtx upload path.
//
class UrlParser {
public:
    // Parse an absolute or relative URL. base is used when url is relative.
    static ParsedUrl parse(const std::string& url,
                           const std::string& base = "");

private:
    static std::string normaliseScheme(const std::string& s);
    static std::string percentDecode(const std::string& s);
};

// UrlSearchParams — key-value store over a URL query string.
// Mirrors the WHATWG URLSearchParams API exactly.
//
class UrlSearchParams {
public:
    UrlSearchParams() = default;
    explicit UrlSearchParams(const std::string& queryString);

    // Returns first value for name, or "" if absent.
    std::string get(const std::string& name) const;

    // Returns all values for name.
    std::vector<std::string> getAll(const std::string& name) const;

    bool has(const std::string& name) const;

    void set(const std::string& name, const std::string& value);
    void append(const std::string& name, const std::string& value);
    void remove(const std::string& name);   // "delete" is a C++ keyword

    // Serialise to "k1=v1&k2=v2" (percent-encoded).
    std::string toString() const;

    // Iterate all pairs (name, value)
    const std::vector<std::pair<std::string,std::string>>& entries() const {
        return params_;
    }

    int size() const { return static_cast<int>(params_.size()); }

    static std::string encode(const std::string& s);
    static std::string decode(const std::string& s);

private:
    std::vector<std::pair<std::string,std::string>> params_;
    static void parseInto(const std::string& qs,
                          std::vector<std::pair<std::string,std::string>>& out);
};

} // namespace symm
