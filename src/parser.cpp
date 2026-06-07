#include "cardcode/parser.hpp"

#include "cardcode/lexer.hpp"

namespace cardcode {
namespace {

struct ParserState {
    const std::vector<Token>& tokens;
    std::size_t pos;
    std::vector<Diagnostic>& diags;

    const Token& peek() const { return tokens[pos]; }
    const Token& next() {
        const Token& t = tokens[pos];
        if (tokens[pos].kind != TokenKind::End) ++pos;
        return t;
    }
};

SourceSpan join(const SourceSpan& a, const SourceSpan& b) {
    return SourceSpan{a.start_offset, b.end_offset,
                      a.start_line, a.start_column,
                      b.end_line, b.end_column};
}

bool parse_expr(ParserState& s, RawExpr& out);

bool parse_list(ParserState& s, const Token& lparen, RawExpr& out) {
    out.kind = RawKind::List;
    SourceSpan span = lparen.span;

    while (true) {
        const Token& t = s.peek();
        if (t.kind == TokenKind::RParen) {
            span = join(span, t.span);
            s.next();
            out.span = span;
            return true;
        }
        if (t.kind == TokenKind::End) {
            s.diags.push_back({DiagnosticSeverity::Error, lparen.span,
                "missing closing parenthesis"});
            out.span = join(span, t.span);
            return false;
        }

        RawExpr child;
        if (!parse_expr(s, child)) {
            out.span = span;
            return false;
        }
        span = join(span, child.span);
        out.list.push_back(std::move(child));
    }
}

bool parse_expr(ParserState& s, RawExpr& out) {
    const Token& t = s.next();
    switch (t.kind) {
        case TokenKind::LParen:
            return parse_list(s, t, out);
        case TokenKind::Symbol:
            out.kind = RawKind::Symbol;
            out.symbol = t.text;
            out.span = t.span;
            return true;
        case TokenKind::Integer:
            out.kind = RawKind::Integer;
            out.integer = t.integer;
            out.span = t.span;
            return true;
        case TokenKind::RParen:
            s.diags.push_back({DiagnosticSeverity::Error, t.span, "unexpected ')'"});
            return false;
        case TokenKind::End:
            s.diags.push_back({DiagnosticSeverity::Error, t.span, "unexpected end of input"});
            return false;
    }
    return false;
}

} // namespace

ParseResult parse(std::string_view source) {
    ParseResult result;

    LexResult lexed = lex(source);
    result.diagnostics = lexed.diagnostics;

    ParserState s{lexed.tokens, 0, result.diagnostics};
    while (s.peek().kind != TokenKind::End) {
        RawExpr e;
        if (parse_expr(s, e)) {
            result.exprs.push_back(std::move(e));
        }
        // parse_expr always consumes at least one token, so the loop makes
        // progress and terminates even on error.
    }
    return result;
}

} // namespace cardcode
