#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cardcode/source_span.hpp"

namespace cardcode {

using NodeId = std::uint32_t;

enum class NodeKind {
    // Control flow
    Do,
    Repeat,
    When,
    If,
    While,
    // Variables and functions
    DefineVar,
    DefineFunc,
    SetVar,
    VarRef,
    Call,
    // Robot commands
    Drive,
    Backward,
    TurnLeft,
    TurnRight,
    Stop,
    Wait,
    Light,
    Beep,
    // Sensor reads (value-producing, highlightable)
    DistanceCm,
    Button,
    LineLeft,
    LineRight,
    // Operators (value-producing, not highlightable)
    Compare,
    BooleanOp,
    Arithmetic,
    // Literals (value-producing, not highlightable)
    IntegerLiteral,
    BooleanLiteral,
    ColorLiteral
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

// One executable AST node.
//
// `nums` holds inline numeric/enum literals for command arguments (drive speed,
// turn degrees, light color, repeat count, ...). Those are not represented as
// child nodes, so command/control IDs stay compact and stable.
//
// `args` holds operand/condition *expressions* (the condition of when/if, the
// then/else branches of if, and operator operands). `children` holds the
// statement body of do/repeat/when.
struct Node {
    NodeId id{};
    NodeKind kind{};
    SourceSpan span{};

    std::string op;                    // operator/variable/function name; display/events
    std::vector<NodePtr> args;         // operand/condition/branch/call-argument expressions
    std::vector<NodePtr> children;     // child statements (do/repeat/when/while/function bodies)
    std::vector<std::int64_t> nums;    // inline numeric/enum literal values
    std::vector<std::string> params;   // parameter names (DefineFunc only)
};

// Highlightable nodes emit NodeStart/NodeDone events and count toward the step
// limit. Operators and literals are evaluated silently.
inline bool is_highlightable(NodeKind kind) {
    switch (kind) {
        case NodeKind::Do:
        case NodeKind::Repeat:
        case NodeKind::When:
        case NodeKind::If:
        case NodeKind::While:
        case NodeKind::DefineVar:
        case NodeKind::SetVar:
        case NodeKind::Call:
        case NodeKind::Drive:
        case NodeKind::Backward:
        case NodeKind::TurnLeft:
        case NodeKind::TurnRight:
        case NodeKind::Stop:
        case NodeKind::Wait:
        case NodeKind::Light:
        case NodeKind::Beep:
        case NodeKind::DistanceCm:
        case NodeKind::Button:
        case NodeKind::LineLeft:
        case NodeKind::LineRight:
            return true;
        case NodeKind::DefineFunc:
        case NodeKind::VarRef:
        case NodeKind::Compare:
        case NodeKind::BooleanOp:
        case NodeKind::Arithmetic:
        case NodeKind::IntegerLiteral:
        case NodeKind::BooleanLiteral:
        case NodeKind::ColorLiteral:
            return false;
    }
    return false;
}

inline std::string node_id_to_string(NodeId id) {
    return "n" + std::to_string(id);
}

inline const char* node_kind_name(NodeKind kind) {
    switch (kind) {
        case NodeKind::Do:             return "do";
        case NodeKind::Repeat:         return "repeat";
        case NodeKind::When:           return "when";
        case NodeKind::If:             return "if";
        case NodeKind::While:          return "while";
        case NodeKind::DefineVar:      return "define";
        case NodeKind::DefineFunc:     return "define-func";
        case NodeKind::SetVar:         return "set!";
        case NodeKind::VarRef:         return "var-ref";
        case NodeKind::Call:           return "call";
        case NodeKind::Drive:          return "drive";
        case NodeKind::Backward:       return "backward";
        case NodeKind::TurnLeft:       return "turn-left";
        case NodeKind::TurnRight:      return "turn-right";
        case NodeKind::Stop:           return "stop";
        case NodeKind::Wait:           return "wait";
        case NodeKind::Light:          return "light";
        case NodeKind::Beep:           return "beep";
        case NodeKind::DistanceCm:     return "distance-cm";
        case NodeKind::Button:         return "button";
        case NodeKind::LineLeft:       return "line-left";
        case NodeKind::LineRight:      return "line-right";
        case NodeKind::Compare:        return "compare";
        case NodeKind::BooleanOp:      return "boolean-op";
        case NodeKind::Arithmetic:     return "arithmetic";
        case NodeKind::IntegerLiteral: return "integer";
        case NodeKind::BooleanLiteral: return "boolean";
        case NodeKind::ColorLiteral:   return "color";
    }
    return "?";
}

// Deterministic pre-order ID assignment over the highlightable AST. Identical
// source text always yields identical IDs.
void assign_node_ids(Node& root);

} // namespace cardcode
