#include "doctest.h"

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
// A host whose distance sensor counts down on each read, so a while loop
// driven by (distance-cm) terminates.
struct CountdownHost : MockRobotHost {
    int reading = 5;
    int distance_cm() override { return reading > 0 ? reading-- : 0; }
};

struct NullSink : ExecutionEventSink {
    void on_event(const ExecutionEvent&) override {}
};
} // namespace

TEST_CASE("while loops until its condition becomes false") {
    CountdownHost robot;
    NullSink sink;
    auto r = compile_and_run("(while (> (distance-cm) 0) (drive 30 100)) (stop)", robot, sink);
    REQUIRE(r.success);
    int drives = 0;
    for (const auto& c : robot.commands) if (c.name == "drive_forward") ++drives;
    CHECK(drives == 5);
    CHECK(robot.commands.back().name == "stop");
}

TEST_CASE("while with a mutable counter") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(define i 0) (while (< i 3) (beep) (set! i (+ i 1)))", robot, sink);
    REQUIRE(r.success);
    int beeps = 0;
    for (const auto& c : robot.commands) if (c.name == "beep") ++beeps;
    CHECK(beeps == 3);
}

TEST_CASE("an infinite while is stopped by the step limit") {
    MockRobotHost robot;
    NullSink sink;
    auto compiled = compile("(while true (beep))");
    REQUIRE(compiled.ok());
    ExecutionLimits limits;
    limits.max_total_steps = 50;
    auto r = execute(*compiled.root, robot, sink, limits);
    CHECK_FALSE(r.success);
    CHECK(robot.commands.back().name == "stop");
}
