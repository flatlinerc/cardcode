#include "cardcode/engine.hpp"

#include <string>
#include <utility>

namespace cardcode {
namespace {

struct ExecState {
    RobotHost& robot;
    ExecutionEventSink& events;
    const ExecutionLimits& limits;
    std::uint32_t steps = 0;
    bool errored = false;
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
};

bool exec_node(const Node& n, ExecState& st);

bool exec_children(const Node& n, ExecState& st) {
    for (const auto& c : n.children) {
        if (!exec_node(*c, st)) return false;
    }
    return true;
}

bool exec_node(const Node& n, ExecState& st) {
    if (st.steps >= st.limits.max_total_steps) {
        st.fail(n, "execution exceeded maximum step limit");
        return false;
    }
    ++st.steps;
    st.emit(ExecutionEventType::NodeStart, n, n.op);

    bool ok = true;
    switch (n.kind) {
        case NodeKind::Do:
            ok = exec_children(n, st);
            break;
        case NodeKind::Repeat: {
            std::int64_t count = n.nums.empty() ? 0 : n.nums[0];
            if (count < 0) {
                st.fail(n, "repeat count must not be negative");
                return false;
            }
            if (static_cast<std::uint64_t>(count) > st.limits.max_repeat_count) {
                st.fail(n, "repeat count exceeds limit");
                return false;
            }
            for (std::int64_t i = 0; i < count && ok; ++i) {
                ok = exec_children(n, st);
            }
            break;
        }
        case NodeKind::Drive:
            st.robot.drive_forward(static_cast<int>(n.nums[0]), static_cast<int>(n.nums[1]));
            break;
        case NodeKind::Backward:
            st.robot.drive_backward(static_cast<int>(n.nums[0]), static_cast<int>(n.nums[1]));
            break;
        case NodeKind::TurnLeft:
            st.robot.turn_left(static_cast<int>(n.nums[0]));
            break;
        case NodeKind::TurnRight:
            st.robot.turn_right(static_cast<int>(n.nums[0]));
            break;
        case NodeKind::Stop:
            st.robot.stop();
            break;
        case NodeKind::Wait:
            st.robot.wait_ms(static_cast<int>(n.nums[0]));
            break;
    }

    if (!ok || st.errored) return false;
    st.emit(ExecutionEventType::NodeDone, n, n.op);
    return true;
}

} // namespace

ExecutionResult execute(const Node& root, RobotHost& robot,
                        ExecutionEventSink& events, const ExecutionLimits& limits) {
    ExecState st{robot, events, limits};
    st.emit_program(ExecutionEventType::ProgramStart);

    bool ok = exec_node(root, st);
    if (!ok || st.errored) {
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

    ExecutionResult exec = execute(*compiled.root, robot, events, options.limits);
    result.success = exec.success;
    if (exec.error) result.diagnostics.push_back(*exec.error);
    return result;
}

} // namespace cardcode
