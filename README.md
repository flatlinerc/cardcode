# CardCode Execution Engine

A standalone C++23 execution engine for **CardCode**, a tiny S-expression robot
teaching language. Source text is the canonical form: it is parsed
deterministically into an internal AST, each node gets a deterministic ID, and
execution emits those IDs (with source spans) so a UI can highlight the matching
statement/card.

See `HANDOFF.md` for the full design brief.

## Status: Milestone 1 vertical slice

This pass implements the end-to-end movement slice and nothing more, per the
handoff's "do not add sensors/conditionals/arithmetic/colors until this works
end-to-end" guidance.

**Implemented**

- Lexer (parens, symbols, integers, `;` comments, source spans, diagnostics)
- Parser (raw S-expression tree, recovery diagnostics)
- Compiler (typed AST, semantic + safety validation, top-level `do` normalization)
- Deterministic pre-order node IDs
- Blocking interpreter with `RobotHost` abstraction and `ExecutionEventSink`
- Mock robot host + CLI runner (`cardcode-run`)
- doctest unit tests: lexer, parser, compiler, IDs, execution, errors/safety

**Language subset**

```lisp
(do ...)
(repeat COUNT ...)
(drive SPEED MS)
(backward SPEED MS)
(turn-left DEGREES)
(turn-right DEGREES)
(stop)
(wait MS)
```

**Deferred to later milestones**: `when`/`if`, comparison/boolean/arithmetic
operators, sensors (`distance-cm`, `button`, `line-left`, `line-right`),
`light`/`beep` and colors. The `RobotHost` interface and `Color` enum carry
forward-compatibility hooks but those commands are not yet wired up.

## Build & test

Requires CMake >= 3.25 and a C++23 compiler. (Developed against GCC 16; the
handoff targets GCC 15 — either works for C++23.)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run a program

```bash
./build/cardcode-run examples/square.ccode
```

Example output:

```text
ProgramStart
NodeStart n0 repeat bytes=...
NodeStart n1 drive bytes=...
  ROBOT drive_forward a0=40 a1=1000
NodeDone n1
NodeStart n2 turn-right bytes=...
  ROBOT turn_right a0=90 a1=0
NodeDone n2
...
ProgramDone
```

## Layout

```text
include/cardcode/   public headers (ast, lexer, parser, compiler, engine, events, robot_host, ...)
src/                lexer.cpp parser.cpp compiler.cpp engine.cpp
tools/              cardcode_run.cpp (CLI)
tests/              doctest suite + vendored third_party/doctest.h
examples/           square.ccode
```

## Design notes

- **Determinism**: `assign_node_ids` is a pre-order walk over the highlightable
  AST. Numeric arguments are stored inline on each node (not as child nodes), so
  only control-flow/command expressions receive IDs and emit events — literals
  never do. Repeated statements keep the same ID across iterations.
- **Errors vs. exceptions**: ordinary syntax/semantic problems return
  `Diagnostic`s; exceptions are reserved for genuine engine bugs.
- **Safety**: v1 arguments are integer literals, so range checks (speed, duration,
  degrees, wait, repeat count) happen at compile time with precise spans. The
  interpreter additionally enforces `max_total_steps` at runtime and calls
  `robot.stop()` on any error.
