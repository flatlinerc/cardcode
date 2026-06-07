#pragma once

#include <string>
#include <vector>

#include "cardcode/robot_host.hpp"

namespace cardcode {

// One recorded robot call. `arg0`/`arg1` carry whatever the command takes:
//   drive_forward/drive_backward -> (speed, duration_ms)
//   turn_left/turn_right         -> (degrees, 0)
//   wait_ms                      -> (duration_ms, 0)
//   set_light                    -> ((int)Color, 0)
//   stop / beep                  -> (0, 0)
struct RobotCommand {
    std::string name;
    int arg0{};
    int arg1{};
};

// Test/CLI host that records actuator calls instead of moving hardware, and
// returns programmable sensor values.
class MockRobotHost : public RobotHost {
public:
    std::vector<RobotCommand> commands;

    // Programmable sensor readings (set these in tests before running).
    int distance_value = 1000;
    bool button_value = false;
    bool line_left_value = false;
    bool line_right_value = false;

    void drive_forward(int speed, int duration_ms) override {
        commands.push_back({"drive_forward", speed, duration_ms});
    }
    void drive_backward(int speed, int duration_ms) override {
        commands.push_back({"drive_backward", speed, duration_ms});
    }
    void turn_left(int degrees) override { commands.push_back({"turn_left", degrees, 0}); }
    void turn_right(int degrees) override { commands.push_back({"turn_right", degrees, 0}); }
    void stop() override { commands.push_back({"stop", 0, 0}); }
    void wait_ms(int duration_ms) override { commands.push_back({"wait_ms", duration_ms, 0}); }
    void set_light(Color color) override {
        commands.push_back({"set_light", static_cast<int>(color), 0});
    }
    void beep() override { commands.push_back({"beep", 0, 0}); }

    int distance_cm() override { return distance_value; }
    bool button_pressed() override { return button_value; }
    bool line_left() override { return line_left_value; }
    bool line_right() override { return line_right_value; }
};

} // namespace cardcode
