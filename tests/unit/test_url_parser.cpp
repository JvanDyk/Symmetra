// Unit tests for UrlParser and UrlSearchParams — no GPU required.
// Build: g++ -std=c++17 -I../../engine/src test_url_parser.cpp
//        ../../engine/src/runtime/UrlParser.cpp -o test_url_parser && ./test_url_parser

#include "runtime/UrlParser.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace symm;

static int passed = 0, failed = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define CHECK_EQ(a, b) do { \
    if ((a) == (b)) { ++passed; } \
    else { ++failed; std::fprintf(stderr, "FAIL [%s:%d] '%s' != '%s'\n", \
           __FILE__, __LINE__, std::string(a).c_str(), std::string(b).c_str()); } \
} while(0)

// ── UrlParser ─────────────────────────────────────────────────────────────────

void test_basic_https() {
    auto r = UrlParser::parse("https://example.com/path?q=1#sec");
    CHECK(r.valid,                "valid");
    CHECK_EQ(r.protocol, "https:");
    CHECK_EQ(r.hostname, "example.com");
    CHECK_EQ(r.port,     "");
    CHECK_EQ(r.host,     "example.com");
    CHECK_EQ(r.pathname, "/path");
    CHECK_EQ(r.search,   "?q=1");
    CHECK_EQ(r.hash,     "#sec");
    CHECK_EQ(r.origin,   "https://example.com");
}

void test_with_port() {
    auto r = UrlParser::parse("http://localhost:8080/api");
    CHECK(r.valid,                   "valid");
    CHECK_EQ(r.hostname, "localhost");
    CHECK_EQ(r.port,     "8080");
    CHECK_EQ(r.host,     "localhost:8080");
    CHECK_EQ(r.pathname, "/api");
}

void test_with_userinfo() {
    auto r = UrlParser::parse("ftp://user:pass@host.com/dir");
    CHECK(r.valid,          "valid");
    CHECK_EQ(r.username, "user");
    CHECK_EQ(r.password, "pass");
    CHECK_EQ(r.hostname, "host.com");
    CHECK_EQ(r.pathname, "/dir");
}

void test_no_path() {
    auto r = UrlParser::parse("https://example.com");
    CHECK(r.valid,                "valid");
    CHECK_EQ(r.hostname, "example.com");
}

void test_invalid_no_scheme() {
    auto r = UrlParser::parse("not-a-url");
    CHECK(!r.valid, "should be invalid");
}

void test_scheme_case_insensitive() {
    auto r = UrlParser::parse("HTTPS://Example.COM/");
    CHECK(r.valid, "valid");
    CHECK_EQ(r.protocol, "https:");  // scheme normalised to lowercase
}

void test_empty_path() {
    auto r = UrlParser::parse("https://example.com");
    CHECK(r.valid, "valid");
    CHECK(r.pathname == "" || r.pathname == "/", "pathname empty or /");
}

void test_query_only() {
    auto r = UrlParser::parse("https://x.com/p?a=1&b=2");
    CHECK(r.valid, "valid");
    CHECK_EQ(r.search, "?a=1&b=2");
    CHECK_EQ(r.hash,   "");
}

void test_hash_only() {
    auto r = UrlParser::parse("https://x.com/p#frag");
    CHECK(r.valid, "valid");
    CHECK_EQ(r.search, "");
    CHECK_EQ(r.hash,   "#frag");
}

// ── UrlSearchParams ───────────────────────────────────────────────────────────

void test_params_parse() {
    UrlSearchParams p("a=1&b=hello+world&c=");
    CHECK_EQ(p.get("a"), "1");
    CHECK_EQ(p.get("b"), "hello world");   // '+' → space
    CHECK_EQ(p.get("c"), "");
    CHECK_EQ(p.get("z"), "");              // missing → ""
}

void test_params_percent_decode() {
    UrlSearchParams p("name=hello%20world&emoji=%F0%9F%A4%96");
    CHECK_EQ(p.get("name"),  "hello world");
    // emoji: U+1F916 = 4-byte UTF-8 F0 9F A4 96
    std::string em = p.get("emoji");
    CHECK(em.size() == 4, "emoji is 4 bytes");
}

void test_params_set_get() {
    UrlSearchParams p;
    p.set("key", "value1");
    CHECK_EQ(p.get("key"), "value1");
    p.set("key", "value2");         // overwrite
    CHECK_EQ(p.get("key"), "value2");
    CHECK(p.size() == 1, "one entry after overwrite");
}

void test_params_append() {
    UrlSearchParams p;
    p.append("tag", "gpu");
    p.append("tag", "fast");
    auto all = p.getAll("tag");
    CHECK(all.size() == 2, "two tags");
    CHECK_EQ(all[0], "gpu");
    CHECK_EQ(all[1], "fast");
}

void test_params_remove() {
    UrlSearchParams p("a=1&b=2&a=3");
    p.remove("a");
    CHECK(!p.has("a"), "a removed");
    CHECK(p.has("b"),  "b present");
    CHECK(p.size() == 1, "one entry remains");
}

void test_params_to_string() {
    UrlSearchParams p;
    p.set("hello world", "foo bar");
    std::string s = p.toString();
    // '+' encoding for spaces in form encoding
    CHECK(s.find("hello") != std::string::npos, "key encoded");
}

void test_params_encode_roundtrip() {
    UrlSearchParams p;
    p.set("a", "hello world");
    p.set("b", "value/with/slashes");
    UrlSearchParams p2(p.toString());
    CHECK_EQ(p2.get("a"), "hello world");
    CHECK_EQ(p2.get("b"), "value/with/slashes");
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    // UrlParser
    test_basic_https();
    test_with_port();
    test_with_userinfo();
    test_no_path();
    test_invalid_no_scheme();
    test_scheme_case_insensitive();
    test_empty_path();
    test_query_only();
    test_hash_only();

    // UrlSearchParams
    test_params_parse();
    test_params_percent_decode();
    test_params_set_get();
    test_params_append();
    test_params_remove();
    test_params_to_string();
    test_params_encode_roundtrip();

    std::printf("\nURL Parser tests: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
