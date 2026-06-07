#include "cardcode/lexer.hpp"

#include <stdexcept>
#include <string>

namespace cardcode {
namespace {

bool is_symbol_char(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
        case '_': case '-': case '?': case '!': case '<': case '>':
        case '=': case '+': case '*': case '/': case '.':
            return true;
        default:
            return false;
    }
}

// Matches an optional leading '-' followed by one or more digits.
bool is_integer_word(const std::string& w) {
    std::size_t i = 0;
    if (i < w.size() && w[i] == '-') ++i;
    if (i >= w.size()) return false; // lone "-"
    for (; i < w.size(); ++i) {
        if (w[i] < '0' || w[i] > '9') return false;
    }
    return true;
}

} // namespace

LexResult lex(std::string_view src) {
    LexResult result;

    std::uint32_t offset = 0;
    std::uint32_t line = 1;
    std::uint32_t col = 1;

    auto advance = [&]() {
        if (src[offset] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        ++offset;
    };

    while (offset < src.size()) {
        char c = src[offset];

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        if (c == ';') {
            while (offset < src.size() && src[offset] != '\n') advance();
            continue;
        }

        std::uint32_t so = offset, sl = line, sc = col;

        if (c == '(' || c == ')') {
            advance();
            result.tokens.push_back({c == '(' ? TokenKind::LParen : TokenKind::RParen,
                                     SourceSpan{so, offset, sl, sc, line, col},
                                     std::string(1, c), 0});
            continue;
        }

        if (is_symbol_char(c)) {
            std::string word;
            while (offset < src.size() && is_symbol_char(src[offset])) {
                word.push_back(src[offset]);
                advance();
            }
            SourceSpan span{so, offset, sl, sc, line, col};

            if (is_integer_word(word)) {
                Token t;
                t.kind = TokenKind::Integer;
                t.span = span;
                t.text = word;
                try {
                    t.integer = std::stoll(word);
                } catch (const std::out_of_range&) {
                    result.diagnostics.push_back({DiagnosticSeverity::Error, span,
                        "integer literal out of range: " + word});
                    t.integer = 0;
                }
                result.tokens.push_back(std::move(t));
            } else if (word[0] >= '0' && word[0] <= '9') {
                // begins with a digit but is not a valid integer (e.g. "12a")
                result.diagnostics.push_back({DiagnosticSeverity::Error, span,
                    "invalid number or symbol '" + word + "'"});
                result.tokens.push_back({TokenKind::Symbol, span, word, 0});
            } else {
                result.tokens.push_back({TokenKind::Symbol, span, word, 0});
            }
            continue;
        }

        // Unrecognized character.
        advance();
        result.diagnostics.push_back({DiagnosticSeverity::Error,
            SourceSpan{so, offset, sl, sc, line, col},
            std::string("unexpected character '") + c + "'"});
    }

    result.tokens.push_back({TokenKind::End, SourceSpan{offset, offset, line, col, line, col}, "", 0});
    return result;
}

} // namespace cardcode
