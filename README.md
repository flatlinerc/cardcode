# CardCode Execution Engine

A standalone C++23 execution engine for **CardCode**, a tiny S-expression robot
teaching language. Source text is the canonical form: it is parsed
deterministically into an internal AST, each node gets a deterministic ID, and
execution emits those IDs (with source spans) so a UI can highlight the matching
statement/card.

See `HANDOFF.md` for the full design brief.

## Status: Milestones 1–5 + variables/functions complete

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

See **Embedded integration** below for the ESP32/ESP-IDF story (Milestone 5).

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
include/cardcode/   public headers (ast, value, lexer, parser, compiler, engine, events, robot_host, mock_robot_host)
src/                lexer.cpp parser.cpp compiler.cpp engine.cpp   (the embeddable engine core)
tools/              cardcode_run.cpp (host-only CLI)
tests/              doctest suite + vendored third_party/doctest.h
examples/           square / obstacle / line / patrol / approach .ccode
docs/               design specs
```

## Design notes

- **Determinism**: `assign_node_ids` is a pre-order walk that numbers only
  *highlightable* nodes (control flow, commands, sensors, `Call`, `DefineVar`,
  `SetVar`). Operators, literals, `VarRef`, and `DefineFunc` are skipped, so
  command/loop IDs stay compact and stable even though arguments are now
  expression nodes. Repeated statements (and function-body nodes across calls)
  keep the same ID each time they run.
- **Errors vs. exceptions**: ordinary syntax/semantic problems return
  `Diagnostic`s, never exceptions. The engine core is in fact **exception-free**
  (see Embedded integration).
- **Safety**: literal command arguments are range-checked at compile time with
  precise spans; computed arguments are range-checked at runtime. The interpreter
  also enforces `max_total_steps` and `max_call_depth`, treats division by zero /
  type mismatches as runtime errors, and calls `robot.stop()` on any error or
  cancellation.
- **Cancellation**: pass an `std::atomic<bool>*` to `execute`/`compile_and_run`.
  It is polled before each node; when set, the engine stops the robot, emits
  `ProgramError("execution cancelled")`, and returns `success == false`.

## Integration

For embedding the engine into a host application — a desktop **websocket harness**
for developing the card UI and **ESP32 firmware** for the real robot — see
[`docs/integration/`](docs/integration/README.md): a shared, transport-agnostic
integration layer, the [JSON wire protocol](docs/integration/protocol.md) both
targets speak, and per-target guides for the
[local harness](docs/integration/local-harness.md) and
[ESP32](docs/integration/esp32.md).

The local harness is **implemented** in [`harness/`](harness/) (builds as
`cardcode-harness`). It speaks the JSON protocol over stdin/stdout (or `--port`
TCP); `npm run dev` wraps it in a browser WebSocket for local UI development.
Quick check:

```bash
printf '%s\n' '{"type":"run","source":"(repeat 4 (drive 40 1000) (turn-right 90))"}' \
  | ./build/cardcode-harness
```

### Browser card UI

The browser UI lives in `app/` and talks to the same JSON protocol as a robot.
The easiest local path is the dependency-free dev server, which serves the app
and exposes the mock harness at a browser WebSocket endpoint.

```bash
cmake -S . -B build
cmake --build build
npm run dev
```

Open `http://127.0.0.1:9000/`, connect to `ws://127.0.0.1:9000/runtime`, and run
the current CardCode program. The mock harness can also receive sensor overrides
from the UI.

To use a real robot, serve the same app and point the URL field at the robot's
WebSocket endpoint, for example `ws://cardcode.local/cardcode`.

## Embedded integration (Milestone 5)

The engine core is identical C++ that compiles on both desktop and ESP32; the
host CLI and tests are left behind. The harness pieces (`JsonEventSink`,
`ProtocolBridge`, the forwarding pattern) are intended to be reused on the
device, but the desktop `ForwardingRobotHost` is *not* a hardware driver —
firmware provides a small device-side wrapper around the `RobotHost`; see
[`Esp32ForwardingRobotHost` in the ESP32 guide](docs/integration/esp32.md#1-a-real-robothost).

- **Clean host boundary.** The engine never touches hardware; it calls the
  abstract `RobotHost` interface (`include/cardcode/robot_host.hpp`). Firmware
  provides a concrete `RobotHost` driving real motors/LEDs/sensors; the desktop
  build uses `MockRobotHost`.
- **Build just the engine.** Only the `cardcode_engine` library is needed; the
  CLI and tests are gated behind CMake options:

  ```bash
  cmake -S . -B build -DCARDCODE_EMBEDDED=ON   # engine only
  cmake --build build                          # -> libcardcode_engine.a
  ```

  `CARDCODE_EMBEDDED=ON` compiles the engine with `-fno-exceptions -fno-rtti`
  (the ESP-IDF defaults) and turns `CARDCODE_BUILD_TOOLS`/`CARDCODE_BUILD_TESTS`
  off. Those two options can also be toggled independently on the host.
- **No host-only dependencies.** The core (`include/cardcode/*`, `src/*.cpp`)
  pulls in only freestanding-friendly standard headers — no `<iostream>`,
  `<fstream>`, `<filesystem>`, or `<thread>`. `<iostream>`/`<fstream>` live only
  in the host CLI.
- **Exception-free.** The lexer parses integers without `std::stoll`, so nothing
  in the core throws; it compiles cleanly under `-fno-exceptions`. (Container
  allocation failure still calls `std::terminate`/`abort`, the expected behavior
  on a device under OOM.)
- **Hot-path allocation, with eyes open.** Two allocations occur during
  execution, both bounded by the safety limits: a per-call environment `Frame`
  (`unordered_map`, bounded by `max_call_depth`) and the per-event message
  `std::string` (bounded by `max_total_steps`). Acceptable for v1; the obvious
  future optimizations are a frame pool/arena and a string-free event payload.
  Compilation allocates freely but happens once, off the motion hot path.
