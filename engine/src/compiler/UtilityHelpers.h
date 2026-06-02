#pragma once
#include <string>
#include <vector>
#include <memory>

namespace symm {

// Utility helper functions for common operations
// These are safe, zero-overhead additions to the compiler

class UtilityHelpers {
public:
    // String manipulation helpers
    static std::string escapeString(const std::string& input);
    static std::string unescapeString(const std::string& input);
    static std::string formatBytes(size_t bytes);

    // Type checking utilities
    static bool isValidIdentifier(const std::string& name);
    static bool isValidTypeName(const std::string& name);
    static bool isReservedKeyword(const std::string& word);

    // Code generation helpers
    static std::string sanitizeSymbolName(const std::string& name);
    static std::string makeIndent(int level, int spacesPerLevel = 4);
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string join(const std::vector<std::string>& parts, const std::string& delimiter);
    static std::string trim(const std::string& str);

    // Performance metrics helpers
    static double microsToMillis(double micros);
    static std::string formatDuration(double micros);

private:
    static const std::vector<std::string> RESERVED_KEYWORDS;
};

}  // namespace symm
