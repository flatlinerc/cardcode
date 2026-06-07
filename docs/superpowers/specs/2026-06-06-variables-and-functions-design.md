# CardCode: Variables & User-Defined Functions — Design

Date: 2026-06-06
Status: Approved

## Goal

Add variables, mutation, named functions (with recursion), and a `while` loop to
CardCode, so it can express robot-control programs that maintain state and react
to sensors over time. Stay within what a **card/block UI** can represent.

## Scope decisions (and why)

The card interface is the governing constraint. Variables, named functions with
parameters, and loops all map to Scratch-style cards; **lambdas, first-class
functions, and closures do not** (a closure is an anonymous value carrying hidden
captured state — there is no card for it, and it breaks "highlight the matching
card"). So:

- **In:** `define` (variable + function), `set!`, `while`, function calls,
  recursion (depth-capped).
- **Out:** `lambda`, functions as values, closures, nested lexical scopes,
  cons/lists, `quote`, macros.

Architecture reference was rui314/minilisp (environment chain, symbol lookup,
`define`/`setq`, function application, `while`) — we adopt its *eval/environment
shape* but not general-purpose Lisp.

## Syntax

```scheme
(define NAME EXPR)              ; bind a variable in the current scope
(define (NAME PARAM...) BODY…)  ; define a named function (top-level only)
(set! NAME EXPR)                ; reassign an existing variable
(while COND BODY…)             ; loop while COND is truthy
(NAME ARG...)                   ; call a user function
NAME                            ; variable reference (bare symbol, value position)
```

Reserved words (always literals; cannot be variable/function names):
`true false off red green blue yellow white`.

## Key internal change: arguments are expressions

Previously command arguments were inline integer literals. Now **every argument
position is an evaluated expression** (literal, variable ref, sensor, operator,
or call), so e.g. `(drive speed 1000)` and `(repeat n …)` work. Range validation
(speed 0–100, duration, degrees, wait, repeat count) therefore happens at
**runtime**; the compiler additionally checks any argument that is a literal, to
preserve early diagnostics.

## AST additions

New `NodeKind`s: `DefineVar`, `DefineFunc`, `SetVar`, `VarRef`, `Call`, `While`.
`Node` gains `std::vector<std::string> params` (function parameter names). The
node name (variable/function name) is stored in `op`. Command/`repeat` arguments
move from `nums` to expression `args`.

## Runtime model

- **Environment**: a global `Frame` (`unordered_map<string, Value>`) plus one
  fresh frame per function call. No `shared_ptr`/GC — a call frame lives only for
  the call. Resolution is flat: *local frame → global frame*. A function sees its
  own params/locals and globals, never its caller's locals (lexical).
- **Functions are a separate namespace (Lisp-2)**: `(square 6)` resolves in the
  function table; bare `square` resolves in the variable namespace. Functions are
  **not** values (no-lambda decision), so `Value` is unchanged (none/int/bool/color).
- **Hoisting**: the function table is built by a top-level pre-scan before
  execution, so call order is irrelevant and mutual recursion works.
- **Return value**: the value of the function body's last expression.

## Evaluation semantics

- `define x v` → bind in current frame (global at top level, local in a function);
  redefinition overwrites.
- `set! x v` → assign to nearest existing binding (local then global); undefined
  is a runtime error.
- `x` (VarRef) → look up; undefined is a runtime error.
- `(f a b)` → eval args, check arity, push a call frame, depth-check, eval body,
  return last value.
- `while` → re-eval COND each iteration; bodies share the enclosing frame (blocks
  are not scopes, Scratch-style).

## Node IDs & events

ID assignment switches to **highlightable nodes only** (counter increments only
for highlightable kinds while traversing the whole tree). This keeps the
Milestone-1 IDs stable now that commands carry expression args.

- Highlightable (IDs + events): `do/repeat/when/if/while`, all commands, sensors,
  `Call`, `DefineVar`, `SetVar`. A function body's nodes keep stable IDs across
  every call, so the UI highlights inside the function card each invocation.
- Not highlightable (no events): `VarRef`, operators, literals, and `DefineFunc`
  (a declaration; at runtime a no-op since the pre-scan already registered it).

## Safety

- `ExecutionLimits.max_call_depth = 64` → exceeding is a runtime error (caps
  runaway recursion before the C++ stack).
- `while`/recursion bounded by `max_total_steps` (every highlightable node start
  counts).
- Runtime range validation on command args; `robot.stop()` on any error.

## Diagnostics

- Compile time: unknown function, wrong call arity, function defined non-top-level,
  `define`/`set!` of a reserved word, duplicate function name, literal arg out of
  range, operator/sensor/literal used as a statement.
- Runtime: undefined variable, `set!` to undefined, call-depth exceeded, type
  mismatch (e.g. non-integer where integer required), out-of-range computed arg,
  division by zero.

## API / CLI / tests

- `compile`/`execute`/`compile_and_run` signatures unchanged. The function table
  rides inside the compiled tree and is rebuilt by `execute` via a pre-scan, so
  existing callers/tests keep working.
- `test_compiler` updates: `drive` args are now expression nodes (`IntegerLiteral`)
  rather than `nums`.
- New tests: variables (`define`/`set!`/scope), functions (params/return/arity/
  recursion + depth cap), `while`, and Lisp-2 namespace behavior.
- New examples: a function-using program and a `while` sensor loop.

## Out of scope / follow-ups

- Reference-cycle leaks are not a concern (no closures).
- Milestone 5 (embedded prep) unchanged and still pending.
```
