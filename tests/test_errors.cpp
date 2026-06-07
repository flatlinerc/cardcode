#include "doctest.h"

#include <string>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct NullSink : ExecutionEventSink {
    void on_event(const ExecutionEvent&) override {}
};

bool has_message(const std::vector<Diagnostic>& diags, const char* needle) {
    for (const auto& d : diags) {
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}
} // namespace

TEST_CASE("rejects an out-of-range speed at compile time") {
    auto r = compile("(drive 140 1000)");
    CHECK_FALSE(r.ok());
    CHECK(has_message(r.diagnostics, "speed must be between"));
}

TEST_CASE("rejects an excessive duration") {
    auto r = compile("(drive 40 999999)");
    CHECK_FALSE(r.ok());
    CHECK(has_message(r.diagnostics, "duration_ms must be between"));
}

TEST_CASE("rejects an excessive repeat count") {
    auto r = compile("(repeat 1000000 (stop))");
    CHECK_FALSE(r.ok());
    CHECK(has_message(r.diagnostics, "repeat count must be between"));
}

TEST_CASE("rejects a negative repeat count") {
    auto r = compile("(repeat -1 (stop))");
    CHECK_FALSE(r.ok());
    CHECK(has_message(r.diagnostics, "must not be negative"));
}

TEST_CASE("max total steps stops a runaway program") {
    auto compiled = compile("(repeat 100 (repeat 100 (stop)))");
    REQUIRE(compiled.ok());

    MockRobotHost robot;
    NullSink sink;
    ExecutionLimits limits;
    limits.max_total_steps = 50;
    auto result = execute(*compiled.root, robot, sink, limits);
    CHECK_FALSE(result.success);
    REQUIRE(result.error.has_value());
    CHECK(result.error->message.find("maximum step limit") != std::string::npos);
}

TEST_CASE("a runtime error calls robot.stop() as a safety fallback") {
    auto compiled = compile("(repeat 100 (repeat 100 (drive 40 1000)))");
    REQUIRE(compiled.ok());

    MockRobotHost robot;
    NullSink sink;
    ExecutionLimits limits;
    limits.max_total_steps = 10;
    auto result = execute(*compiled.root, robot, sink, limits);
    CHECK_FALSE(result.success);
    REQUIRE_FALSE(robot.commands.empty());
    CHECK(robot.commands.back().name == "stop");
}

TEST_CASE("error diagnostics format with line and column") {
    auto r = compile("(fly 10)");
    REQUIRE_FALSE(r.ok());
    REQUIRE_FALSE(r.diagnostics.empty());
    const std::string text = format_diagnostic(r.diagnostics[0]);
    CHECK(text.find("line 1") != std::string::npos);
    CHECK(text.find("column 2") != std::string::npos);
}
