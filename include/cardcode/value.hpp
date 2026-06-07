#pragma once

#include <cstdint>

#include "cardcode/robot_host.hpp"

namespace cardcode {

enum class ValueKind {
    None,
    Integer,
    Boolean,
    Color
};

// Runtime value produced by expression evaluation. Statements evaluate to None.
struct Value {
    ValueKind kind{ValueKind::None};
    std::int64_t integer{};
    bool boolean{};
    Color color{Color::Off};

    static Value none() { return Value{}; }
    static Value of_int(std::int64_t v) { Value x; x.kind = ValueKind::Integer; x.integer = v; return x; }
    static Value of_bool(bool v) { Value x; x.kind = ValueKind::Boolean; x.boolean = v; return x; }
    static Value of_color(Color c) { Value x; x.kind = ValueKind::Color; x.color = c; return x; }

    bool truthy() const {
        switch (kind) {
            case ValueKind::Boolean: return boolean;
            case ValueKind::Integer: return integer != 0;
            case ValueKind::Color:   return true;
            case ValueKind::None:    return false;
        }
        return false;
    }
};

} // namespace cardcode
