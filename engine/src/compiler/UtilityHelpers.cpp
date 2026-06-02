#include "UtilityHelpers.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace symm {

const std::vector<std::string> UtilityHelpers::RESERVED_KEYWORDS = {
    "abstract", "arguments", "await", "boolean", "break", "byte", "case", "catch", "char",
    "class", "const", "continue", "debugger", "default", "delete", "do", "double", "else",
    "enum", "eval", "export", "extends", "false", "final", "finally", "float", "for",
    "function", "goto", "if", "implements", "import", "in", "instanceof", "int", "interface",
    "let", "long", "native", "new", "null", "package", "private", "protected", "public",
    "return", "short", "static", "super", "switch", "synchronized", "this", "throw", "throws",
    "transient", "true", "try", "typeof", "var", "void", "volatile", "while", "with", "yield"
};

std::string UtilityHelpers::escapeString(const std::string& input) {
    std::string result;
    for (char c : input) {
        switch (c) {
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\'': result += "\\'"; break;
            default: result += c;
        }
    }
    return result;
}

std::string UtilityHelpers::unescapeString(const std::string& input) {
    std::string result;
    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] == '\\' && i + 1 < input.length()) {
            char next = input[i + 1];
            switch (next) {
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                case '\\': result += '\\'; i++; break;
                case '"': result += '"'; i++; break;
                case '\'': result += '\''; i++; break;
                default: result += input[i];
            }
        } else {
            result += input[i];
        }
    }
    return result;
}

std::string UtilityHelpers::formatBytes(size_t bytes) {
    const char* units[] = { "B", "KB", "MB", "GB" };
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

bool UtilityHelpers::isValidIdentifier(const std::string& name) {
    if (name.empty()) return false;
    if (!std::isalpha(name[0]) && name[0] != '_' && name[0] != '$') return false;
    for (size_t i = 1; i < name.length(); i++) {
        char c = name[i];
        if (!std::isalnum(c) && c != '_' && c != '$') return false;
    }
    return true;
}

bool UtilityHelpers::isValidTypeName(const std::string& name) {
    if (!isValidIdentifier(name)) return false;
    // Type names typically start with uppercase, but not required
    return !name.empty();
}

bool UtilityHelpers::isReservedKeyword(const std::string& word) {
    return std::find(RESERVED_KEYWORDS.begin(), RESERVED_KEYWORDS.end(), word)
        != RESERVED_KEYWORDS.end();
}

std::string UtilityHelpers::sanitizeSymbolName(const std::string& name) {
    std::string result = name;
    // Replace invalid characters with underscores
    for (char& c : result) {
        if (!std::isalnum(c) && c != '_') {
            c = '_';
        }
    }
    // Ensure it doesn't start with a digit
    if (!result.empty() && std::isdigit(result[0])) {
        result = "_" + result;
    }
    return result;
}

std::string UtilityHelpers::makeIndent(int level, int spacesPerLevel) {
    return std::string(level * spacesPerLevel, ' ');
}

std::vector<std::string> UtilityHelpers::split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        result.push_back(item);
    }
    return result;
}

std::string UtilityHelpers::join(const std::vector<std::string>& parts, const std::string& delimiter) {
    if (parts.empty()) return "";
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
        result += delimiter + parts[i];
    }
    return result;
}

std::string UtilityHelpers::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    return str.substr(start, end - start + 1);
}

double UtilityHelpers::microsToMillis(double micros) {
    return micros / 1000.0;
}

std::string UtilityHelpers::formatDuration(double micros) {
    if (micros < 1000.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << micros << " µs";
        return oss.str();
    } else if (micros < 1000000.0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (micros / 1000.0) << " ms";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (micros / 1000000.0) << " s";
        return oss.str();
    }
}

}  // namespace symm
