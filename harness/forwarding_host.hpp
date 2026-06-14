#pragma once

// A RobotHost for the desktop harness: it forwards each actuator call to the UI
// as a `robotCommand` message and returns programmable (UI-injected) sensor
// values, emitting a `sensorRead` for each read. Sensor fields are atomic so the
// reader thread (engine) and writer (control messages) don't race.

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

#include "cardcode/robot_host.hpp"

#include "json.hpp"

namespace cardcode::harness {

class ForwardingRobotHost : public RobotHost {
public:
    using Emit = std::function<void(const std::string&)>;

    explicit ForwardingRobotHost(Emit emit, bool realtime = false)
        : emit_(std::move(emit)), realtime_(realtime) {}

    void reset() {
        distance_.store(1000);
        button_.store(false);
        line_left_.store(false);
        line_right_.store(false);
    }

    // --- UI-injected sensor values (from setSensor control messages) ---
    void set_distance(int cm) { distance_.store(cm); }
    void set_button(bool v)   { button_.store(v); }
    void set_line_left(bool v)  { line_left_.store(v); }
    void set_line_right(bool v) { line_right_.store(v); }

    // --- Actuators: forward as robotCommand, then (optionally) take real time ---
    void drive_forward(int speed, int duration_ms) override {
        command("drive_forward", {{"speed", jnum(speed)}, {"durationMs", jnum(duration_ms)}});
        delay(duration_ms);
    }
    void drive_backward(int speed, int duration_ms) override {
        command("drive_backward", {{"speed", jnum(speed)}, {"durationMs", jnum(duration_ms)}});
        delay(duration_ms);
    }
    void turn_left(int degrees) override {
        command("turn_left", {{"degrees", jnum(degrees)}});
        delay(turn_duration_ms(degrees));
    }
    void turn_right(int degrees) override {
        command("turn_right", {{"degrees", jnum(degrees)}});
        delay(turn_duration_ms(degrees));
    }
    void stop() override { command("stop", {}); }
    void wait_ms(int duration_ms) override {
        command("wait", {{"durationMs", jnum(duration_ms)}});
        delay(duration_ms);
    }
    void set_light(Color color) override {
        command("set_light", {{"color", jstr(color_name(color))}});
    }
    void beep() override {
        command("beep", {});
        delay(kBeepMs);
    }

    // --- Sensors: report the injected value and echo it as sensorRead ---
    int distance_cm() override   { int v = distance_.load();  sensor("distance-cm", jnum(v)); return v; }
    bool button_pressed() override { bool v = button_.load();   sensor("button", jbool(v));     return v; }
    bool line_left() override     { bool v = line_left_.load(); sensor("line-left", jbool(v));   return v; }
    bool line_right() override    { bool v = line_right_.load();sensor("line-right", jbool(v));  return v; }

private:
    void command(const char* name, std::initializer_list<JField> args) {
        emit_(jobj({{"type", jstr("robotCommand")},
                    {"command", jstr(name)},
                    {"args", jobj(args)}}));
    }
    void sensor(const char* name, const std::string& value_json) {
        emit_(jobj({{"type", jstr("sensorRead")},
                    {"sensor", jstr(name)},
                    {"value", value_json}}));
    }
    void delay(int ms) {
        if (realtime_ && ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // Turns carry only an angle, so derive a wall-clock duration from a fixed
    // turn rate (90 deg/sec) for realtime pacing; beep gets a fixed duration.
    static constexpr int kTurnDegPerSec = 90;
    static constexpr int kBeepMs = 250;
    static int turn_duration_ms(int degrees) {
        int magnitude = degrees < 0 ? -degrees : degrees;
        return magnitude * 1000 / kTurnDegPerSec;
    }

    Emit emit_;
    bool realtime_;
    std::atomic<int>  distance_{1000};
    std::atomic<bool> button_{false};
    std::atomic<bool> line_left_{false};
    std::atomic<bool> line_right_{false};
};

} // namespace cardcode::harness
