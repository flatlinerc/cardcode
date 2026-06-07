#include "cardcode/engine.hpp"

#include <string>
#include <utility>

#include "cardcode/value.hpp"

namespace cardcode {
namespace {

struct ExecState {
    RobotHost& robot;
    ExecutionEventSink& events;
    const ExecutionLimits& limits;
    const std::atomic<bool>* cancel;
    std::uint32_t steps = 0;
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

Value eval(const Node& n, ExecState& st);

void eval_children(const Node& n, ExecState& st) {
    for (const auto& c : n.children) {
        eval(*c, st);
        if (st.stopping()) return;
    }
}

bool require_int(const Value& v, const Node& ctx, const char* what, ExecState& st) {
    if (v.kind != ValueKind::Integer) {
        st.fail(ctx, std::string("expected an integer ") + what);
        return false;
    }
    return true;
}

Value eval_inner(const Node& n, ExecState& st) {
    switch (n.kind) {
        case NodeKind::Do:
            eval_children(n, st);
            return Value::none();

        case NodeKind::Repeat: {
            std::int64_t count = n.nums.empty() ? 0 : n.nums[0];
            if (count < 0) { st.fail(n, "repeat count must not be negative"); return Value::none(); }
            if (static_cast<std::uint64_t>(count) > st.limits.max_repeat_count) {
                st.fail(n, "repeat count exceeds limit");
                return Value::none();
            }
            for (std::int64_t i = 0; i < count; ++i) {
                eval_children(n, st);
                if (st.stopping()) break;
            }
            return Value::none();
        }

        case NodeKind::When: {
            Value cond = eval(*n.args[0], st);
            if (st.stopping()) return Value::none();
            if (cond.truthy()) {
                eval_children(n, st);
            }
            return Value::none();
        }

        case NodeKind::If: {
            Value cond = eval(*n.args[0], st);
            if (st.stopping()) return Value::none();
            eval(cond.truthy() ? *n.args[1] : *n.args[2], st);
            return Value::none();
        }

        case NodeKind::Drive:
            st.robot.drive_forward(static_cast<int>(n.nums[0]), static_cast<int>(n.nums[1]));
            return Value::none();
        case NodeKind::Backward:
            st.robot.drive_backward(static_cast<int>(n.nums[0]), static_cast<int>(n.nums[1]));
            return Value::none();
        case NodeKind::TurnLeft:
            st.robot.turn_left(static_cast<int>(n.nums[0]));
            return Value::none();
        case NodeKind::TurnRight:
            st.robot.turn_right(static_cast<int>(n.nums[0]));
            return Value::none();
        case NodeKind::Stop:
            st.robot.stop();
            return Value::none();
        case NodeKind::Wait:
            st.robot.wait_ms(static_cast<int>(n.nums[0]));
            return Value::none();
        case NodeKind::Light:
            st.robot.set_light(static_cast<Color>(n.nums[0]));
            return Value::none();
        case NodeKind::Beep:
            st.robot.beep();
            return Value::none();

        case NodeKind::DistanceCm: return Value::of_int(st.robot.distance_cm());
        case NodeKind::Button:     return Value::of_bool(st.robot.button_pressed());
        case NodeKind::LineLeft:   return Value::of_bool(st.robot.line_left());
        case NodeKind::LineRight:  return Value::of_bool(st.robot.line_right());

        case NodeKind::Compare: {
            Value a = eval(*n.args[0], st);
            if (st.stopping()) return Value::none();
            Value b = eval(*n.args[1], st);
            if (st.stopping()) return Value::none();
            if (!require_int(a, n, "on the left of a comparison", st)) return Value::none();
            if (!require_int(b, n, "on the right of a comparison", st)) return Value::none();
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
                Value a = eval(*n.args[0], st);
                if (st.stopping()) return Value::none();
                return Value::of_bool(!a.truthy());
            }
            const bool is_and = (n.op == "and");
            for (const auto& a : n.args) {
                Value v = eval(*a, st);
                if (st.stopping()) return Value::none();
                if (is_and && !v.truthy()) return Value::of_bool(false);
                if (!is_and && v.truthy()) return Value::of_bool(true);
            }
            return Value::of_bool(is_and);
        }

        case NodeKind::Arithmetic: {
            std::int64_t acc = 0;
            for (std::size_t i = 0; i < n.args.size(); ++i) {
                Value v = eval(*n.args[i], st);
                if (st.stopping()) return Value::none();
                if (!require_int(v, n, "operand", st)) return Value::none();
                if (i == 0) {
                    acc = v.integer;
                } else if (n.op == "+") {
                    acc += v.integer;
                } else if (n.op == "-") {
                    acc -= v.integer;
                } else if (n.op == "*") {
                    acc *= v.integer;
                } else { // "/"
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

Value eval(const Node& n, ExecState& st) {
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

    Value v = eval_inner(n, st);

    if (st.stopping()) return v;
    if (highlight) st.emit(ExecutionEventType::NodeDone, n, n.op);
    return v;
}

} // namespace

ExecutionResult execute(const Node& root, RobotHost& robot,
                        ExecutionEventSink& events, const ExecutionLimits& limits,
                        const std::atomic<bool>* cancel) {
    ExecState st{robot, events, limits, cancel};
    st.emit_program(ExecutionEventType::ProgramStart);

    eval(root, st);

    if (st.cancelled) {
        robot.stop(); // safety fallback
        st.emit_program(ExecutionEventType::ProgramError, "execution cancelled");
        return ExecutionResult{false, Diagnostic{DiagnosticSeverity::Error, root.span, "execution cancelled"}};
    }
    if (st.errored) {
        robot.stop(); // safety fallback
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
