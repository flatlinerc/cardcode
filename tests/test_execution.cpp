#include "doctest.h"

#include <utility>
#include <vector>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct RecordingSink : ExecutionEventSink {
    std::vector<ExecutionEvent> events;
    void on_event(const ExecutionEvent& e) override { events.push_back(e); }
};
} // namespace

TEST_CASE("square emits the expected robot commands and node events") {
    auto compiled = compile("(repeat 2 (drive 40 1000) (turn-right 90))");
    REQUIRE(compiled.ok());

    MockRobotHost robot;
    RecordingSink sink;
    auto result = execute(*compiled.root, robot, sink);
    REQUIRE(result.success);

    REQUIRE(robot.commands.size() == 4);
    CHECK(robot.commands[0].name == "drive_forward");
    CHECK(robot.commands[0].arg0 == 40);
    CHECK(robot.commands[0].arg1 == 1000);
    CHECK(robot.commands[1].name == "turn_right");
    CHECK(robot.commands[1].arg0 == 90);
    CHECK(robot.commands[2].name == "drive_forward");
    CHECK(robot.commands[3].name == "turn_right");

    using T = ExecutionEventType;
    std::vector<std::pair<T, NodeId>> seq;
    for (const auto& e : sink.events) seq.emplace_back(e.type, e.node_id);

    const std::vector<std::pair<T, NodeId>> expected = {
        {T::ProgramStart, 0},
        {T::NodeStart, 0},                    // repeat
        {T::NodeStart, 1}, {T::NodeDone, 1},  // drive
        {T::NodeStart, 2}, {T::NodeDone, 2},  // turn-right
        {T::NodeStart, 1}, {T::NodeDone, 1},  // drive (2nd iter)
        {T::NodeStart, 2}, {T::NodeDone, 2},  // turn-right (2nd iter)
        {T::NodeDone, 0},                     // repeat
        {T::ProgramDone, 0},
    };
    CHECK(seq == expected);
}

TEST_CASE("repeated statements keep the same node id on each iteration") {
    auto compiled = compile("(repeat 3 (stop))");
    REQUIRE(compiled.ok());

    MockRobotHost robot;
    RecordingSink sink;
    execute(*compiled.root, robot, sink);

    int stop_starts = 0;
    for (const auto& e : sink.events) {
        if (e.type == ExecutionEventType::NodeStart && e.node_id == 1) ++stop_starts;
    }
    CHECK(stop_starts == 3);
    CHECK(robot.commands.size() == 3);
}

TEST_CASE("node events carry source spans") {
    auto compiled = compile("(drive 40 1000)");
    REQUIRE(compiled.ok());

    MockRobotHost robot;
    RecordingSink sink;
    execute(*compiled.root, robot, sink);

    bool found = false;
    for (const auto& e : sink.events) {
        if (e.type == ExecutionEventType::NodeStart && e.node_id == 0) {
            CHECK(e.span.end_offset > e.span.start_offset);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("compile_and_run drives a program end-to-end") {
    MockRobotHost robot;
    RecordingSink sink;
    auto result = compile_and_run("(do (drive 30 500) (stop))", robot, sink);
    CHECK(result.success);
    REQUIRE(robot.commands.size() == 2);
    CHECK(robot.commands[0].name == "drive_forward");
    CHECK(robot.commands[1].name == "stop");
}
