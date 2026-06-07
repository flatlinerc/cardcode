#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "cardcode/ast.hpp"
#include "cardcode/diagnostic.hpp"

namespace cardcode {

struct CompileResult {
    std::unique_ptr<Node> root; // populated even on error when a partial tree exists
    std::vector<Diagnostic> diagnostics;

    bool ok() const { return root != nullptr && !has_errors(diagnostics); }
};

// Lex + parse + semantically validate, producing the typed AST with
// deterministic node IDs assigned.
CompileResult compile(std::string_view source);

} // namespace cardcode
