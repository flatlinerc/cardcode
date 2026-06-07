#include "doctest.h"

#include <string>

#include "cardcode/compiler.hpp"

using namespace cardcode;

TEST_CASE("compiles a known command with expression arguments") {
    auto r = compile("(drive 40 1000)");
    REQUIRE(r.ok());
    REQUIRE(r.root);
    CHECK(r.root->kind == NodeKind::Drive);
    REQUIRE(r.root->args.size() == 2);
    CHECK(r.root->args[0]->kind == NodeKind::IntegerLiteral);
    CHECK(r.root->args[0]->nums[0] == 40);
    CHECK(r.root->args[1]->nums[0] == 1000);
}

TEST_CASE("reports an unknown command") {
    auto r = compile("(fly 10)");
    CHECK_FALSE(r.ok());
    REQUIRE_FALSE(r.diagnostics.empty());
    CHECK(r.diagnostics[0].message.find("unknown operation") != std::string::npos);
}

TEST_CASE("reports a wrong argument count") {
    auto r = compile("(drive 40)");
    CHECK_FALSE(r.ok());
}

TEST_CASE("a single top-level expression is the root (no implicit do)") {
    auto r = compile("(repeat 2 (stop))");
    REQUIRE(r.ok());
    CHECK(r.root->kind == NodeKind::Repeat);
}

TEST_CASE("multiple top-level expressions normalize to an implicit do") {
    auto r = compile("(stop)(stop)");
    REQUIRE(r.ok());
    CHECK(r.root->kind == NodeKind::Do);
    CHECK(r.root->children.size() == 2);
}

TEST_CASE("an explicit do is the root") {
    auto r = compile("(do (stop) (stop))");
    REQUIRE(r.ok());
    CHECK(r.root->kind == NodeKind::Do);
    CHECK(r.root->children.size() == 2);
}

TEST_CASE("an empty list is rejected") {
    auto r = compile("()");
    CHECK_FALSE(r.ok());
}
