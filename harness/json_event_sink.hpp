#pragma once

// Translates engine ExecutionEvents into protocol JSON messages (see
// docs/integration/protocol.md) and hands them to a thread-safe emit callback.

#include <functional>
#include <string>
#include <vector>

#include "cardcode/ast.hpp"
#include "cardcode/events.hpp"

#include "json.hpp"

namespace cardcode::harness {

class JsonEventSink : public ExecutionEventSink {
public:
    using Emit = std::function<void(const std::string&)>;

    explicit JsonEventSink(Emit emit) : emit_(std::move(emit)) {}

    void on_event(const ExecutionEvent& e) override {
        using T = ExecutionEventType;
        switch (e.type) {
            case T::ProgramStart: emit_(jobj({{"type", jstr("programStart")}})); break;
            case T::ProgramDone:  emit_(jobj({{"type", jstr("programDone")}}));  break;
            case T::ProgramError:
                emit_(jobj({{"type", jstr("programError")},
                            {"message", jstr(e.message)},
                            {"span", jspan(e.span)}}));
                break;
            case T::NodeStart:   emit_(node_json("start", e));   break;
            case T::NodeDone:    emit_(node_json("done", e));    break;
            case T::NodeSkipped: emit_(node_json("skipped", e)); break;
            case T::NodeError:   emit_(node_json("error", e));   break;
            case T::Log:
                emit_(jobj({{"type", jstr("log")}, {"message", jstr(e.message)}}));
                break;
        }
    }

private:
    std::string node_json(const char* ev, const ExecutionEvent& e) {
        std::vector<JField> f = {
            {"type", jstr("node")},
            {"event", jstr(ev)},
            {"id", jstr(node_id_to_string(e.node_id))},
            {"nodeId", jnum(e.node_id)},
            {"op", jstr(e.message)},     // engine sets message to the node's op for node events
            {"span", jspan(e.span)},
        };
        if (e.type == ExecutionEventType::NodeError) {
            f.push_back({"message", jstr(e.message)});
        }
        return jobj_v(f);
    }

    Emit emit_;
};

} // namespace cardcode::harness
