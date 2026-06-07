#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cardcode/diagnostic.hpp"
#include "cardcode/source_span.hpp"

namespace cardcode {

enum class TokenKind {
    LParen,
    RParen,
    Symbol,
    Integer,
    End
};

struct Token {
    TokenKind kind{};
    SourceSpan span{};
    std::string text;          // raw text; symbol name for Symbol tokens
    std::int64_t integer{};    // value when kind == Integer
};

struct LexResult {
    std::vector<Token> tokens; // always terminated by an End token
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return !has_errors(diagnostics); }
};

LexResult lex(std::string_view source);

} // namespace cardcode
