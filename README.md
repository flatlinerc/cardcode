# CardCode Execution Engine

A standalone C++23 execution engine for **CardCode**, a tiny S-expression robot
teaching language. Source text is the canonical form: it is parsed
deterministically into an internal AST, each node gets a deterministic ID, and
execution emits those IDs (with source spans) so a UI can highlight the matching
statement/card.

See `HANDOFF.md` for the full design brief.

## Status: Milestones 1–4 + variables/functions complete

**Implemented**

- Lexer (parens, symbols, integers, `;` comments, source spans, diagnostics)
- Parser (raw S-expression tree, recovery diagnostics)
- Compiler (typed AST, semantic + safety validation, top-level `do` normalization)
- Deterministic node IDs (highlightable nodes only)
- Conditionals (`when`/`if`), `while`, sensors, comparison/boolean/arithmetic
  operators, colored `light`, `beep`
- **Variables (`define`/`set!`) and user-defined functions with parameters and
  recursion** — an expression-oriented evaluator with a flat global+call-frame
  environment (Lisp-2 namespaces; no lambdas/closures, by design — see the spec)
- Value model with truthiness; runtime type checks; division-by-zero error
- Blocking interpreter with `RobotHost` abstraction and `ExecutionEventSink`
- Cooperative cancellation (`std::atomic<bool>`), call-depth limit, emergency stop
- Mock robot host (with programmable sensors) + CLI runner (`cardcode-run`)
- doctest unit tests: lexer, parser, compiler, IDs, execution, errors/safety,
  conditions/sensors/operators, cancellation, variables, functions, `while`

**Language**

```lisp
; control flow
(do ...)
(repeat COUNT ...)             ; COUNT is any integer expression
(when CONDITION ...)
(if CONDITION THEN ELSE)       ; THEN/ELSE are single expressions (use do to group)
(while CONDITION ...)

; variables and functions
(define NAME EXPR)             ; bind a variable in the current scope
(set! NAME EXPR)               ; reassign an existing variable
(define (NAME PARAM...) BODY…) ; named function (top level only); returns last expr
(NAME ARG...)                  ; call a user function

; commands (arguments are expressions)
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

Reserved words (always literals; cannot be variable/function names):
`true false off red green blue yellow white`.

**Expression-oriented**: every form produces a value (commands and control flow
yield None); a function/loop/conditional body is a sequence of expressions whose
last value is the result. This is what makes `(define (square n) (* n n))` and
recursion work.

**Scope & functions**: variables live in a flat environment — a global frame plus
one fresh frame per function call. Functions see their own params/locals and
globals, never their caller's locals. Functions are a separate namespace (Lisp-2)
and are hoisted, so call order is irrelevant and mutual recursion works. There
are **no lambdas/closures** — a deliberate choice so every construct maps to a
card in the block UI (see `docs/superpowers/specs/`).

**Highlighting policy**: control-flow, command, sensor, `Call`, `DefineVar`, and
`SetVar` nodes are highlightable — they get IDs *and* emit `NodeStart`/`NodeDone`.
Operators, literals, `VarRef`, and `DefineFunc` get no IDs and evaluate silently.
The Milestone-1 movement IDs are unchanged because command arguments are
non-highlightable expression nodes.

**Deferred (Milestone 5, embedded prep)**: interface hardening for ESP32
(avoiding heap churn in the hot path, optional embedded build flags). Not started.

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
./build/cardcode-run examples/patrol.ccode    # variables + a function
./build/cardcode-run examples/approach.ccode  # while + mutable counter
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
