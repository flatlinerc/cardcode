#pragma once

namespace cardcode {

enum class Color {
    Off,
    Red,
    Green,
    Blue,
    Yellow,
    White
};

inline const char* color_name(Color c) {
    switch (c) {
        case Color::Off:    return "off";
        case Color::Red:    return "red";
        case Color::Green:  return "green";
        case Color::Blue:   return "blue";
        case Color::Yellow: return "yellow";
        case Color::White:  return "white";
    }
    return "?";
}

// The engine never touches hardware directly; it drives this interface.
class RobotHost {
public:
    virtual ~RobotHost() = default;

    // Actuators
    virtual void drive_forward(int speed, int duration_ms) = 0;
    virtual void drive_backward(int speed, int duration_ms) = 0;
    virtual void turn_left(int degrees) = 0;
    virtual void turn_right(int degrees) = 0;
    virtual void stop() = 0;
    virtual void wait_ms(int duration_ms) = 0;
    virtual void set_light(Color color) = 0;
    virtual void beep() = 0;

    // Sensors
    virtual int distance_cm() = 0;
    virtual bool button_pressed() = 0;
    virtual bool line_left() = 0;
    virtual bool line_right() = 0;
};

} // namespace cardcode
