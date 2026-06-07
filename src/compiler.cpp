#include "cardcode/compiler.hpp"

#include <string>

#include "cardcode/parser.hpp"
#include "cardcode/robot_host.hpp"

namespace cardcode {
namespace {

// Fixed v1 safety bounds (see HANDOFF.md "Safety Rules"). Command arguments are
// integer literals, so these are validated at compile time with precise spans.
constexpr std::int64_t kSpeedMin = 0;
constexpr std::int64_t kSpeedMax = 100;
constexpr std::int64_t kDurationMax = 10000;
constexpr std::int64_t kTurnMax = 3600;
constexpr std::int64_t kWaitMax = 10000;
constexpr std::int64_t kRepeatMax = 100;

void err(std::vector<Diagnostic>& diags, const SourceSpan& span, std::string msg) {
    diags.push_back({DiagnosticSeverity::Error, span, std::move(msg)});
}

NodePtr make_node(NodeKind kind, std::string op, const SourceSpan& span) {
    auto n = std::make_unique<Node>();
    n->kind = kind;
    n->op = std::move(op);
    n->span = span;
    return n;
}

bool color_from_name(const std::string& name, Color& out) {
    if (name == "off")    { out = Color::Off;    return true; }
    if (name == "red")    { out = Color::Red;    return true; }
    if (name == "green")  { out = Color::Green;  return true; }
    if (name == "blue")   { out = Color::Blue;   return true; }
    if (name == "yellow") { out = Color::Yellow; return true; }
    if (name == "white")  { out = Color::White;  return true; }
    return false;
}

NodePtr compile_stmt(const RawExpr& raw, std::vector<Diagnostic>& diags);
NodePtr compile_value(const RawExpr& raw, std::vector<Diagnostic>& diags);

bool expect_int(const RawExpr& raw, std::int64_t& out,
                std::vector<Diagnostic>& diags, const char* what) {
    if (raw.kind != RawKind::Integer) {
        err(diags, raw.span, std::string("expected integer for ") + what);
        return false;
    }
    out = raw.integer;
    return true;
}

bool is_comparison(const std::string& op) {
    return op == "<" || op == ">" || op == "=" || op == "<=" || op == ">=";
}
bool is_boolean_op(const std::string& op) {
    return op == "and" || op == "or" || op == "not";
}
bool is_arithmetic(const std::string& op) {
    return op == "+" || op == "-" || op == "*" || op == "/";
}
bool is_sensor(const std::string& op) {
    return op == "distance-cm" || op == "button" || op == "line-left" || op == "line-right";
}
bool is_command(const std::string& op) {
    return op == "drive" || op == "backward" || op == "turn-left" || op == "turn-right" ||
           op == "stop" || op == "wait" || op == "light" || op == "beep";
}

NodePtr compile_sensor(const RawExpr& raw, const std::string& op,
                       std::size_t argc, std::vector<Diagnostic>& diags) {
    if (argc != 0) {
        err(diags, raw.span, "'" + op + "' takes no arguments");
        return nullptr;
    }
    NodeKind kind = op == "distance-cm" ? NodeKind::DistanceCm
                  : op == "button"      ? NodeKind::Button
                  : op == "line-left"   ? NodeKind::LineLeft
                                        : NodeKind::LineRight;
    return make_node(kind, op, raw.span);
}

// Compile an expression that appears in a value position (a condition or an
// operator operand).
NodePtr compile_value(const RawExpr& raw, std::vector<Diagnostic>& diags) {
    switch (raw.kind) {
        case RawKind::Integer: {
            auto n = make_node(NodeKind::IntegerLiteral, "", raw.span);
            n->nums.push_back(raw.integer);
            return n;
        }
        case RawKind::Symbol: {
            if (raw.symbol == "true" || raw.symbol == "false") {
                auto n = make_node(NodeKind::BooleanLiteral, raw.symbol, raw.span);
                n->nums.push_back(raw.symbol == "true" ? 1 : 0);
                return n;
            }
            err(diags, raw.span, "bare symbol '" + raw.symbol + "' is not a valid value");
            return nullptr;
        }
        case RawKind::List:
            break;
    }

    if (raw.list.empty()) {
        err(diags, raw.span, "empty list is not a valid expression");
        return nullptr;
    }
    if (raw.list[0].kind != RawKind::Symbol) {
        err(diags, raw.list[0].span, "expected an operation name");
        return nullptr;
    }

    const std::string& op = raw.list[0].symbol;
    const std::size_t argc = raw.list.size() - 1;
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    if (is_sensor(op)) {
        return compile_sensor(raw, op, argc, diags);
    }

    if (is_comparison(op)) {
        if (argc != 2) {
            err(diags, raw.span, "'" + op + "' expects 2 arguments");
            return nullptr;
        }
        auto n = make_node(NodeKind::Compare, op, raw.span);
        for (std::size_t i = 0; i < 2; ++i) {
            if (auto a = compile_value(arg(i), diags)) n->args.push_back(std::move(a));
        }
        return n->args.size() == 2 ? std::move(n) : nullptr;
    }

    if (is_boolean_op(op)) {
        if (op == "not") {
            if (argc != 1) { err(diags, raw.span, "'not' expects 1 argument"); return nullptr; }
        } else if (argc < 2) {
            err(diags, raw.span, "'" + op + "' expects at least 2 arguments");
            return nullptr;
        }
        auto n = make_node(NodeKind::BooleanOp, op, raw.span);
        for (std::size_t i = 0; i < argc; ++i) {
            if (auto a = compile_value(arg(i), diags)) n->args.push_back(std::move(a));
        }
        return n->args.size() == argc ? std::move(n) : nullptr;
    }

    if (is_arithmetic(op)) {
        if ((op == "-" || op == "/") && argc != 2) {
            err(diags, raw.span, "'" + op + "' expects 2 arguments");
            return nullptr;
        }
        if ((op == "+" || op == "*") && argc < 2) {
            err(diags, raw.span, "'" + op + "' expects at least 2 arguments");
            return nullptr;
        }
        auto n = make_node(NodeKind::Arithmetic, op, raw.span);
        for (std::size_t i = 0; i < argc; ++i) {
            if (auto a = compile_value(arg(i), diags)) n->args.push_back(std::move(a));
        }
        return n->args.size() == argc ? std::move(n) : nullptr;
    }

    err(diags, raw.list[0].span, "'" + op + "' is not a value-producing expression");
    return nullptr;
}

NodePtr compile_command_list(const RawExpr& raw, const std::string& op,
                             std::size_t argc, std::vector<Diagnostic>& diags) {
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    if (op == "drive" || op == "backward") {
        NodeKind kind = (op == "drive") ? NodeKind::Drive : NodeKind::Backward;
        if (argc != 2) {
            err(diags, raw.span, "'" + op + "' expects 2 arguments: speed and duration_ms");
            return nullptr;
        }
        auto n = make_node(kind, op, raw.span);
        std::int64_t speed = 0, ms = 0;
        bool ok = expect_int(arg(0), speed, diags, "speed");
        ok = expect_int(arg(1), ms, diags, "duration_ms") && ok;
        if (ok) {
            if (speed < kSpeedMin || speed > kSpeedMax) {
                err(diags, arg(0).span, "speed must be between 0 and 100");
            }
            if (ms < 0 || ms > kDurationMax) {
                err(diags, arg(1).span,
                    "duration_ms must be between 0 and " + std::to_string(kDurationMax));
            }
        }
        n->nums = {speed, ms};
        return n;
    }

    if (op == "turn-left" || op == "turn-right") {
        NodeKind kind = (op == "turn-left") ? NodeKind::TurnLeft : NodeKind::TurnRight;
        if (argc != 1) { err(diags, raw.span, "'" + op + "' expects 1 argument: degrees"); return nullptr; }
        auto n = make_node(kind, op, raw.span);
        std::int64_t deg = 0;
        if (expect_int(arg(0), deg, diags, "degrees")) {
            if (deg < 0 || deg > kTurnMax) {
                err(diags, arg(0).span, "degrees must be between 0 and " + std::to_string(kTurnMax));
            }
        }
        n->nums.push_back(deg);
        return n;
    }

    if (op == "stop") {
        if (argc != 0) { err(diags, raw.span, "'stop' takes no arguments"); return nullptr; }
        return make_node(NodeKind::Stop, op, raw.span);
    }

    if (op == "wait") {
        if (argc != 1) { err(diags, raw.span, "'wait' expects 1 argument: duration_ms"); return nullptr; }
        auto n = make_node(NodeKind::Wait, op, raw.span);
        std::int64_t ms = 0;
        if (expect_int(arg(0), ms, diags, "duration_ms")) {
            if (ms < 0 || ms > kWaitMax) {
                err(diags, arg(0).span, "duration_ms must be between 0 and " + std::to_string(kWaitMax));
            }
        }
        n->nums.push_back(ms);
        return n;
    }

    if (op == "light") {
        if (argc != 1) { err(diags, raw.span, "'light' expects 1 argument: color"); return nullptr; }
        if (arg(0).kind != RawKind::Symbol) {
            err(diags, arg(0).span, "'light' expects a color name");
            return nullptr;
        }
        Color color = Color::Off;
        if (!color_from_name(arg(0).symbol, color)) {
            err(diags, arg(0).span, "unknown color '" + arg(0).symbol +
                "' (expected off, red, green, blue, yellow, or white)");
            return nullptr;
        }
        auto n = make_node(NodeKind::Light, op, raw.span);
        n->nums.push_back(static_cast<std::int64_t>(color));
        return n;
    }

    if (op == "beep") {
        if (argc != 0) { err(diags, raw.span, "'beep' takes no arguments"); return nullptr; }
        return make_node(NodeKind::Beep, op, raw.span);
    }

    return nullptr; // not a command
}

NodePtr compile_stmt_list(const RawExpr& raw, std::vector<Diagnostic>& diags) {
    const std::string& op = raw.list[0].symbol;
    const std::size_t argc = raw.list.size() - 1;
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    if (op == "do") {
        auto n = make_node(NodeKind::Do, op, raw.span);
        for (std::size_t i = 0; i < argc; ++i) {
            if (auto child = compile_stmt(arg(i), diags)) n->children.push_back(std::move(child));
        }
        return n;
    }

    if (op == "repeat") {
        if (argc < 1) { err(diags, raw.span, "'repeat' expects a count and a body"); return nullptr; }
        auto n = make_node(NodeKind::Repeat, op, raw.span);
        std::int64_t count = 0;
        if (expect_int(arg(0), count, diags, "repeat count")) {
            if (count < 0) {
                err(diags, arg(0).span, "repeat count must not be negative");
            } else if (count > kRepeatMax) {
                err(diags, arg(0).span,
                    "repeat count must be between 0 and " + std::to_string(kRepeatMax));
            }
        }
        n->nums.push_back(count);
        for (std::size_t i = 1; i < argc; ++i) {
            if (auto child = compile_stmt(arg(i), diags)) n->children.push_back(std::move(child));
        }
        return n;
    }

    if (op == "when") {
        if (argc < 1) { err(diags, raw.span, "'when' expects a condition and a body"); return nullptr; }
        auto n = make_node(NodeKind::When, op, raw.span);
        if (auto cond = compile_value(arg(0), diags)) n->args.push_back(std::move(cond));
        for (std::size_t i = 1; i < argc; ++i) {
            if (auto child = compile_stmt(arg(i), diags)) n->children.push_back(std::move(child));
        }
        return n->args.size() == 1 ? std::move(n) : nullptr;
    }

    if (op == "if") {
        if (argc != 3) {
            err(diags, raw.span, "'if' expects 3 arguments: condition, then, else");
            return nullptr;
        }
        auto n = make_node(NodeKind::If, op, raw.span);
        if (auto cond = compile_value(arg(0), diags)) n->args.push_back(std::move(cond));
        if (auto then_s = compile_stmt(arg(1), diags)) n->args.push_back(std::move(then_s));
        if (auto else_s = compile_stmt(arg(2), diags)) n->args.push_back(std::move(else_s));
        return n->args.size() == 3 ? std::move(n) : nullptr;
    }

    if (is_command(op)) {
        return compile_command_list(raw, op, argc, diags);
    }

    // A value-producing form used where a statement was expected, or unknown op.
    if (is_sensor(op) || is_comparison(op) || is_boolean_op(op) || is_arithmetic(op)) {
        err(diags, raw.list[0].span, "'" + op + "' produces a value and cannot be used as a statement");
        return nullptr;
    }

    err(diags, raw.list[0].span, "unknown operation '" + op + "'");
    return nullptr;
}

NodePtr compile_stmt(const RawExpr& raw, std::vector<Diagnostic>& diags) {
    switch (raw.kind) {
        case RawKind::List:
            if (raw.list.empty()) {
                err(diags, raw.span, "empty list is not a valid expression");
                return nullptr;
            }
            if (raw.list[0].kind != RawKind::Symbol) {
                err(diags, raw.list[0].span, "expected an operation name");
                return nullptr;
            }
            return compile_stmt_list(raw, diags);
        case RawKind::Symbol:
            err(diags, raw.span, "bare symbol '" + raw.symbol + "' is not a valid statement");
            return nullptr;
        case RawKind::Integer:
            err(diags, raw.span, "bare integer is not a valid statement");
            return nullptr;
    }
    return nullptr;
}

void assign_preorder(Node& node, NodeId& next) {
    node.id = next++;
    for (auto& a : node.args) assign_preorder(*a, next);
    for (auto& child : node.children) assign_preorder(*child, next);
}

} // namespace

void assign_node_ids(Node& root) {
    NodeId next = 0;
    assign_preorder(root, next);
}

CompileResult compile(std::string_view source) {
    CompileResult result;

    ParseResult parsed = parse(source);
    result.diagnostics = parsed.diagnostics;

    if (parsed.exprs.empty()) {
        if (!has_errors(result.diagnostics)) {
            err(result.diagnostics, SourceSpan{}, "program is empty");
        }
        return result;
    }

    NodePtr root;
    if (parsed.exprs.size() == 1) {
        root = compile_stmt(parsed.exprs[0], result.diagnostics);
    } else {
        const SourceSpan& first = parsed.exprs.front().span;
        const SourceSpan& last = parsed.exprs.back().span;
        SourceSpan span{first.start_offset, last.end_offset,
                        first.start_line, first.start_column,
                        last.end_line, last.end_column};
        auto do_node = make_node(NodeKind::Do, "do", span);
        for (const auto& e : parsed.exprs) {
            if (auto child = compile_stmt(e, result.diagnostics)) {
                do_node->children.push_back(std::move(child));
            }
        }
        root = std::move(do_node);
    }

    if (root) {
        assign_node_ids(*root);
        result.root = std::move(root);
    }
    return result;
}

} // namespace cardcode
