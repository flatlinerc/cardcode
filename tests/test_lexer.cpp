#include "doctest.h"

#include "cardcode/lexer.hpp"

using namespace cardcode;

TEST_CASE("lexes parens, symbols, and integers") {
    auto r = lex("(drive 40 1000)");
    REQUIRE(r.ok());
    REQUIRE(r.tokens.size() == 6); // ( drive 40 1000 ) End
    CHECK(r.tokens[0].kind == TokenKind::LParen);
    CHECK(r.tokens[1].kind == TokenKind::Symbol);
    CHECK(r.tokens[1].text == "drive");
    CHECK(r.tokens[2].kind == TokenKind::Integer);
    CHECK(r.tokens[2].integer == 40);
    CHECK(r.tokens[3].kind == TokenKind::Integer);
    CHECK(r.tokens[3].integer == 1000);
    CHECK(r.tokens[4].kind == TokenKind::RParen);
    CHECK(r.tokens[5].kind == TokenKind::End);
}

TEST_CASE("distinguishes negative integers from dashed symbols") {
    auto r = lex("turn-right -10");
    REQUIRE(r.ok());
    CHECK(r.tokens[0].kind == TokenKind::Symbol);
    CHECK(r.tokens[0].text == "turn-right");
    CHECK(r.tokens[1].kind == TokenKind::Integer);
    CHECK(r.tokens[1].integer == -10);
}

TEST_CASE("skips comments to end of line") {
    auto r = lex("; a comment\n(stop) ; trailing comment\n");
    REQUIRE(r.ok());
    CHECK(r.tokens[0].kind == TokenKind::LParen);
    CHECK(r.tokens[1].text == "stop");
    CHECK(r.tokens[2].kind == TokenKind::RParen);
    CHECK(r.tokens[3].kind == TokenKind::End);
}

TEST_CASE("records source spans with line and column") {
    auto r = lex("(stop)");
    const Token& sym = r.tokens[1];
    CHECK(sym.text == "stop");
    CHECK(sym.span.start_offset == 1);
    CHECK(sym.span.end_offset == 5);
    CHECK(sym.span.start_line == 1);
    CHECK(sym.span.start_column == 2);
}

TEST_CASE("tracks line numbers across newlines") {
    auto r = lex("(stop)\n(stop)");
    // second '(' begins on line 2, column 1
    CHECK(r.tokens[3].kind == TokenKind::LParen);
    CHECK(r.tokens[3].span.start_line == 2);
    CHECK(r.tokens[3].span.start_column == 1);
}

TEST_CASE("reports invalid characters") {
    auto r = lex("(drive % 1)");
    CHECK_FALSE(r.ok());
    CHECK(r.diagnostics.size() == 1);
}
