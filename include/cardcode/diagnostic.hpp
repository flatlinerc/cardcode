#pragma once

#include <string>
#include <vector>

#include "cardcode/source_span.hpp"

namespace cardcode {

enum class DiagnosticSeverity {
    Error,
    Warning
};

// A structured user-facing message. The engine returns these for ordinary
// syntax/semantic problems instead of throwing.
struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    SourceSpan span{};
    std::string message;
};

inline bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        if (d.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

// "Error at line 1, column 2: unknown operation 'fly'"
inline std::string format_diagnostic(const Diagnostic& d) {
    std::string out = d.severity == DiagnosticSeverity::Error ? "Error" : "Warning";
    out += " at line ";
    out += std::to_string(d.span.start_line);
    out += ", column ";
    out += std::to_string(d.span.start_column);
    out += ": ";
    out += d.message;
    return out;
}

} // namespace cardcode
