#include "doctest.h"

#include <vector>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct RecordingSink : ExecutionEventSink {
    std::vector<ExecutionEvent> events;
    void on_event(const ExecutionEvent& e) override { events.push_back(e); }
};

bool ran(const MockRobotHost& robot, const char* name) {
    for (const auto& c : robot.commands) {
        if (c.name == name) return true;
    }
    return false;
}
} // namespace

TEST_CASE("when runs its body only if the condition is truthy") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.distance_value = 10; // closer than the threshold

    auto compiled = compile("(when (< (distance-cm) 20) (stop) (light red))");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "stop"));
    CHECK(ran(robot, "set_light"));
}

TEST_CASE("when skips its body if the condition is false") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.distance_value = 100; // far away

    auto r = compile_and_run("(when (< (distance-cm) 20) (stop))", robot, sink);
    REQUIRE(r.success);
    CHECK_FALSE(ran(robot, "stop"));
}

TEST_CASE("if chooses the then branch when truthy") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.line_left_value = true;

    auto r = compile_and_run("(if (line-left) (turn-left 10) (drive 25 200))", robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "turn_left"));
    CHECK_FALSE(ran(robot, "drive_forward"));
}

TEST_CASE("if chooses the else branch when falsy") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.line_left_value = false;

    auto r = compile_and_run("(if (line-left) (turn-left 10) (drive 25 200))", robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "drive_forward"));
    CHECK_FALSE(ran(robot, "turn_left"));
}

TEST_CASE("light accepts named colors") {
    MockRobotHost robot;
    RecordingSink sink;
    auto r = compile_and_run("(light green)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].name == "set_light");
    CHECK(robot.commands[0].arg0 == static_cast<int>(Color::Green));
}

TEST_CASE("an unknown color name is an undefined variable at runtime") {
    // With variables, a bare symbol like `purple` is a variable reference, so an
    // unknown color is caught at runtime (undefined variable) rather than compile time.
    MockRobotHost robot;
    RecordingSink sink;
    auto compiled = compile("(light purple)");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink);
    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->message.find("undefined variable") != std::string::npos);
}

TEST_CASE("boolean and/or short-circuit") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.button_value = true;
    robot.distance_value = 5;
    auto r = compile_and_run(
        "(when (and (button) (< (distance-cm) 10)) (beep))", robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "beep"));
}

TEST_CASE("not negates a condition") {
    MockRobotHost robot;
    RecordingSink sink;
    robot.button_value = false;
    auto r = compile_and_run("(when (not (button)) (beep))", robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "beep"));
}

TEST_CASE("arithmetic evaluates inside conditions") {
    MockRobotHost robot;
    RecordingSink sink;
    auto r = compile_and_run("(when (= (+ 2 3) 5) (beep))", robot, sink);
    REQUIRE(r.success);
    CHECK(ran(robot, "beep"));
}

TEST_CASE("sensor reads emit highlight events") {
    MockRobotHost robot;
    RecordingSink sink;
    auto compiled = compile("(when (< (distance-cm) 20) (stop))");
    REQUIRE(compiled.ok());
    robot.distance_value = 5;
    execute(*compiled.root, robot, sink);

    bool sensor_event = false;
    for (const auto& e : sink.events) {
        if (e.type == ExecutionEventType::NodeStart && e.message == "distance-cm") {
            sensor_event = true;
        }
    }
    CHECK(sensor_event);
}

TEST_CASE("operators are not highlightable and emit no events") {
    MockRobotHost robot;
    RecordingSink sink;
    auto compiled = compile("(when (< (distance-cm) 20) (stop))");
    REQUIRE(compiled.ok());
    execute(*compiled.root, robot, sink);

    for (const auto& e : sink.events) {
        if (e.type == ExecutionEventType::NodeStart) {
            CHECK(e.message != "<");
        }
    }
}

TEST_CASE("division by zero is a runtime error that stops the robot") {
    MockRobotHost robot;
    RecordingSink sink;
    auto compiled = compile("(when (= (/ 10 0) 1) (beep))");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink);
    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->message.find("division by zero") != std::string::npos);
    REQUIRE_FALSE(robot.commands.empty());
    CHECK(robot.commands.back().name == "stop");
}

TEST_CASE("an empty list is still rejected") {
    // CardCode is expression-oriented now, so a bare comparison like (< 1 2) is a
    // legal (if pointless) top-level expression. An empty list remains invalid.
    CHECK(compile("(< 1 2)").ok());
    CHECK_FALSE(compile("()").ok());
}
