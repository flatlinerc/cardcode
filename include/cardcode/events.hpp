#pragma once

#include <string>

#include "cardcode/ast.hpp"
#include "cardcode/source_span.hpp"

namespace cardcode {

enum class ExecutionEventType {
    ProgramStart,
    ProgramDone,
    ProgramError,
    NodeStart,
    NodeDone,
    NodeSkipped,
    NodeError,
    Log
};

// The bridge from runtime execution to UI highlighting. Each node event carries
// the deterministic node ID and its source span so the UI can highlight the
// exact source range / card.
struct ExecutionEvent {
    ExecutionEventType type{};
    NodeId node_id{};       // meaningful for Node* events
    SourceSpan span{};      // source range for UI highlight
    std::string message;    // operator name, error, or log text
};

class ExecutionEventSink {
public:
    virtual ~ExecutionEventSink() = default;
    virtual void on_event(const ExecutionEvent& event) = 0;
};

inline const char* event_type_name(ExecutionEventType type) {
    switch (type) {
        case ExecutionEventType::ProgramStart: return "ProgramStart";
        case ExecutionEventType::ProgramDone:  return "ProgramDone";
        case ExecutionEventType::ProgramError: return "ProgramError";
        case ExecutionEventType::NodeStart:    return "NodeStart";
        case ExecutionEventType::NodeDone:     return "NodeDone";
        case ExecutionEventType::NodeSkipped:  return "NodeSkipped";
        case ExecutionEventType::NodeError:    return "NodeError";
        case ExecutionEventType::Log:          return "Log";
    }
    return "?";
}

} // namespace cardcode
