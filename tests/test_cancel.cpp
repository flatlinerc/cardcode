#include "doctest.h"

#include <atomic>

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct NullSink : ExecutionEventSink {
    void on_event(const ExecutionEvent&) override {}
};
} // namespace

TEST_CASE("a pre-set cancel flag halts before doing any work") {
    MockRobotHost robot;
    NullSink sink;
    std::atomic<bool> cancel{true};

    auto compiled = compile("(repeat 10 (drive 40 1000))");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink, ExecutionLimits{}, &cancel);

    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->message.find("cancelled") != std::string::npos);
    // No drive commands; only the safety stop.
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].name == "stop");
}

TEST_CASE("cancellation observed mid-run stops the robot") {
    struct CancelOnFirstDrive : MockRobotHost {
        std::atomic<bool>* flag = nullptr;
        void drive_forward(int speed, int ms) override {
            MockRobotHost::drive_forward(speed, ms);
            if (flag) flag->store(true); // request cancel after the first drive
        }
    };

    CancelOnFirstDrive robot;
    NullSink sink;
    std::atomic<bool> cancel{false};
    robot.flag = &cancel;

    auto compiled = compile("(repeat 10 (drive 40 1000) (turn-right 90))");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink, ExecutionLimits{}, &cancel);

    CHECK_FALSE(r.success);
    // First drive ran, then cancellation was observed before further commands;
    // the last recorded command is the safety stop.
    CHECK(robot.commands.front().name == "drive_forward");
    CHECK(robot.commands.back().name == "stop");
    // Loop did not run to completion (10 iters would be 20 commands).
    CHECK(robot.commands.size() < 20);
}

TEST_CASE("no cancel flag runs normally") {
    MockRobotHost robot;
    NullSink sink;
    auto compiled = compile("(repeat 2 (stop))");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink); // cancel defaults to nullptr
    CHECK(r.success);
    CHECK(robot.commands.size() == 2);
}
