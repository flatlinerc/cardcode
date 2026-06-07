#include "cardcode/compiler.hpp"

#include <string>

#include "cardcode/parser.hpp"

namespace cardcode {
namespace {

// Fixed v1 safety bounds (see HANDOFF.md "Safety Rules"). All v1 command
// arguments are integer literals, so these are validated at compile time and
// reported with precise source spans.
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

NodePtr compile_expr(const RawExpr& raw, std::vector<Diagnostic>& diags);

bool expect_int(const RawExpr& raw, std::int64_t& out,
                std::vector<Diagnostic>& diags, const char* what) {
    if (raw.kind != RawKind::Integer) {
        err(diags, raw.span, std::string("expected integer for ") + what);
        return false;
    }
    out = raw.integer;
    return true;
}

NodePtr compile_list(const RawExpr& raw, std::vector<Diagnostic>& diags) {
    const std::string& op = raw.list[0].symbol;
    const std::size_t argc = raw.list.size() - 1;
    auto arg = [&](std::size_t i) -> const RawExpr& { return raw.list[i + 1]; };

    if (op == "do") {
        auto n = make_node(NodeKind::Do, op, raw.span);
        for (std::size_t i = 0; i < argc; ++i) {
            if (auto child = compile_expr(arg(i), diags)) n->children.push_back(std::move(child));
        }
        return n;
    }

    if (op == "repeat") {
        if (argc < 1) {
            err(diags, raw.span, "'repeat' expects a count and a body");
            return nullptr;
        }
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
            if (auto child = compile_expr(arg(i), diags)) n->children.push_back(std::move(child));
        }
        return n;
    }

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
        if (argc != 1) {
            err(diags, raw.span, "'" + op + "' expects 1 argument: degrees");
            return nullptr;
        }
        auto n = make_node(kind, op, raw.span);
        std::int64_t deg = 0;
        if (expect_int(arg(0), deg, diags, "degrees")) {
            if (deg < 0 || deg > kTurnMax) {
                err(diags, arg(0).span,
                    "degrees must be between 0 and " + std::to_string(kTurnMax));
            }
        }
        n->nums.push_back(deg);
        return n;
    }

    if (op == "stop") {
        if (argc != 0) {
            err(diags, raw.span, "'stop' takes no arguments");
            return nullptr;
        }
        return make_node(NodeKind::Stop, op, raw.span);
    }

    if (op == "wait") {
        if (argc != 1) {
            err(diags, raw.span, "'wait' expects 1 argument: duration_ms");
            return nullptr;
        }
        auto n = make_node(NodeKind::Wait, op, raw.span);
        std::int64_t ms = 0;
        if (expect_int(arg(0), ms, diags, "duration_ms")) {
            if (ms < 0 || ms > kWaitMax) {
                err(diags, arg(0).span,
                    "duration_ms must be between 0 and " + std::to_string(kWaitMax));
            }
        }
        n->nums.push_back(ms);
        return n;
    }

    err(diags, raw.list[0].span, "unknown operation '" + op + "'");
    return nullptr;
}

NodePtr compile_expr(const RawExpr& raw, std::vector<Diagnostic>& diags) {
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
            return compile_list(raw, diags);
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
    for (auto& child : node.children) {
        assign_preorder(*child, next);
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
        if (!has_errors(result.diagnostics)) {
            err(result.diagnostics, SourceSpan{}, "program is empty");
        }
        return result;
    }

    NodePtr root;
    if (parsed.exprs.size() == 1) {
        // A single top-level expression is the root directly. An explicit
        // (do ...) therefore becomes n0; a lone (repeat ...) becomes n0.
        root = compile_expr(parsed.exprs[0], result.diagnostics);
    } else {
        // Multiple top-level expressions normalize into an implicit do root.
        const SourceSpan& first = parsed.exprs.front().span;
        const SourceSpan& last = parsed.exprs.back().span;
        SourceSpan span{first.start_offset, last.end_offset,
                        first.start_line, first.start_column,
                        last.end_line, last.end_column};
        auto do_node = make_node(NodeKind::Do, "do", span);
        for (const auto& e : parsed.exprs) {
            if (auto child = compile_expr(e, result.diagnostics)) {
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
