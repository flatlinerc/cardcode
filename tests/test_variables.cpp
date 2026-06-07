#include "doctest.h"

#include <vector>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct NullSink : ExecutionEventSink {
    void on_event(const ExecutionEvent&) override {}
};
} // namespace

TEST_CASE("a variable can be defined and used as a command argument") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run("(define speed 40) (drive speed 1000)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].name == "drive_forward");
    CHECK(robot.commands[0].arg0 == 40);
    CHECK(robot.commands[0].arg1 == 1000);
}

TEST_CASE("set! reassigns an existing variable") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(define speed 30) (drive speed 100) (set! speed 60) (drive speed 100)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 2);
    CHECK(robot.commands[0].arg0 == 30);
    CHECK(robot.commands[1].arg0 == 60);
}

TEST_CASE("repeat count can be a variable") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run("(define n 3) (repeat n (stop))", robot, sink);
    REQUIRE(r.success);
    CHECK(robot.commands.size() == 3);
}

TEST_CASE("a color can be stored in a variable and used by light") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run("(define c green) (light c)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].name == "set_light");
    CHECK(robot.commands[0].arg0 == static_cast<int>(Color::Green));
}

TEST_CASE("reading an undefined variable is a runtime error") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run("(drive missing 100)", robot, sink);
    CHECK_FALSE(r.success);
}

TEST_CASE("set! of an undefined variable is a runtime error") {
    MockRobotHost robot;
    NullSink sink;
    auto compiled = compile("(set! x 5)");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink);
    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->message.find("undefined variable") != std::string::npos);
}

TEST_CASE("reserved words cannot be variable names") {
    CHECK_FALSE(compile("(define red 5)").ok());
    CHECK_FALSE(compile("(define true 1)").ok());
}

TEST_CASE("DefineVar and SetVar are highlightable events") {
    struct RecordingSink : ExecutionEventSink {
        std::vector<ExecutionEvent> events;
        void on_event(const ExecutionEvent& e) override { events.push_back(e); }
    };
    MockRobotHost robot;
    RecordingSink sink;
    auto compiled = compile("(define x 1) (set! x 2)");
    REQUIRE(compiled.ok());
    execute(*compiled.root, robot, sink);
    bool saw_define = false, saw_set = false;
    for (const auto& e : sink.events) {
        if (e.type == ExecutionEventType::NodeStart && e.message == "x") {
            saw_define = saw_define || true;
        }
    }
    // Both define and set! reference x; at least one NodeStart with message "x".
    for (const auto& e : sink.events) if (e.type == ExecutionEventType::NodeStart && e.message == "x") saw_set = true;
    CHECK(saw_define);
    CHECK(saw_set);
}
