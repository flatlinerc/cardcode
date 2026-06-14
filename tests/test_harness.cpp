#include "doctest.h"

#include <chrono>
#include <string>
#include <vector>

#include "json.hpp"
#include "protocol_bridge.hpp"

using namespace cardcode::harness;

// Note: JSON literals use the R"CC(...)CC" delimiter because the embedded
// CardCode source ends in ')', and the default R"(...)" delimiter would be
// terminated early by the )" sequence in e.g. (stop)".

namespace {
// Capture emitted protocol messages. async=false so a `run` completes inline and
// every message is present by the time handle() returns.
struct Capture {
    std::vector<std::string> out;
    ProtocolBridge bridge;
    Capture() : bridge([this](const std::string& m) { out.push_back(m); },
                       BridgeOptions{/*async=*/false, /*realtime=*/false}) {}

    void send(const std::string& m) { bridge.handle(m); }

    bool has_type(const std::string& type) const {
        for (const auto& m : out) {
            auto v = json_parse(m);
            if (v && v->find("type") && v->find("type")->as_str() == type) return true;
        }
        return false;
    }
    int count_type(const std::string& type) const {
        int n = 0;
        for (const auto& m : out) {
            auto v = json_parse(m);
            if (v && v->find("type") && v->find("type")->as_str() == type) ++n;
        }
        return n;
    }
    std::vector<JsonValue> of_type(const std::string& type) const {
        std::vector<JsonValue> r;
        for (const auto& m : out) {
            auto v = json_parse(m);
            if (v && v->find("type") && v->find("type")->as_str() == type) r.push_back(*v);
        }
        return r;
    }
};
} // namespace

TEST_CASE("json round-trips strings with escapes") {
    auto v = json_parse(R"CC({"type":"run","source":"(drive 40 1000) ; hi\n(stop)"})CC");
    REQUIRE(v.has_value());
    REQUIRE(v->is_obj());
    CHECK(v->find("type")->as_str() == "run");
    CHECK(v->find("source")->as_str() == "(drive 40 1000) ; hi\n(stop)");
}

TEST_CASE("compile reports ok with no diagnostics for valid source") {
    Capture c;
    c.send(R"CC({"type":"compile","source":"(drive 40 1000)"})CC");
    auto diags = c.of_type("diagnostics");
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].find("ok")->as_bool() == true);
    CHECK_FALSE(c.has_type("programStart")); // compile does not run
}

TEST_CASE("compile reports diagnostics for invalid source") {
    Capture c;
    c.send(R"CC({"type":"compile","source":"(fly 1)"})CC");
    auto diags = c.of_type("diagnostics");
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].find("ok")->as_bool() == false);
    REQUIRE(diags[0].find("diagnostics")->arr.size() == 1);
    CHECK(diags[0].find("diagnostics")->arr[0].find("message")->as_str().find("unknown operation") != std::string::npos);
}

TEST_CASE("run streams diagnostics, program lifecycle, nodes, and robot commands") {
    Capture c;
    c.send(R"CC({"type":"run","source":"(repeat 2 (drive 40 1000) (turn-right 90))"})CC");

    CHECK(c.has_type("diagnostics"));
    CHECK(c.has_type("programStart"));
    CHECK(c.has_type("programDone"));
    CHECK_FALSE(c.has_type("programError"));

    // Two iterations: drive + turn-right each -> 4 robot commands.
    auto cmds = c.of_type("robotCommand");
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[0].find("command")->as_str() == "drive_forward");
    CHECK(cmds[0].find("args")->find("speed")->as_int() == 40);
    CHECK(cmds[0].find("args")->find("durationMs")->as_int() == 1000);
    CHECK(cmds[1].find("command")->as_str() == "turn_right");
    CHECK(cmds[1].find("args")->find("degrees")->as_int() == 90);

    // repeat=n0, drive=n1, turn-right=n2 -> n1 starts twice.
    int drive_starts = 0;
    for (const auto& n : c.of_type("node")) {
        if (n.find("event")->as_str() == "start" && n.find("id")->as_str() == "n1") ++drive_starts;
    }
    CHECK(drive_starts == 2);
}

TEST_CASE("ordering: robotCommand falls between its node start and done") {
    Capture c;
    c.send(R"CC({"type":"run","source":"(drive 40 1000)"})CC");

    int i_start = -1, i_cmd = -1, i_done = -1;
    for (int i = 0; i < static_cast<int>(c.out.size()); ++i) {
        auto v = json_parse(c.out[i]);
        const std::string type = v->find("type")->as_str();
        if (type == "node" && v->find("event")->as_str() == "start" && v->find("id")->as_str() == "n0") i_start = i;
        if (type == "robotCommand") i_cmd = i;
        if (type == "node" && v->find("event")->as_str() == "done" && v->find("id")->as_str() == "n0") i_done = i;
    }
    REQUIRE(i_start >= 0);
    REQUIRE(i_cmd >= 0);
    REQUIRE(i_done >= 0);
    CHECK(i_start < i_cmd);
    CHECK(i_cmd < i_done);
}

TEST_CASE("setSensor injects values that drive conditionals") {
    Capture c;
    c.send(R"CC({"type":"setSensor","sensor":"distance-cm","value":10})CC");
    c.send(R"CC({"type":"run","source":"(when (< (distance-cm) 20) (light red))"})CC");
    auto cmds = c.of_type("robotCommand");
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].find("command")->as_str() == "set_light");
    CHECK(cmds[0].find("args")->find("color")->as_str() == "red");
}

TEST_CASE("setSensor (far) makes the when body skip") {
    Capture c;
    c.send(R"CC({"type":"setSensor","sensor":"distance-cm","value":100})CC");
    c.send(R"CC({"type":"run","source":"(when (< (distance-cm) 20) (light red))"})CC");
    CHECK(c.count_type("robotCommand") == 0);
    CHECK(c.has_type("programDone"));

    // The engine must emit a `node` message with event "skipped" for the body
    // of the `when`, since the condition is false and the body is not run.
    int skipped = 0;
    for (const auto& n : c.of_type("node")) {
        if (n.find("event")->as_str() == "skipped") ++skipped;
    }
    CHECK(skipped >= 1);
}

TEST_CASE("a runtime error yields programError and a safety stop") {
    Capture c;
    c.send(R"CC({"type":"run","source":"(when (= (/ 1 0) 0) (beep))"})CC");
    CHECK(c.has_type("programError"));
    // engine called robot.stop() as the safety fallback
    auto cmds = c.of_type("robotCommand");
    REQUIRE_FALSE(cmds.empty());
    CHECK(cmds.back().find("command")->as_str() == "stop");
}

TEST_CASE("variables and functions work through the protocol") {
    Capture c;
    c.send(R"CC({"type":"run","source":"(define (sq n) (* n n)) (wait (sq 7))"})CC");
    auto cmds = c.of_type("robotCommand");
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0].find("command")->as_str() == "wait");
    CHECK(cmds[0].find("args")->find("durationMs")->as_int() == 49);
}

// --- Realtime pacing -------------------------------------------------------
// In realtime mode the forwarding host sleeps for each command's duration so the
// UI highlight tracks a real run. Turns are paced at 90 deg/sec and beep gets a
// fixed 250 ms so both are visible; instant mode (the default) never sleeps.

namespace {
// Run one program synchronously and return wall-clock milliseconds elapsed.
long long run_elapsed_ms(bool realtime, const std::string& message) {
    std::vector<std::string> out;
    ProtocolBridge bridge([&out](const std::string& m) { out.push_back(m); },
                          BridgeOptions{/*async=*/false, realtime});
    auto t0 = std::chrono::steady_clock::now();
    bridge.handle(message);
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}
} // namespace

TEST_CASE("realtime mode paces a 90-degree turn at ~1 second") {
    long long ms = run_elapsed_ms(true, R"CC({"type":"run","source":"(turn-right 90)"})CC");
    CHECK(ms >= 900); // 90 deg / (90 deg/sec) ~= 1000 ms
}

TEST_CASE("realtime mode gives beep a visible duration") {
    long long ms = run_elapsed_ms(true, R"CC({"type":"run","source":"(beep)"})CC");
    CHECK(ms >= 230); // 250 ms beep, with slack for timer granularity
}

TEST_CASE("instant mode never sleeps for turns or beep") {
    long long ms = run_elapsed_ms(false, R"CC({"type":"run","source":"(turn-left 360) (beep)"})CC");
    CHECK(ms < 200);
}

TEST_CASE("malformed and unknown messages report an error, not a crash") {
    Capture c;
    c.send("not json");
    c.send(R"CC({"source":"(stop)"})CC");      // missing type
    c.send(R"CC({"type":"frobnicate"})CC");    // unknown type
    CHECK(c.count_type("error") == 3);
}
