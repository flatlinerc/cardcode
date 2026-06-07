#include "doctest.h"

#include "cardcode/engine.hpp"
#include "cardcode/mock_robot_host.hpp"

using namespace cardcode;

namespace {
struct NullSink : ExecutionEventSink {
    void on_event(const ExecutionEvent&) override {}
};
} // namespace

TEST_CASE("a function returns the value of its last expression") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(define (square n) (* n n)) (drive (square 6) 1000)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].arg0 == 36);
}

TEST_CASE("functions are hoisted (callable before their definition)") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(drive (dbl 20) 500) (define (dbl n) (* n 2))", robot, sink);
    REQUIRE(r.success);
    CHECK(robot.commands[0].arg0 == 40);
}

TEST_CASE("a function body can issue robot commands") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(define (corner) (drive 40 1000) (turn-right 90)) (corner) (corner)", robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 4);
    CHECK(robot.commands[0].name == "drive_forward");
    CHECK(robot.commands[1].name == "turn_right");
}

TEST_CASE("parameters are local to the call (flat scope)") {
    MockRobotHost robot;
    NullSink sink;
    // n inside f does not leak; the outer drive uses the global n.
    auto r = compile_and_run(
        "(define (f n) (* n 10)) (define n 2) (drive (f 5) 100) (drive n 100)",
        robot, sink);
    REQUIRE(r.success);
    CHECK(robot.commands[0].arg0 == 50);
    CHECK(robot.commands[1].arg0 == 2);
}

TEST_CASE("recursion works") {
    MockRobotHost robot;
    NullSink sink;
    // Route the result through wait (range 0..10000); fac(5) = 120 would exceed
    // drive's speed cap of 100.
    auto r = compile_and_run(
        "(define (fac n) (if (<= n 1) 1 (* n (fac (- n 1))))) (wait (fac 5))",
        robot, sink);
    REQUIRE(r.success);
    REQUIRE(robot.commands.size() == 1);
    CHECK(robot.commands[0].name == "wait_ms");
    CHECK(robot.commands[0].arg0 == 120);
}

TEST_CASE("unbounded recursion is stopped by the call-depth limit") {
    MockRobotHost robot;
    NullSink sink;
    auto compiled = compile("(define (loop n) (loop (+ n 1))) (loop 0)");
    REQUIRE(compiled.ok());
    auto r = execute(*compiled.root, robot, sink);
    CHECK_FALSE(r.success);
    REQUIRE(r.error.has_value());
    CHECK(r.error->message.find("call depth") != std::string::npos);
    CHECK(robot.commands.back().name == "stop");
}

TEST_CASE("calling an unknown function is a compile error") {
    CHECK_FALSE(compile("(nope 1 2)").ok());
}

TEST_CASE("wrong argument count is a compile error") {
    CHECK_FALSE(compile("(define (f a b) (+ a b)) (f 1)").ok());
}

TEST_CASE("a function defined inside a body is a compile error") {
    CHECK_FALSE(compile("(repeat 2 (define (f) (stop)))").ok());
}

TEST_CASE("redefining a built-in is a compile error") {
    CHECK_FALSE(compile("(define (drive x) (stop))").ok());
}

TEST_CASE("duplicate function definitions are a compile error") {
    CHECK_FALSE(compile("(define (f) (stop)) (define (f) (beep))").ok());
}

TEST_CASE("function and variable share a name without colliding (Lisp-2)") {
    MockRobotHost robot;
    NullSink sink;
    auto r = compile_and_run(
        "(define (size) 7) (define size 3) (drive (size) 100) (drive size 100)",
        robot, sink);
    REQUIRE(r.success);
    CHECK(robot.commands[0].arg0 == 7); // (size) -> function
    CHECK(robot.commands[1].arg0 == 3); // size  -> variable
}
