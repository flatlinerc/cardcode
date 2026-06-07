# CardCode Execution Engine

A standalone C++23 execution engine for **CardCode**, a tiny S-expression robot
teaching language. Source text is the canonical form: it is parsed
deterministically into an internal AST, each node gets a deterministic ID, and
execution emits those IDs (with source spans) so a UI can highlight the matching
statement/card.

See `HANDOFF.md` for the full design brief.

## Status: Milestones 1–4 complete

**Implemented**

- Lexer (parens, symbols, integers, `;` comments, source spans, diagnostics)
- Parser (raw S-expression tree, recovery diagnostics)
- Compiler (typed AST, semantic + safety validation, top-level `do` normalization)
- Deterministic pre-order node IDs
- Conditionals (`when`/`if`), sensors, comparison/boolean/arithmetic operators,
  colored `light`, `beep`
- Value model with truthiness; runtime type checks; division-by-zero error
- Blocking interpreter with `RobotHost` abstraction and `ExecutionEventSink`
- Cooperative cancellation via an `std::atomic<bool>` flag + emergency stop
- Mock robot host (with programmable sensors) + CLI runner (`cardcode-run`)
- doctest unit tests: lexer, parser, compiler, IDs, execution, errors/safety,
  conditions/sensors/operators, and cancellation

**Language**

```lisp
; control flow
(do ...)
(repeat COUNT ...)            ; COUNT is an integer literal in v1
(when CONDITION ...)
(if CONDITION THEN ELSE)      ; THEN/ELSE are single statements (use do to group)

; commands
(drive SPEED MS)   (backward SPEED MS)
(turn-left DEG)    (turn-right DEG)
(stop)             (wait MS)
(light COLOR)      (beep)        ; COLOR: off red green blue yellow white

; sensors (value-producing)
(distance-cm) (button) (line-left) (line-right)

; operators (value-producing)
(< A B) (> A B) (= A B) (<= A B) (>= A B)
(and A B ...) (or A B ...) (not A)
(+ A B ...) (- A B) (* A B ...) (/ A B)
```

**Highlighting policy**: control-flow, command, and sensor nodes are
highlightable — they receive IDs *and* emit `NodeStart`/`NodeDone` events.
Operators and literals are assigned internal IDs for determinism but evaluate
silently (no events). The movement-slice IDs from Milestone 1 are unchanged
because command numeric arguments are stored inline rather than as child nodes.

**Deferred (Milestone 5, embedded prep)**: no functional language work remains;
the next step is interface hardening for ESP32 (avoiding heap churn in the hot
path, optional embedded build flags). Not started.

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
./build/cardcode-run examples/obstacle.ccode
./build/cardcode-run examples/line.ccode
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
- **Safety**: command arguments are integer literals, so range checks (speed,
  duration, degrees, wait, repeat count) happen at compile time with precise
  spans. The interpreter additionally enforces `max_total_steps` at runtime,
  treats division by zero / type mismatches as runtime errors, and calls
  `robot.stop()` on any error or cancellation.
- **Cancellation**: pass an `std::atomic<bool>*` to `execute`/`compile_and_run`.
  It is polled before each node; when set, the engine stops the robot, emits
  `ProgramError("execution cancelled")`, and returns `success == false`.
