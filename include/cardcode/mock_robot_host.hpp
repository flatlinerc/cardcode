#pragma once

#include <string>
#include <vector>

#include "cardcode/robot_host.hpp"

namespace cardcode {

// One recorded robot call. `arg0`/`arg1` carry whatever the command takes:
//   drive_forward/drive_backward -> (speed, duration_ms)
//   turn_left/turn_right         -> (degrees, 0)
//   wait_ms                      -> (duration_ms, 0)
//   stop                         -> (0, 0)
struct RobotCommand {
    std::string name;
    int arg0{};
    int arg1{};
};

// Test/CLI host that records commands instead of moving hardware.
class MockRobotHost : public RobotHost {
public:
    std::vector<RobotCommand> commands;

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
};

} // namespace cardcode
