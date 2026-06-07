#include "cardcode/engine.hpp"

#include <string>
#include <unordered_map>
#include <utility>

#include "cardcode/value.hpp"

namespace cardcode {
namespace {

// A variable scope: its own bindings plus a pointer to the global frame. The
// global frame has global == nullptr. There is no closure capture (no lambdas),
// so a function call frame only ever resolves to its own locals then globals.
struct Frame {
    std::unordered_map<std::string, Value> vars;
    Frame* global = nullptr;

    Value* find(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        if (global) {
            auto g = global->vars.find(name);
            if (g != global->vars.end()) return &g->second;
        }
        return nullptr;
    }
};

struct ExecState {
    RobotHost& robot;
    ExecutionEventSink& events;
    const ExecutionLimits& limits;
    const std::atomic<bool>* cancel;
    std::unordered_map<std::string, const Node*> functions{};
    Frame global{};
    std::uint32_t steps = 0;
    std::uint32_t call_depth = 0;
    bool errored = false;
    bool cancelled = false;
    Diagnostic error{};

    void emit(ExecutionEventType type, const Node& n, std::string msg = {}) {
        events.on_event({type, n.id, n.span, std::move(msg)});
    }
    void emit_program(ExecutionEventType type, std::string msg = {}) {
        events.on_event({type, 0, SourceSpan{}, std::move(msg)});
    }
    void fail(const Node& n, std::string msg) {
        errored = true;
        error = Diagnostic{DiagnosticSeverity::Error, n.span, msg};
        emit(ExecutionEventType::NodeError, n, std::move(msg));
    }
    bool stopping() const { return errored || cancelled; }
};

Value eval(const Node& n, ExecState& st, Frame& scope);

void eval_children(const Node& n, ExecState& st, Frame& scope) {
    for (const auto& c : n.children) {
        eval(*c, st, scope);
        if (st.stopping()) return;
    }
}

bool eval_int(const Node& argn, const Node& ctx, const char* what,
              ExecState& st, Frame& scope, std::int64_t& out) {
    Value v = eval(argn, st, scope);
    if (st.stopping()) return false;
    if (v.kind != ValueKind::Integer) {
        st.fail(ctx, std::string("expected an integer for ") + what);
        return false;
    }
    out = v.integer;
    return true;
}

Value call_function(const Node& call, ExecState& st, Frame& scope) {
    auto it = st.functions.find(call.op);
    if (it == st.functions.end()) {
        st.fail(call, "undefined function '" + call.op + "'");
        return Value::none();
    }
    const Node& fn = *it->second;

    std::vector<Value> argv;
    argv.reserve(call.args.size());
    for (const auto& a : call.args) {
        Value v = eval(*a, st, scope);
        if (st.stopping()) return Value::none();
        argv.push_back(v);
    }

    if (st.call_depth >= st.limits.max_call_depth) {
        st.fail(call, "maximum call depth exceeded");
        return Value::none();
    }

    Frame frame;
    frame.global = &st.global;
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        frame.vars[fn.params[i]] = i < argv.size() ? argv[i] : Value::none();
    }

    ++st.call_depth;
    Value ret = Value::none();
    for (const auto& body : fn.children) {
        ret = eval(*body, st, frame);
        if (st.stopping()) break;
    }
    --st.call_depth;
    return ret;
}

Value eval_inner(const Node& n, ExecState& st, Frame& scope) {
    switch (n.kind) {
        case NodeKind::Do: {
            Value last = Value::none();
            for (const auto& c : n.children) {
                last = eval(*c, st, scope);
                if (st.stopping()) return Value::none();
            }
            return last;
        }

        case NodeKind::Repeat: {
            std::int64_t count = 0;
            if (!eval_int(*n.args[0], n, "repeat count", st, scope, count)) return Value::none();
            if (count < 0) { st.fail(n, "repeat count must not be negative"); return Value::none(); }
            if (static_cast<std::uint64_t>(count) > st.limits.max_repeat_count) {
                st.fail(n, "repeat count exceeds limit");
                return Value::none();
            }
            for (std::int64_t i = 0; i < count; ++i) {
                eval_children(n, st, scope);
                if (st.stopping()) break;
            }
            return Value::none();
        }

        case NodeKind::When: {
            Value cond = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            Value last = Value::none();
            if (cond.truthy()) {
                for (const auto& c : n.children) {
                    last = eval(*c, st, scope);
                    if (st.stopping()) return Value::none();
                }
            }
            return last;
        }

        case NodeKind::If: {
            Value cond = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            return eval(cond.truthy() ? *n.args[1] : *n.args[2], st, scope);
        }

        case NodeKind::While: {
            for (;;) {
                if (st.steps >= st.limits.max_total_steps) {
                    st.fail(n, "execution exceeded maximum step limit");
                    break;
                }
                ++st.steps; // bound the loop even with a non-highlightable body
                Value cond = eval(*n.args[0], st, scope);
                if (st.stopping()) break;
                if (!cond.truthy()) break;
                eval_children(n, st, scope);
                if (st.stopping()) break;
            }
            return Value::none();
        }

        case NodeKind::DefineFunc:
            return Value::none(); // registered during the pre-scan; no-op at runtime

        case NodeKind::DefineVar: {
            Value v = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            scope.vars[n.op] = v;
            return v;
        }

        case NodeKind::SetVar: {
            Value v = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            Value* slot = scope.find(n.op);
            if (!slot) { st.fail(n, "set! of undefined variable '" + n.op + "'"); return Value::none(); }
            *slot = v;
            return v;
        }

        case NodeKind::VarRef: {
            Value* slot = scope.find(n.op);
            if (!slot) { st.fail(n, "undefined variable '" + n.op + "'"); return Value::none(); }
            return *slot;
        }

        case NodeKind::Call:
            return call_function(n, st, scope);

        case NodeKind::Drive:
        case NodeKind::Backward: {
            std::int64_t speed = 0, ms = 0;
            if (!eval_int(*n.args[0], n, "speed", st, scope, speed)) return Value::none();
            if (!eval_int(*n.args[1], n, "duration_ms", st, scope, ms)) return Value::none();
            if (speed < 0 || speed > 100) { st.fail(n, "speed must be between 0 and 100"); return Value::none(); }
            if (ms < 0 || ms > 10000) { st.fail(n, "duration_ms must be between 0 and 10000"); return Value::none(); }
            if (n.kind == NodeKind::Drive) st.robot.drive_forward(static_cast<int>(speed), static_cast<int>(ms));
            else st.robot.drive_backward(static_cast<int>(speed), static_cast<int>(ms));
            return Value::none();
        }

        case NodeKind::TurnLeft:
        case NodeKind::TurnRight: {
            std::int64_t deg = 0;
            if (!eval_int(*n.args[0], n, "degrees", st, scope, deg)) return Value::none();
            if (deg < 0 || deg > 3600) { st.fail(n, "degrees must be between 0 and 3600"); return Value::none(); }
            if (n.kind == NodeKind::TurnLeft) st.robot.turn_left(static_cast<int>(deg));
            else st.robot.turn_right(static_cast<int>(deg));
            return Value::none();
        }

        case NodeKind::Stop:
            st.robot.stop();
            return Value::none();

        case NodeKind::Wait: {
            std::int64_t ms = 0;
            if (!eval_int(*n.args[0], n, "duration_ms", st, scope, ms)) return Value::none();
            if (ms < 0 || ms > 10000) { st.fail(n, "duration_ms must be between 0 and 10000"); return Value::none(); }
            st.robot.wait_ms(static_cast<int>(ms));
            return Value::none();
        }

        case NodeKind::Light: {
            Value v = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            if (v.kind != ValueKind::Color) { st.fail(n, "light expects a color"); return Value::none(); }
            st.robot.set_light(v.color);
            return Value::none();
        }

        case NodeKind::Beep:
            st.robot.beep();
            return Value::none();

        case NodeKind::DistanceCm: return Value::of_int(st.robot.distance_cm());
        case NodeKind::Button:     return Value::of_bool(st.robot.button_pressed());
        case NodeKind::LineLeft:   return Value::of_bool(st.robot.line_left());
        case NodeKind::LineRight:  return Value::of_bool(st.robot.line_right());

        case NodeKind::Compare: {
            Value a = eval(*n.args[0], st, scope);
            if (st.stopping()) return Value::none();
            Value b = eval(*n.args[1], st, scope);
            if (st.stopping()) return Value::none();
            if (a.kind != ValueKind::Integer || b.kind != ValueKind::Integer) {
                st.fail(n, "comparison operands must be integers");
                return Value::none();
            }
            std::int64_t x = a.integer, y = b.integer;
            bool r = false;
            if (n.op == "<")       r = x < y;
            else if (n.op == ">")  r = x > y;
            else if (n.op == "=")  r = x == y;
            else if (n.op == "<=") r = x <= y;
            else if (n.op == ">=") r = x >= y;
            return Value::of_bool(r);
        }

        case NodeKind::BooleanOp: {
            if (n.op == "not") {
                Value a = eval(*n.args[0], st, scope);
                if (st.stopping()) return Value::none();
                return Value::of_bool(!a.truthy());
            }
            const bool is_and = (n.op == "and");
            for (const auto& a : n.args) {
                Value v = eval(*a, st, scope);
                if (st.stopping()) return Value::none();
                if (is_and && !v.truthy()) return Value::of_bool(false);
                if (!is_and && v.truthy()) return Value::of_bool(true);
            }
            return Value::of_bool(is_and);
        }

        case NodeKind::Arithmetic: {
            std::int64_t acc = 0;
            for (std::size_t i = 0; i < n.args.size(); ++i) {
                Value v = eval(*n.args[i], st, scope);
                if (st.stopping()) return Value::none();
                if (v.kind != ValueKind::Integer) { st.fail(n, "arithmetic operands must be integers"); return Value::none(); }
                if (i == 0) acc = v.integer;
                else if (n.op == "+") acc += v.integer;
                else if (n.op == "-") acc -= v.integer;
                else if (n.op == "*") acc *= v.integer;
                else { // "/"
                    if (v.integer == 0) { st.fail(n, "division by zero"); return Value::none(); }
                    acc /= v.integer;
                }
            }
            return Value::of_int(acc);
        }

        case NodeKind::IntegerLiteral: return Value::of_int(n.nums.empty() ? 0 : n.nums[0]);
        case NodeKind::BooleanLiteral: return Value::of_bool(!n.nums.empty() && n.nums[0] != 0);
        case NodeKind::ColorLiteral:   return Value::of_color(static_cast<Color>(n.nums.empty() ? 0 : n.nums[0]));
    }
    return Value::none();
}

Value eval(const Node& n, ExecState& st, Frame& scope) {
    if (st.stopping()) return Value::none();

    if (st.cancel != nullptr && st.cancel->load()) {
        st.cancelled = true;
        return Value::none();
    }

    const bool highlight = is_highlightable(n.kind);
    if (highlight) {
        if (st.steps >= st.limits.max_total_steps) {
            st.fail(n, "execution exceeded maximum step limit");
            return Value::none();
        }
        ++st.steps;
        st.emit(ExecutionEventType::NodeStart, n, n.op);
    }

    Value v = eval_inner(n, st, scope);

    if (st.stopping()) return v;
    if (highlight) st.emit(ExecutionEventType::NodeDone, n, n.op);
    return v;
}

void collect_functions(const Node& root, std::unordered_map<std::string, const Node*>& out) {
    if (root.kind == NodeKind::DefineFunc) out[root.op] = &root;
    for (const auto& c : root.children) {
        if (c->kind == NodeKind::DefineFunc) out[c->op] = c.get();
    }
}

} // namespace

ExecutionResult execute(const Node& root, RobotHost& robot,
                        ExecutionEventSink& events, const ExecutionLimits& limits,
                        const std::atomic<bool>* cancel) {
    ExecState st{robot, events, limits, cancel};
    collect_functions(root, st.functions);

    st.emit_program(ExecutionEventType::ProgramStart);
    eval(root, st, st.global);

    if (st.cancelled) {
        robot.stop();
        st.emit_program(ExecutionEventType::ProgramError, "execution cancelled");
        return ExecutionResult{false, Diagnostic{DiagnosticSeverity::Error, root.span, "execution cancelled"}};
    }
    if (st.errored) {
        robot.stop();
        st.emit_program(ExecutionEventType::ProgramError, st.error.message);
        return ExecutionResult{false, st.error};
    }

    st.emit_program(ExecutionEventType::ProgramDone);
    return ExecutionResult{true, std::nullopt};
}

RunResult compile_and_run(std::string_view source, RobotHost& robot,
                          ExecutionEventSink& events, const RunOptions& options) {
    CompileResult compiled = compile(source);

    RunResult result;
    result.diagnostics = compiled.diagnostics;
    if (!compiled.ok()) {
        result.success = false;
        return result;
    }

    ExecutionResult exec = execute(*compiled.root, robot, events, options.limits, options.cancel);
    result.success = exec.success;
    if (exec.error) result.diagnostics.push_back(*exec.error);
    return result;
}

} // namespace cardcode
