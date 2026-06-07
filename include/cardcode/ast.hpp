#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cardcode/source_span.hpp"

namespace cardcode {

using NodeId = std::uint32_t;

// Executable node kinds for the v1 vertical slice. Conditionals, sensors,
// operators, and colors are intentionally deferred to later milestones.
enum class NodeKind {
    Do,
    Repeat,
    Drive,
    Backward,
    TurnLeft,
    TurnRight,
    Stop,
    Wait
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

// One executable AST node. Numeric arguments are stored inline in `nums` rather
// than as child nodes, so only control-flow and command expressions receive IDs
// and emit highlight events -- literals never do.
struct Node {
    NodeId id{};
    NodeKind kind{};
    SourceSpan span{};

    std::string op;                  // original operator name, used for display/events
    std::vector<NodePtr> children;   // child statements (do/repeat bodies only)
    std::vector<std::int64_t> nums;  // numeric literal arguments
};

inline std::string node_id_to_string(NodeId id) {
    return "n" + std::to_string(id);
}

inline const char* node_kind_name(NodeKind kind) {
    switch (kind) {
        case NodeKind::Do:        return "do";
        case NodeKind::Repeat:    return "repeat";
        case NodeKind::Drive:     return "drive";
        case NodeKind::Backward:  return "backward";
        case NodeKind::TurnLeft:  return "turn-left";
        case NodeKind::TurnRight: return "turn-right";
        case NodeKind::Stop:      return "stop";
        case NodeKind::Wait:      return "wait";
    }
    return "?";
}

// Deterministic pre-order ID assignment over the highlightable AST. Identical
// source text always yields identical IDs.
void assign_node_ids(Node& root);

} // namespace cardcode
