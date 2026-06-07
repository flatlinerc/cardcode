#pragma once

namespace cardcode {

// Color is part of the longer-term host API (see HANDOFF.md). It is declared
// here for forward compatibility but unused by the v1 movement slice.
enum class Color {
    Off,
    Red,
    Green,
    Blue,
    Yellow,
    White
};

// The engine never touches hardware directly; it drives this interface.
//
// v1 vertical slice exposes only the movement/timing subset. Sensors
// (distance_cm/button/line_*), set_light, and beep are deferred to later
// milestones and will be added here when their commands are implemented.
class RobotHost {
public:
    virtual ~RobotHost() = default;

    virtual void drive_forward(int speed, int duration_ms) = 0;
    virtual void drive_backward(int speed, int duration_ms) = 0;
    virtual void turn_left(int degrees) = 0;
    virtual void turn_right(int degrees) = 0;
    virtual void stop() = 0;
    virtual void wait_ms(int duration_ms) = 0;
};

} // namespace cardcode
