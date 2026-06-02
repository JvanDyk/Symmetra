#include "ErrorReporting.h"
#include <sstream>

namespace symm {

std::string Diagnostic::format() const {
    std::ostringstream oss;

    // Format header: "file:line:col: error: message"
    oss << location.toString() << ": ";

    switch (severity) {
        case ErrorSeverity::Error:   oss << "error: "; break;
        case ErrorSeverity::Warning: oss << "warning: "; break;
        case ErrorSeverity::Info:    oss << "info: "; break;
        case ErrorSeverity::Note:    oss << "note: "; break;
    }

    oss << message << "\n";

    // Format context if available
    if (!context.empty()) {
        oss << "  > " << context << "\n";
    }

    // Format suggestions
    for (const auto& suggestion : suggestions) {
        oss << "  hint: " << suggestion << "\n";
    }

    return oss.str();
}

void ErrorReporter::addError(const Diagnostic& diag) {
    errors.push_back(diag);
}

void ErrorReporter::addError(const std::string& message, int line, const std::string& context) {
    Diagnostic d;
    d.severity = ErrorSeverity::Error;
    d.location = SourceLocation(line);
    d.message = message;
    d.context = context;
    errors.push_back(d);
}

void ErrorReporter::addWarning(const std::string& message, int line, const std::string& context) {
    Diagnostic d;
    d.severity = ErrorSeverity::Warning;
    d.location = SourceLocation(line);
    d.message = message;
    d.context = context;
    warnings.push_back(d);
}

std::string ErrorReporter::formatAll() const {
    std::ostringstream oss;

    for (const auto& warning : warnings) {
        oss << warning.format();
    }

    for (const auto& error : errors) {
        oss << error.format();
    }

    if (!errors.empty()) {
        oss << "\n" << errors.size() << " error(s)";
        if (!warnings.empty()) {
            oss << ", " << warnings.size() << " warning(s)";
        }
        oss << " generated.\n";
    } else if (!warnings.empty()) {
        oss << "\n" << warnings.size() << " warning(s) generated.\n";
    }

    return oss.str();
}

void ErrorReporter::clear() {
    errors.clear();
    warnings.clear();
}

}  // namespace symm
