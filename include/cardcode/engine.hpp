#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "cardcode/ast.hpp"
#include "cardcode/compiler.hpp"
#include "cardcode/diagnostic.hpp"
#include "cardcode/events.hpp"
#include "cardcode/robot_host.hpp"

namespace cardcode {

struct ExecutionLimits {
    std::uint32_t max_repeat_count = 100;
    std::uint32_t max_total_steps = 1000;
    std::uint32_t max_runtime_ms = 60000; // reserved for future cooperative scheduling
};

struct ExecutionResult {
    bool success{};
    std::optional<Diagnostic> error; // set when success == false
};

// Run a compiled program against a robot host, emitting events as it goes.
// Emits ProgramStart, then per-node events, then ProgramDone or ProgramError.
// On any runtime error it calls robot.stop() as a safety fallback.
ExecutionResult execute(const Node& root,
                        RobotHost& robot,
                        ExecutionEventSink& events,
                        const ExecutionLimits& limits = {});

struct RunOptions {
    ExecutionLimits limits{};
};

struct RunResult {
    bool success{};
    std::vector<Diagnostic> diagnostics;
};

// High-level convenience: compile then (if compilation succeeded) execute.
RunResult compile_and_run(std::string_view source,
                          RobotHost& robot,
                          ExecutionEventSink& events,
                          const RunOptions& options = {});

} // namespace cardcode
