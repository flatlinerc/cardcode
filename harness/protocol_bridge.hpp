#pragma once

// Parses incoming control messages (docs/integration/protocol.md) and drives the
// engine, emitting protocol messages through a thread-safe emit callback.
//
// `async` (default) runs each program on a worker thread so cancel/setSensor keep
// flowing during a run. Tests construct it with async=false for deterministic,
// inline execution.

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "cardcode/engine.hpp"

#include "forwarding_host.hpp"
#include "json.hpp"
#include "json_event_sink.hpp"

namespace cardcode::harness {

struct BridgeOptions {
    bool async = true;     // run programs on a worker thread
    bool realtime = false; // sleep for command durations
};

class ProtocolBridge {
public:
    using Emit = std::function<void(const std::string&)>;

    explicit ProtocolBridge(Emit emit, BridgeOptions opts = {})
        : emit_(std::move(emit)), opts_(opts), robot_(emit_, opts.realtime) {}

    // Join any in-flight run so events flush before the transport tears down.
    ~ProtocolBridge() {
        if (worker_.joinable()) worker_.join();
    }

    // Handle one JSON control message.
    void handle(const std::string& message) {
        auto parsed = json_parse(message);
        if (!parsed || !parsed->is_obj()) { error("malformed message"); return; }
        const JsonValue& msg = *parsed;

        const JsonValue* type = msg.find("type");
        if (!type || type->type != JsonValue::Type::Str) { error("missing 'type'"); return; }
        const std::string t = type->str;

        if (t == "compile")        do_compile(msg);
        else if (t == "run")       do_run(msg);
        else if (t == "cancel")    cancel_.store(true);
        else if (t == "setSensor") do_set_sensor(msg);
        else if (t == "reset")     robot_.reset();
        else                       error("unknown message type '" + t + "'");
    }

private:
    void do_compile(const JsonValue& msg) {
        const JsonValue* src = msg.find("source");
        if (!src) { error("compile: missing 'source'"); return; }
        CompileResult compiled = compile(src->as_str());
        emit_diagnostics(compiled);
    }

    void do_run(const JsonValue& msg) {
        const JsonValue* src = msg.find("source");
        if (!src) { error("run: missing 'source'"); return; }
        if (running_.exchange(true)) { error("a program is already running"); return; }
        if (worker_.joinable()) worker_.join(); // reap the previous finished run

        ExecutionLimits limits = parse_limits(msg.find("limits"));
        cancel_.store(false);
        std::string source = src->as_str();

        auto job = [this, source = std::move(source), limits]() mutable {
            JsonEventSink sink(emit_);
            CompileResult compiled = compile(source);
            emit_diagnostics(compiled);
            if (compiled.ok()) {
                execute(*compiled.root, robot_, sink, limits, &cancel_);
            }
            running_.store(false);
        };

        if (opts_.async) worker_ = std::thread(std::move(job));
        else             job();
    }

    void do_set_sensor(const JsonValue& msg) {
        const JsonValue* s = msg.find("sensor");
        const JsonValue* v = msg.find("value");
        if (!s || !v) { error("setSensor: missing 'sensor' or 'value'"); return; }
        const std::string name = s->as_str();
        if (name == "distance-cm")     robot_.set_distance(static_cast<int>(v->as_int()));
        else if (name == "button")     robot_.set_button(v->as_bool());
        else if (name == "line-left")  robot_.set_line_left(v->as_bool());
        else if (name == "line-right") robot_.set_line_right(v->as_bool());
        else error("setSensor: unknown sensor '" + name + "'");
    }

    static ExecutionLimits parse_limits(const JsonValue* limits) {
        ExecutionLimits lim;
        if (limits && limits->is_obj()) {
            if (auto* x = limits->find("maxTotalSteps"))  lim.max_total_steps  = static_cast<std::uint32_t>(x->as_int());
            if (auto* x = limits->find("maxRepeatCount")) lim.max_repeat_count = static_cast<std::uint32_t>(x->as_int());
            if (auto* x = limits->find("maxCallDepth"))   lim.max_call_depth   = static_cast<std::uint32_t>(x->as_int());
        }
        return lim;
    }

    void emit_diagnostics(const CompileResult& compiled) {
        std::vector<std::string> items;
        for (const auto& d : compiled.diagnostics) {
            items.push_back(jobj({
                {"severity", jstr(d.severity == DiagnosticSeverity::Error ? "error" : "warning")},
                {"message", jstr(d.message)},
                {"span", jspan(d.span)},
            }));
        }
        emit_(jobj({{"type", jstr("diagnostics")},
                    {"ok", jbool(compiled.ok())},
                    {"diagnostics", jarr(items)}}));
    }

    void error(const std::string& message) {
        emit_(jobj({{"type", jstr("error")}, {"message", jstr(message)}}));
    }

    Emit emit_;
    BridgeOptions opts_;
    ForwardingRobotHost robot_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace cardcode::harness
