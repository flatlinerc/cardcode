#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cardcode/diagnostic.hpp"
#include "cardcode/source_span.hpp"

namespace cardcode {

enum class RawKind {
    List,
    Symbol,
    Integer
};

// Untyped S-expression tree. The compiler turns this into the typed AST so that
// lexing/parsing stay free of language semantics.
struct RawExpr {
    RawKind kind{};
    SourceSpan span{};
    std::string symbol;
    std::int64_t integer{};
    std::vector<RawExpr> list;
};

struct ParseResult {
    std::vector<RawExpr> exprs; // top-level expressions, in source order
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return !has_errors(diagnostics); }
};

ParseResult parse(std::string_view source);

} // namespace cardcode
