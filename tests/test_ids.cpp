#include "doctest.h"

#include "cardcode/compiler.hpp"

using namespace cardcode;

TEST_CASE("assigns deterministic pre-order IDs") {
    auto r = compile("(repeat 2 (drive 40 1000) (turn-right 90))");
    REQUIRE(r.ok());
    CHECK(r.root->id == 0); // repeat = n0
    REQUIRE(r.root->children.size() == 2);
    CHECK(r.root->children[0]->id == 1); // drive = n1
    CHECK(r.root->children[1]->id == 2); // turn-right = n2
}

TEST_CASE("IDs and spans are stable across recompilation") {
    const char* src = "(repeat 2 (drive 40 1000) (turn-right 90))";
    auto a = compile(src);
    auto b = compile(src);
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    CHECK(a.root->id == b.root->id);
    CHECK(a.root->children[0]->id == b.root->children[0]->id);
    CHECK(a.root->children[1]->id == b.root->children[1]->id);
    CHECK(a.root->children[0]->span.start_offset == b.root->children[0]->span.start_offset);
    CHECK(a.root->children[0]->span.end_offset == b.root->children[0]->span.end_offset);
}

TEST_CASE("formats node IDs as nN") {
    CHECK(node_id_to_string(0) == "n0");
    CHECK(node_id_to_string(42) == "n42");
}
