#pragma once
#include <string>
#include <vector>

namespace symm {

// Enhanced error reporting with line numbers and source context
struct SourceLocation {
    int line;
    int column;
    std::string filename;

    SourceLocation() : line(0), column(0), filename("") {}
    SourceLocation(int l, int c = 0, const std::string& f = "")
        : line(l), column(c), filename(f) {}

    std::string toString() const {
        std::string result;
        if (!filename.empty()) {
            result += filename + ":";
        }
        result += std::to_string(line);
        if (column > 0) {
            result += ":" + std::to_string(column);
        }
        return result;
    }
};

// Error severity levels
enum class ErrorSeverity {
    Error,
    Warning,
    Info,
    Note
};

// Compiler diagnostic message
struct Diagnostic {
    ErrorSeverity severity;
    SourceLocation location;
    std::string message;
    std::string context;  // Source code snippet for context
    std::vector<std::string> suggestions;

    std::string format() const;
};

// Error collector for batch reporting
class ErrorReporter {
public:
    void addError(const Diagnostic& diag);
    void addError(const std::string& message, int line = 0, const std::string& context = "");
    void addWarning(const std::string& message, int line = 0, const std::string& context = "");

    bool hasErrors() const { return !errors.empty(); }
    int errorCount() const { return (int)errors.size(); }
    int warningCount() const { return (int)warnings.size(); }

    std::string formatAll() const;
    void clear();

private:
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
};

}  // namespace symm
