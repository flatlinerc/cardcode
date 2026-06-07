#include "doctest.h"

#include "cardcode/parser.hpp"

using namespace cardcode;

TEST_CASE("parses a single expression") {
    auto r = parse("(drive 40 1000)");
    REQUIRE(r.ok());
    REQUIRE(r.exprs.size() == 1);
    CHECK(r.exprs[0].kind == RawKind::List);
    REQUIRE(r.exprs[0].list.size() == 3);
    CHECK(r.exprs[0].list[0].symbol == "drive");
    CHECK(r.exprs[0].list[1].integer == 40);
}

TEST_CASE("parses nested expressions") {
    auto r = parse("(repeat 2 (drive 40 1000))");
    REQUIRE(r.ok());
    REQUIRE(r.exprs.size() == 1);
    const RawExpr& rep = r.exprs[0];
    REQUIRE(rep.list.size() == 3);
    CHECK(rep.list[2].kind == RawKind::List);
    CHECK(rep.list[2].list[0].symbol == "drive");
}

TEST_CASE("parses multiple top-level expressions") {
    auto r = parse("(stop)(stop)");
    REQUIRE(r.ok());
    CHECK(r.exprs.size() == 2);
}

TEST_CASE("reports a missing closing paren") {
    auto r = parse("(drive 40 1000");
    CHECK_FALSE(r.ok());
}

TEST_CASE("reports an extra closing paren") {
    auto r = parse("(stop))");
    CHECK_FALSE(r.ok());
}

TEST_CASE("parses an empty list (rejected later by the compiler)") {
    auto r = parse("()");
    REQUIRE(r.ok());
    REQUIRE(r.exprs.size() == 1);
    CHECK(r.exprs[0].kind == RawKind::List);
    CHECK(r.exprs[0].list.empty());
}
