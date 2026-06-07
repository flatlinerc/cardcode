#include "cardcode/compiler.hpp"

#include <map>
#include <string>

#include "cardcode/parser.hpp"
#include "cardcode/robot_host.hpp"

namespace cardcode {
namespace {

// Fixed v1 safety bounds (see HANDOFF.md "Safety Rules"). Checked at compile time
// when an argument is a literal, and again at runtime for computed arguments.
constexpr std::int64_t kSpeedMin = 0;
constexpr std::int64_t kSpeedMax = 100;
constexpr std::int64_t kDurationMax = 10000;
constexpr std::int64_t kTurnMax = 3600;
constexpr std::int64_t kWaitMax = 10000;
constexpr std::int64_t kRepeatMax = 100;

// Function name -> parameter count, collected before compilation (hoisting).
using ArityTable = std::map<std::string, std::size_t>;

struct Ctx {
    const ArityTable& arity;
    std::vector<Diagnostic>& diags;
};

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
bool is_control(const std::string& op) {
    return op == "do" || op == "repeat" || op == "when" || op == "if" || op == "while" ||
           op == "define" || op == "set!";
}
bool is_reserved(const std::string& name) {
    Color dummy;
    return name == "true" || name == "false" || color_from_name(name, dummy);
}
// Names a user function may not take (would shadow language built-ins).
bool is_builtin_name(const std::string& op) {
    return is_command(op) || is_control(op) || is_sensor(op) ||
           is_comparison(op) || is_boolean_op(op) || is_arithmetic(op) || is_reserved(op);
}

// CardCode is expression-oriented: every form produces a value (commands and
// control flow yield None). A function/loop/conditional body is a sequence of
// expressions; the last one's value is the result. So there is a single
// compiler for all positions, parameterized only by whether define-func is
// allowed (top level only).
NodePtr compile_expr(const RawExpr& raw, Ctx& ctx, bool top_level);
NodePtr compile_value(const RawExpr& raw, Ctx& ctx); // == compile_expr(raw, ctx, false)
NodePtr compile_form(const RawExpr& raw, Ctx& ctx, bool top_level);

// Build a Call node, validating the function exists and arity matches.
NodePtr compile_call(const RawExpr& raw, const std::string& op, std::size_t argc, Ctx& ctx) {
    auto it = ctx.arity.find(op);
    if (it == ctx.arity.end()) {
        err(ctx.diags, raw.list[0].span, "unknown operation or function '" + op + "'");
        return nullptr;
    }
    if (argc != it->second) {
        err(ctx.diags, raw.span, "function '" + op + "' expects " +
            std::to_string(it->second) + " argument(s) but got " + std::to_string(argc));
        return nullptr;
    }
    auto n = make_node(NodeKind::Call, op, raw.span);
    for (std::size_t i = 0; i < argc; ++i) {
        if (auto a = compile_value(raw.list[i + 1], ctx)) n->args.push_back(std::move(a));
    }
    return n->args.size() == argc ? std::move(n) : nullptr;
}

NodePtr compile_define(const RawExpr& raw, std::size_t argc, Ctx& ctx, bool top_level) {
    if (argc < 1) { err(ctx.diags, raw.span, "'define' expects a name and a value"); return nullptr; }
    const RawExpr& target = raw.list[1];

    // (define (name params...) body...) -> function definition
    if (target.kind == RawKind::List) {
        if (!top_level) {
            err(ctx.diags, raw.span, "functions must be defined at the top level");
            return nullptr;
        }
        if (target.list.empty() || target.list[0].kind != RawKind::Symbol) {
            err(ctx.diags, target.span, "function definition needs a name");
            return nullptr;
        }
        const std::string& name = target.list[0].symbol;
        if (is_builtin_name(name)) {
            err(ctx.diags, target.list[0].span, "cannot redefine built-in '" + name + "'");
            return nullptr;
        }
        auto fn = make_node(NodeKind::DefineFunc, name, raw.span);
        for (std::size_t i = 1; i < target.list.size(); ++i) {
            const RawExpr& p = target.list[i];
            if (p.kind != RawKind::Symbol) {
                err(ctx.diags, p.span, "function parameters must be names");
            } else if (is_reserved(p.symbol)) {
                err(ctx.diags, p.span, "'" + p.symbol + "' is reserved and cannot be a parameter");
            } else {
                fn->params.push_back(p.symbol);
            }
        }
        for (std::size_t i = 2; i < raw.list.size(); ++i) {
            if (auto s = compile_value(raw.list[i], ctx)) fn->children.push_back(std::move(s));
        }
        return fn;
    }

    // (define name value) -> variable binding
    if (target.kind != RawKind::Symbol) {
        err(ctx.diags, target.span, "'define' expects a name");
        return nullptr;
    }
    if (argc != 2) { err(ctx.diags, raw.span, "'define' expects a name and a value"); return nullptr; }
    if (is_reserved(target.symbol)) {
        err(ctx.diags, target.span, "'" + target.symbol + "' is reserved and cannot be a variable name");
        return nullptr;
    }
    auto n = make_node(NodeKind::DefineVar, target.symbol, raw.span);
    if (auto v = compile_value(raw.list[2], ctx)) n->args.push_back(std::move(v));
    return n->args.size() == 1 ? std::move(n) : nullptr;
}

NodePtr compile_command(const RawExpr& raw, const std::string& op, std::size_t argc, Ctx& ctx) {
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    // Range-check a literal argument at compile time (computed args are checked at runtime).
    auto lit_check = [&](std::size_t i, std::int64_t lo, std::int64_t hi, const std::string& msg) {
        if (arg(i).kind == RawKind::Integer &&
            (arg(i).integer < lo || arg(i).integer > hi)) {
            err(ctx.diags, arg(i).span, msg);
        }
    };

    NodeKind kind;
    std::size_t want;
    if (op == "drive" || op == "backward") {
        kind = op == "drive" ? NodeKind::Drive : NodeKind::Backward;
        want = 2;
    } else if (op == "turn-left" || op == "turn-right") {
        kind = op == "turn-left" ? NodeKind::TurnLeft : NodeKind::TurnRight;
        want = 1;
    } else if (op == "wait") { kind = NodeKind::Wait;  want = 1; }
    else if (op == "light")  { kind = NodeKind::Light; want = 1; }
    else if (op == "stop")   { kind = NodeKind::Stop;  want = 0; }
    else                     { kind = NodeKind::Beep;  want = 0; }

    if (argc != want) {
        err(ctx.diags, raw.span,
            "'" + op + "' expects " + std::to_string(want) + " argument(s) but got " + std::to_string(argc));
        return nullptr;
    }

    if (kind == NodeKind::Drive || kind == NodeKind::Backward) {
        lit_check(0, kSpeedMin, kSpeedMax, "speed must be between 0 and 100");
        lit_check(1, 0, kDurationMax, "duration_ms must be between 0 and " + std::to_string(kDurationMax));
    } else if (kind == NodeKind::TurnLeft || kind == NodeKind::TurnRight) {
        lit_check(0, 0, kTurnMax, "degrees must be between 0 and " + std::to_string(kTurnMax));
    } else if (kind == NodeKind::Wait) {
        lit_check(0, 0, kWaitMax, "duration_ms must be between 0 and " + std::to_string(kWaitMax));
    }

    auto n = make_node(kind, op, raw.span);
    for (std::size_t i = 0; i < argc; ++i) {
        if (auto a = compile_value(arg(i), ctx)) n->args.push_back(std::move(a));
    }
    return n->args.size() == argc ? std::move(n) : nullptr;
}

NodePtr compile_operator(const RawExpr& raw, const std::string& op, std::size_t argc, Ctx& ctx) {
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    NodeKind kind;
    if (is_comparison(op)) {
        if (argc != 2) { err(ctx.diags, raw.span, "'" + op + "' expects 2 arguments"); return nullptr; }
        kind = NodeKind::Compare;
    } else if (is_arithmetic(op)) {
        if ((op == "-" || op == "/") && argc != 2) {
            err(ctx.diags, raw.span, "'" + op + "' expects 2 arguments"); return nullptr;
        }
        if ((op == "+" || op == "*") && argc < 2) {
            err(ctx.diags, raw.span, "'" + op + "' expects at least 2 arguments"); return nullptr;
        }
        kind = NodeKind::Arithmetic;
    } else { // boolean
        if (op == "not") {
            if (argc != 1) { err(ctx.diags, raw.span, "'not' expects 1 argument"); return nullptr; }
        } else if (argc < 2) {
            err(ctx.diags, raw.span, "'" + op + "' expects at least 2 arguments"); return nullptr;
        }
        kind = NodeKind::BooleanOp;
    }

    auto n = make_node(kind, op, raw.span);
    for (std::size_t i = 0; i < argc; ++i) {
        if (auto a = compile_value(arg(i), ctx)) n->args.push_back(std::move(a));
    }
    return n->args.size() == argc ? std::move(n) : nullptr;
}

NodePtr compile_form(const RawExpr& raw, Ctx& ctx, bool top_level) {
    const std::string& op = raw.list[0].symbol;
    const std::size_t argc = raw.list.size() - 1;
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    if (op == "do") {
        auto n = make_node(NodeKind::Do, op, raw.span);
        for (std::size_t i = 0; i < argc; ++i) {
            if (auto c = compile_value(arg(i), ctx)) n->children.push_back(std::move(c));
        }
        return n;
    }
    if (op == "repeat") {
        if (argc < 1) { err(ctx.diags, raw.span, "'repeat' expects a count and a body"); return nullptr; }
        if (arg(0).kind == RawKind::Integer) {
            if (arg(0).integer < 0) err(ctx.diags, arg(0).span, "repeat count must not be negative");
            else if (arg(0).integer > kRepeatMax)
                err(ctx.diags, arg(0).span, "repeat count must be between 0 and " + std::to_string(kRepeatMax));
        }
        auto n = make_node(NodeKind::Repeat, op, raw.span);
        if (auto count = compile_value(arg(0), ctx)) n->args.push_back(std::move(count));
        for (std::size_t i = 1; i < argc; ++i) {
            if (auto c = compile_value(arg(i), ctx)) n->children.push_back(std::move(c));
        }
        return n->args.size() == 1 ? std::move(n) : nullptr;
    }
    if (op == "when") {
        if (argc < 1) { err(ctx.diags, raw.span, "'when' expects a condition and a body"); return nullptr; }
        auto n = make_node(NodeKind::When, op, raw.span);
        if (auto cond = compile_value(arg(0), ctx)) n->args.push_back(std::move(cond));
        for (std::size_t i = 1; i < argc; ++i) {
            if (auto c = compile_value(arg(i), ctx)) n->children.push_back(std::move(c));
        }
        return n->args.size() == 1 ? std::move(n) : nullptr;
    }
    if (op == "if") {
        if (argc != 3) { err(ctx.diags, raw.span, "'if' expects 3 arguments: condition, then, else"); return nullptr; }
        auto n = make_node(NodeKind::If, op, raw.span);
        if (auto cond = compile_value(arg(0), ctx)) n->args.push_back(std::move(cond));
        if (auto t = compile_value(arg(1), ctx)) n->args.push_back(std::move(t));
        if (auto e = compile_value(arg(2), ctx)) n->args.push_back(std::move(e));
        return n->args.size() == 3 ? std::move(n) : nullptr;
    }
    if (op == "while") {
        if (argc < 1) { err(ctx.diags, raw.span, "'while' expects a condition and a body"); return nullptr; }
        auto n = make_node(NodeKind::While, op, raw.span);
        if (auto cond = compile_value(arg(0), ctx)) n->args.push_back(std::move(cond));
        for (std::size_t i = 1; i < argc; ++i) {
            if (auto c = compile_value(arg(i), ctx)) n->children.push_back(std::move(c));
        }
        return n->args.size() == 1 ? std::move(n) : nullptr;
    }
    if (op == "define") return compile_define(raw, argc, ctx, top_level);
    if (op == "set!") {
        if (argc != 2 || arg(0).kind != RawKind::Symbol) {
            err(ctx.diags, raw.span, "'set!' expects a variable name and a value");
            return nullptr;
        }
        if (is_reserved(arg(0).symbol)) {
            err(ctx.diags, arg(0).span, "'" + arg(0).symbol + "' is reserved and cannot be assigned");
            return nullptr;
        }
        auto n = make_node(NodeKind::SetVar, arg(0).symbol, raw.span);
        if (auto v = compile_value(arg(1), ctx)) n->args.push_back(std::move(v));
        return n->args.size() == 1 ? std::move(n) : nullptr;
    }

    if (is_command(op)) return compile_command(raw, op, argc, ctx);
    if (is_sensor(op)) {
        if (argc != 0) { err(ctx.diags, raw.span, "'" + op + "' takes no arguments"); return nullptr; }
        NodeKind kind = op == "distance-cm" ? NodeKind::DistanceCm
                      : op == "button"      ? NodeKind::Button
                      : op == "line-left"   ? NodeKind::LineLeft
                                            : NodeKind::LineRight;
        return make_node(kind, op, raw.span);
    }
    if (is_comparison(op) || is_boolean_op(op) || is_arithmetic(op)) {
        return compile_operator(raw, op, argc, ctx);
    }

    // Otherwise it must be a call to a user-defined function.
    return compile_call(raw, op, argc, ctx);
}

NodePtr compile_expr(const RawExpr& raw, Ctx& ctx, bool top_level) {
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
            Color color;
            if (color_from_name(raw.symbol, color)) {
                auto n = make_node(NodeKind::ColorLiteral, raw.symbol, raw.span);
                n->nums.push_back(static_cast<std::int64_t>(color));
                return n;
            }
            return make_node(NodeKind::VarRef, raw.symbol, raw.span); // variable reference
        }
        case RawKind::List:
            break;
    }
    if (raw.list.empty()) {
        err(ctx.diags, raw.span, "empty list is not a valid expression");
        return nullptr;
    }
    if (raw.list[0].kind != RawKind::Symbol) {
        err(ctx.diags, raw.list[0].span, "expected an operation name");
        return nullptr;
    }
    return compile_form(raw, ctx, top_level);
}

NodePtr compile_value(const RawExpr& raw, Ctx& ctx) {
    return compile_expr(raw, ctx, /*top_level=*/false);
}

void assign_preorder(Node& node, NodeId& next) {
    if (is_highlightable(node.kind)) node.id = next++;
    for (auto& a : node.args) assign_preorder(*a, next);
    for (auto& c : node.children) assign_preorder(*c, next);
}

// Collect top-level function signatures (name -> arity) for hoisting and call checks.
void collect_arity(const std::vector<const RawExpr*>& tops, ArityTable& out,
                   std::vector<Diagnostic>& diags) {
    for (const RawExpr* e : tops) {
        if (e->kind == RawKind::List && e->list.size() >= 2 &&
            e->list[0].kind == RawKind::Symbol && e->list[0].symbol == "define" &&
            e->list[1].kind == RawKind::List && !e->list[1].list.empty() &&
            e->list[1].list[0].kind == RawKind::Symbol) {
            const std::string& name = e->list[1].list[0].symbol;
            if (out.count(name)) {
                err(diags, e->list[1].list[0].span, "function '" + name + "' is defined more than once");
            } else {
                out[name] = e->list[1].list.size() - 1;
            }
        }
    }
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
        if (!has_errors(result.diagnostics)) err(result.diagnostics, SourceSpan{}, "program is empty");
        return result;
    }

    // Decide the top-level statement list and the root's shape. A single
    // explicit (do ...) is unwrapped so its children count as top level.
    const bool single_do = parsed.exprs.size() == 1 &&
                           parsed.exprs[0].kind == RawKind::List &&
                           !parsed.exprs[0].list.empty() &&
                           parsed.exprs[0].list[0].kind == RawKind::Symbol &&
                           parsed.exprs[0].list[0].symbol == "do";

    std::vector<const RawExpr*> tops;
    SourceSpan root_span = parsed.exprs.front().span;
    if (single_do) {
        root_span = parsed.exprs[0].span;
        for (std::size_t i = 1; i < parsed.exprs[0].list.size(); ++i) {
            tops.push_back(&parsed.exprs[0].list[i]);
        }
    } else {
        for (const auto& e : parsed.exprs) tops.push_back(&e);
        root_span.end_offset = parsed.exprs.back().span.end_offset;
        root_span.end_line = parsed.exprs.back().span.end_line;
        root_span.end_column = parsed.exprs.back().span.end_column;
    }

    ArityTable arity;
    collect_arity(tops, arity, result.diagnostics);
    Ctx ctx{arity, result.diagnostics};

    std::vector<NodePtr> children;
    for (const RawExpr* e : tops) {
        if (auto n = compile_expr(*e, ctx, /*top_level=*/true)) children.push_back(std::move(n));
    }

    NodePtr root;
    const bool single_statement = !single_do && parsed.exprs.size() == 1;
    if (single_statement) {
        if (!children.empty()) root = std::move(children[0]);
    } else {
        auto do_node = make_node(NodeKind::Do, "do", root_span);
        do_node->children = std::move(children);
        root = std::move(do_node);
    }

    if (root) {
        assign_node_ids(*root);
        result.root = std::move(root);
    }
    return result;
}

} // namespace cardcode
