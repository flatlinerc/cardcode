# CardCode Execution Engine Handoff



## Purpose



Build a standalone C++ execution engine for a tiny Lisp/S-expression style robot teaching language called **CardCode**.



This handoff covers only the execution engine:



- Source text parsing

- Compilation to an internal AST

- Deterministic node ID assignment

- Runtime execution of the AST

- Status/event callbacks for UI highlighting

- Host/robot API abstraction

- Tests



This handoff intentionally does **not** cover:



- Phone UI

- Card rendering

- Web display implementation

- JSON AST interchange

- Firmware replacement workflow

- Blockly/Scratch-style editor implementation

- Network protocol details beyond the local event callback interface



The core principle is:



> **The source of truth is CardCode source text.**

>

> CardCode text is parsed deterministically into an internal AST. Because compilation is deterministic, each expression gets a deterministic ID. During execution, the engine emits that ID so the UI can highlight the matching code statement and/or card view.



---



## Target Environment



Primary implementation target:



- Language: **C++**

- Compiler: **GCC 15**

- Standard: **C++23**, unless project constraints require C++20

- Initial platform: desktop Linux for development/testing

- Later platform: ESP32-class firmware integration, likely through ESP-IDF C++ or equivalent



Development should begin as a normal host-side C++ library and CLI test runner. Embedded integration should be kept in mind but should not slow down the initial execution engine.



---



## High-Level Architecture



```text

CardCode source text

    |

    v

Lexer

    |

    v

Parser

    |

    v

Raw syntax tree

    |

    v

Compiler / semantic validator

    |

    v

Internal executable AST with deterministic node IDs

    |

    v

Interpreter

    |

    +--> RobotHost command interface

    |

    +--> ExecutionEventSink status callbacks

```



Important rules:



1. Source text is canonical.

2. No JSON AST interchange is needed for the engine.

3. The AST is internal only.

4. The AST must preserve source spans for UI highlighting.

5. Node IDs must be deterministic for identical source text.

6. Execution must emit node IDs before/during/after running each meaningful expression.



---



## CardCode Language Overview



CardCode is a tiny S-expression language.



Example:



```lisp

(do

  (repeat 4

    (drive 40 1000)

    (turn-right 90))



  (when (< (distance-cm) 20)

    (stop)

    (light red)))

```



The first symbol in each list is the operation name. The remaining elements are arguments or child statements.



Examples:



```lisp

(drive 40 1000)

(turn-right 90)

(repeat 4

  (drive 40 1000)

  (turn-right 90))

```



The language should be deliberately small. This is a teaching/runtime language, not a general-purpose Lisp.



---



## Initial Language Features



### Program Forms



#### `(do ...)`



Execute child expressions sequentially.



```lisp

(do

  (drive 40 1000)

  (turn-right 90))

```



A source file may either be:



```lisp

(do ...)

```



or a sequence of top-level expressions. The compiler should normalize multiple top-level expressions into an implicit `do` root.



Example:



```lisp

(drive 40 1000)

(turn-right 90)

```



is equivalent internally to:



```lisp

(do

  (drive 40 1000)

  (turn-right 90))

```



#### `(repeat COUNT ...)`



Repeat child expressions `COUNT` times.



```lisp

(repeat 4

  (drive 40 1000)

  (turn-right 90))

```



Initial constraints:



- `COUNT` must be an integer expression.

- Negative counts are compile-time or runtime errors. Prefer compile-time if constant.

- There should be a maximum repeat count safety limit.



#### `(when CONDITION ...)`



Execute child expressions only when `CONDITION` evaluates truthy.



```lisp

(when (< (distance-cm) 20)

  (stop))

```



#### `(if CONDITION THEN_EXPR ELSE_EXPR)`



Evaluate condition. Execute `THEN_EXPR` if true, otherwise `ELSE_EXPR`.



```lisp

(if (< (distance-cm) 20)

  (stop)

  (drive 30 500))

```



For v1, `then` and `else` should be single expressions. A `do` expression can be used for multiple statements.



```lisp

(if (< (distance-cm) 20)

  (do

    (stop)

    (light red))

  (do

    (light green)

    (drive 30 500)))

```



---



## Initial Robot Commands



These are statement expressions that interact with the host robot API.



### `(drive SPEED MS)`



Drive forward.



- `SPEED`: integer, expected range `-100..100` or `0..100` depending on motor model. For v1, treat negative speed as invalid and provide separate `backward`.

- `MS`: integer milliseconds.



```lisp

(drive 40 1000)

```



### `(backward SPEED MS)`



Drive backward.



```lisp

(backward 30 500)

```



### `(turn-left DEGREES)`



Turn left by approximate degrees.



```lisp

(turn-left 90)

```



### `(turn-right DEGREES)`



Turn right by approximate degrees.



```lisp

(turn-right 90)

```



### `(stop)`



Stop all motors.



```lisp

(stop)

```



### `(wait MS)`



Wait for a number of milliseconds.



```lisp

(wait 250)

```



### `(light COLOR)`



Set a visible LED color.



```lisp

(light red)

```



Allowed initial colors:



- `off`

- `red`

- `green`

- `blue`

- `yellow`

- `white`



### `(beep)`



Emit a short beep if hardware supports it. Otherwise no-op in mock host.



```lisp

(beep)

```



---



## Initial Sensor / Value Expressions



### `(distance-cm)`



Return distance sensor value in centimeters.



```lisp

(distance-cm)

```



### `(button)`



Return boolean-ish value indicating whether a button is pressed.



```lisp

(button)

```



### `(line-left)` and `(line-right)`



Return line sensor state if available.



```lisp

(line-left)

(line-right)

```



For v1, mock these in tests even if hardware does not yet implement them.



---



## Initial Operators



### Numeric comparisons



```lisp

(< A B)

(> A B)

(= A B)

(<= A B)

(>= A B)

```



Each returns a boolean value.



### Boolean operators



```lisp

(and A B ...)

(or A B ...)

(not A)

```



Short-circuit behavior is preferred for `and` and `or`.



### Arithmetic operators



For v1, include basic arithmetic only if needed by examples/tests:



```lisp

(+ A B ...)

(- A B)

(* A B ...)

(/ A B)

```



Division by zero must be a runtime error.



---



## Deliberately Excluded from V1



Do not implement these unless explicitly added later:



- Variables

- User-defined functions

- Lambdas

- Macros

- Lists as values

- Strings, except maybe future speech/display commands

- Floating point unless required by hardware

- File IO

- Network IO from student code

- Arbitrary recursion

- Mutation

- General-purpose Lisp evaluation



This should be an interpreter for a fixed robot command language, not an open-ended Lisp runtime.



---



## Source Locations and Spans



Every parsed expression must preserve a source span:



```cpp

struct SourceSpan {

    std::uint32_t start_offset;

    std::uint32_t end_offset;

    std::uint32_t start_line;

    std::uint32_t start_column;

    std::uint32_t end_line;

    std::uint32_t end_column;

};

```



Offsets are byte offsets into the UTF-8 source string. Since v1 source should be ASCII-compatible, this is acceptable.



These spans allow the UI layer to map runtime events to exact source ranges.



---



## Deterministic Node IDs



Each executable AST node must have a deterministic ID.



### Required Properties



For identical source text, the compiled node IDs must be identical.



For semantically equivalent but textually different source, IDs do **not** need to match.



Examples:



```lisp

(drive 40 1000)

```



and



```lisp

( drive 40 1000 )

```



may produce different source spans but should preferably produce the same structural IDs if practical. This is a nice-to-have, not required for v1.



### Recommended V1 ID Strategy



Use pre-order traversal IDs after parsing and normalization:



```text

n0, n1, n2, n3, ...

```



Example source:



```lisp

(do

  (repeat 4

    (drive 40 1000)

    (turn-right 90))

  (stop))

```



Possible deterministic IDs:



```text

n0 = do

n1 = repeat

n2 = drive

n3 = turn-right

n4 = stop

```



This is simple and deterministic given a stable compiler.





### ID Type



Use a compact internal type:



```cpp

using NodeId = std::uint32_t;

```



When emitting externally, convert to a display string:



```cpp

std::string node_id_to_string(NodeId id); // "n42"

```



---



## Internal AST Design



Use a typed AST, not a generic list evaluator.



Suggested base types:



```cpp

using NodeId = std::uint32_t;



struct SourceSpan {

    std::uint32_t start_offset{};

    std::uint32_t end_offset{};

    std::uint32_t start_line{};

    std::uint32_t start_column{};

    std::uint32_t end_line{};

    std::uint32_t end_column{};

};



struct AstNode {

    NodeId id{};

    SourceSpan span{};

    virtual ~AstNode() = default;

};



struct Expr : AstNode {};

struct Stmt : AstNode {};

```



However, virtual inheritance may be undesirable for embedded targets. A `std::variant` AST is also acceptable.



Recommended for v1 host-side implementation:



```cpp

enum class NodeKind {

    Do,

    Repeat,

    When,

    If,

    Drive,

    Backward,

    TurnLeft,

    TurnRight,

    Stop,

    Wait,

    Light,

    Beep,

    DistanceCm,

    Button,

    LineLeft,

    LineRight,

    Compare,

    BooleanOp,

    Arithmetic,

    IntegerLiteral,

    BooleanLiteral,

    ColorLiteral

};



struct Node;

using NodePtr = std::unique_ptr<Node>;



struct Node {

    NodeId id{};

    NodeKind kind{};

    SourceSpan span{};



    // Keep v1 simple. Use fields selectively by kind.

    std::string op;

    std::vector<NodePtr> children;

    std::int64_t int_value{};

    bool bool_value{};

    std::string symbol_value;

};

```



This is not the most elegant long-term AST, but it is fast to build and easy to inspect.



If Codex prefers stronger typing, it may split this into dedicated structs and use `std::variant`. But do not over-engineer.



---



## Lexer Requirements



The lexer must support:



- `(`

- `)`

- symbols

- integers

- comments

- whitespace



### Comments



Use semicolon comments to end of line:



```lisp

; this is a comment

(drive 40 1000) ; comment after expression

```



### Symbols



Allowed symbol characters for v1:



```text

A-Z a-z 0-9 _ - ? ! < > = + * / .

```



A symbol may not begin with a digit unless it is parsed as a number.



Examples:



```text

drive

turn-right

distance-cm

<

>=

line-left

```



### Integers



Support decimal integers:



```text

0

1

42

-10

```



Negative literals are allowed for expressions, but motor commands may reject invalid ranges.



### Strings



Do not support strings in v1 unless needed. This keeps parsing small.



---



## Parser Requirements



The parser should parse source into a raw S-expression tree first.



Raw expression types:



```cpp

enum class RawKind {

    List,

    Symbol,

    Integer

};



struct RawExpr {

    RawKind kind;

    SourceSpan span;

    std::string symbol;

    std::int64_t integer{};

    std::vector<RawExpr> list;

};

```



Then compile `RawExpr` into typed internal AST nodes.



Benefits:



- Simpler lexer/parser

- Better error messages

- Language semantics isolated in compiler

- Easy future pretty-printer/card renderer support



---



## Compiler / Semantic Validation



The compiler converts raw S-expressions into executable AST nodes.



Examples:



Raw:



```lisp

(drive 40 1000)

```



Compiled:



```text

NodeKind::Drive

  speed = IntegerLiteral(40)

  duration_ms = IntegerLiteral(1000)

```



The compiler must validate:



- Known operation names

- Correct argument counts

- Correct expected argument types where statically knowable

- Required body expressions

- Empty lists are invalid

- Invalid top-level values are invalid unless wrapped in `do`



### Error Handling



Use a structured diagnostics system.



```cpp

enum class DiagnosticSeverity {

    Error,

    Warning

};



struct Diagnostic {

    DiagnosticSeverity severity;

    SourceSpan span;

    std::string message;

};



struct CompileResult {

    std::unique_ptr<Node> root;

    std::vector<Diagnostic> diagnostics;



    bool ok() const;

};

```



Do not throw for ordinary user syntax errors. Return diagnostics.



Throwing is acceptable only for programmer bugs or unrecoverable engine failures.



---



## Runtime Value Model



Use a small value type:



```cpp

enum class ValueKind {

    None,

    Integer,

    Boolean,

    Color

};



struct Value {

    ValueKind kind{ValueKind::None};

    std::int64_t integer{};

    bool boolean{};

    Color color{Color::Off};

};

```



For statements, return `None`.



Truthiness:



- Boolean: its value

- Integer: `0` is false, nonzero is true

- None: false

- Color: true, though color in conditions should usually be invalid unless intentionally allowed



Prefer compile-time validation to prevent weird condition values where practical.



---



## Execution Model



The interpreter should be cooperative and event-driven enough to support robot motion and UI highlighting.



For v1, implement a blocking interpreter that calls blocking host robot functions. This is simplest.



Later, convert to cooperative/async if needed.



### Basic Execution Flow



For each executable expression:



1. Emit `NodeStart`

2. Execute the expression

3. Emit `NodeDone`



On error:



1. Emit `NodeError`

2. Stop execution

3. Call `RobotHost::stop()` as safety fallback



### Event Types



```cpp

enum class ExecutionEventType {

    ProgramStart,

    ProgramDone,

    ProgramError,

    NodeStart,

    NodeDone,

    NodeSkipped,

    NodeError,

    Log

};



struct ExecutionEvent {

    ExecutionEventType type;

    NodeId node_id{};          // meaningful for node events

    SourceSpan span{};         // source range for UI highlight

    std::string message;       // error/log text

};



class ExecutionEventSink {

public:

    virtual ~ExecutionEventSink() = default;

    virtual void on_event(const ExecutionEvent& event) = 0;

};

```



### Required Highlighting Behavior



For this program:



```lisp

(repeat 2

  (drive 40 1000)

  (turn-right 90))

```



Expected event order:



```text

ProgramStart

NodeStart n0 repeat

NodeStart n1 drive

NodeDone  n1 drive

NodeStart n2 turn-right

NodeDone  n2 turn-right

NodeStart n1 drive

NodeDone  n1 drive

NodeStart n2 turn-right

NodeDone  n2 turn-right

NodeDone  n0 repeat

ProgramDone

```



The repeated child node IDs stay the same across iterations. This is important. The UI should see the same `drive` node highlighted each time that expression runs.



---



## Runtime Context



Use an execution context object:



```cpp

struct ExecutionLimits {

    std::uint32_t max_repeat_count = 100;

    std::uint32_t max_total_steps = 1000;

    std::uint32_t max_runtime_ms = 60000;

};



struct ExecutionContext {

    RobotHost& robot;

    ExecutionEventSink& events;

    ExecutionLimits limits;

    bool cancel_requested = false;

    std::uint32_t steps_executed = 0;

};

```



The interpreter should check limits frequently.



---



## RobotHost Interface



The engine must not directly access hardware. It should call a host interface.



```cpp

enum class Color {

    Off,

    Red,

    Green,

    Blue,

    Yellow,

    White

};



class RobotHost {

public:

    virtual ~RobotHost() = default;



    virtual void drive_forward(int speed, int duration_ms) = 0;

    virtual void drive_backward(int speed, int duration_ms) = 0;

    virtual void turn_left(int degrees) = 0;

    virtual void turn_right(int degrees) = 0;

    virtual void stop() = 0;

    virtual void wait_ms(int duration_ms) = 0;

    virtual void set_light(Color color) = 0;

    virtual void beep() = 0;



    virtual int distance_cm() = 0;

    virtual bool button_pressed() = 0;

    virtual bool line_left() = 0;

    virtual bool line_right() = 0;

};

```



For desktop tests, implement `MockRobotHost` that records commands instead of moving hardware.



---



## Safety Rules



The execution engine should enforce basic safety even before hardware integration.



Recommended v1 limits:



```text

speed: 0..100

drive/backward duration: 0..10000 ms

turn degrees: 0..3600

wait: 0..10000 ms

repeat count: 0..100

max total executed node starts: 1000

```



On runtime error:



- emit error event

- call `robot.stop()`

- halt program



On cancellation:



- call `robot.stop()`

- emit `ProgramError` or future `ProgramCancelled`

- halt program



---



## Status Updates / Deterministic IDs



The event sink does not need to know the AST. Each event includes:



- node ID

- source span

- event type

- optional message



Example event object in C++:



```cpp

ExecutionEvent{

    .type = ExecutionEventType::NodeStart,

    .node_id = 2,

    .span = node.span,

    .message = "drive"

};

```



The UI can map this to:



```text

node id n2

source span bytes 24..39

card rendered from that source expression

```



Because the compiler is deterministic, a separately parsed UI-side representation can assign the same node IDs if it implements the same traversal rules. However, the engine should still emit source spans to make UI mapping robust.



---



## Deterministic ID Assignment Algorithm



After parsing and semantic compilation, run a deterministic assignment pass:



```cpp

void assign_node_ids(Node& root) {

    NodeId next = 0;

    assign_preorder(root, next);

}



void assign_preorder(Node& node, NodeId& next) {

    node.id = next++;

    for (auto& child : node.children) {

        assign_preorder(*child, next);

    }

}

```



This pass must traverse only AST nodes that correspond to highlightable expressions.



Decision for v1:



- Command and control expressions get IDs.

- Literal values may have IDs internally if convenient, but do not emit execution events for literals.

- If literals receive IDs, UI card mapping may become confusing. Prefer not assigning IDs to literals unless needed.



Recommended v1 approach:



- Use separate node classes/kinds for executable expressions.

- Literals are value nodes but not highlightable runtime nodes.

- Assign IDs to all expression nodes that can produce meaningful execution steps.



Example:



```lisp

(drive 40 1000)

```



Only `drive` gets a runtime event ID. `40` and `1000` do not emit events.



For conditions:



```lisp

(< (distance-cm) 20)

```



Both `<` and `distance-cm` may get IDs, but for visual simplicity, v1 can emit only for robot/sensor/control expressions. Choose one policy and document it in tests.



Recommended policy:



- Emit for control expressions: `do`, `repeat`, `when`, `if`

- Emit for robot command expressions: `drive`, `turn-right`, etc.

- Emit for sensor reads: `distance-cm`, `button`, etc.

- Do not emit for pure operators/literals by default.



Still assign deterministic internal IDs to pure operators if useful, but do not require UI highlighting for them.



---



## Project Structure



Suggested repository layout:



```text

cardcode-engine/

  CMakeLists.txt

  README.md

  include/

    cardcode/

      ast.hpp

      diagnostic.hpp

      engine.hpp

      events.hpp

      lexer.hpp

      parser.hpp

      compiler.hpp

      robot_host.hpp

      value.hpp

  src/

    ast.cpp

    diagnostic.cpp

    engine.cpp

    lexer.cpp

    parser.cpp

    compiler.cpp

    robot_host.cpp

  tests/

    CMakeLists.txt

    test_lexer.cpp

    test_parser.cpp

    test_compiler.cpp

    test_ids.cpp

    test_execution.cpp

    test_errors.cpp

  tools/

    cardcode_run.cpp

```



Use CMake.



Minimum CMake:



```cmake

cmake_minimum_required(VERSION 3.25)

project(cardcode_engine LANGUAGES CXX)



set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)



add_library(cardcode_engine

  src/lexer.cpp

  src/parser.cpp

  src/compiler.cpp

  src/engine.cpp

)



target_include_directories(cardcode_engine PUBLIC include)



target_compile_options(cardcode_engine PRIVATE

  -Wall -Wextra -Wpedantic

)

```



---



## Public API



The library should expose one simple high-level function for initial use:



```cpp

namespace cardcode {



struct RunOptions {

    ExecutionLimits limits{};

};



struct RunResult {

    bool success{};

    std::vector<Diagnostic> diagnostics;

};



RunResult compile_and_run(

    std::string_view source,

    RobotHost& robot,

    ExecutionEventSink& events,

    const RunOptions& options = {}

);



} // namespace cardcode

```



Also expose lower-level APIs for tests:



```cpp

LexResult lex(std::string_view source);

ParseResult parse(std::string_view source);

CompileResult compile(std::string_view source);

ExecutionResult execute(const Node& root, RobotHost& robot, ExecutionEventSink& events, const ExecutionLimits& limits);

```



---



## CLI Tool



Provide a small CLI tool for development:



```bash

cardcode-run examples/square.ccode

```



It should:



1. Read source file

2. Compile

3. Print diagnostics if any

4. Execute using `MockRobotHost`

5. Print event stream and mock robot commands



Example output:



```text

ProgramStart

NodeStart n0 repeat bytes=0..62

NodeStart n1 drive bytes=14..29

ROBOT drive_forward speed=40 ms=1000

NodeDone n1

NodeStart n2 turn-right bytes=34..49

ROBOT turn_right degrees=90

NodeDone n2

...

ProgramDone

```



---



## Examples



Create an `examples/` directory.



### `examples/square.ccode`



```lisp

(repeat 4

  (drive 40 1000)

  (turn-right 90))

```



### `examples/obstacle.ccode`



```lisp

(do

  (drive 30 500)

  (when (< (distance-cm) 20)

    (stop)

    (light red)

    (beep)))

```



### `examples/line.ccode`



```lisp

(if (line-left)

  (turn-left 10)

  (drive 25 200))

```



---



## Tests



Use any simple C++ test framework. Acceptable options:



- Catch2

- doctest

- GoogleTest

- Simple custom assert-based runner



Prefer doctest or Catch2 for speed.



### Lexer Tests



Test:



- parentheses

- symbols

- integers

- comments

- source spans

- invalid characters



### Parser Tests



Test:



- single expression

- nested expression

- multiple top-level expressions

- missing closing paren

- extra closing paren

- empty list rejection or parse then compile rejection



### Compiler Tests



Test:



- known commands compile

- unknown command diagnostic

- wrong argument count diagnostic

- invalid command argument diagnostic

- top-level normalization to `do`



### Deterministic ID Tests



Given:



```lisp

(repeat 2

  (drive 40 1000)

  (turn-right 90))

```



Assert stable IDs:



```text

repeat = n0

drive = n1

turn-right = n2

```



Compile the same source twice and assert identical ID assignment and spans.



### Execution Tests



Use `MockRobotHost` and `RecordingEventSink`.



Test square:



```lisp

(repeat 2

  (drive 40 1000)

  (turn-right 90))

```



Expected robot command sequence:



```text

drive_forward(40,1000)

turn_right(90)

drive_forward(40,1000)

turn_right(90)

```



Expected node event sequence:



```text

ProgramStart

NodeStart n0

NodeStart n1

NodeDone n1

NodeStart n2

NodeDone n2

NodeStart n1

NodeDone n1

NodeStart n2

NodeDone n2

NodeDone n0

ProgramDone

```



### Safety Tests



Test:



- invalid speed rejected

- excessive duration rejected

- excessive repeat rejected

- max total steps exceeded

- divide by zero errors

- error calls `robot.stop()`



---



## Error Message Examples



Unknown operation:



```lisp

(fly 10)

```



Diagnostic:



```text

Error at line 1, column 2: unknown operation 'fly'

```



Wrong argument count:



```lisp

(drive 40)

```



Diagnostic:



```text

Error at line 1, column 2: 'drive' expects 2 arguments: speed and duration_ms

```



Invalid speed:



```lisp

(drive 140 1000)

```



Diagnostic:



```text

Error at line 1, column 8: speed must be between 0 and 100

```



---



## Acceptance Criteria



The execution engine is acceptable when all of these are true:



1. A C++23 project builds with GCC 15.

2. `cardcode-run examples/square.ccode` compiles and runs against a mock robot host.

3. The parser accepts nested S-expression CardCode.

4. The compiler produces an internal AST, not JSON.

5. Node IDs are assigned deterministically using documented traversal.

6. Runtime events include node ID and source span.

7. Repeated statements emit the same node ID on each iteration.

8. Robot commands are called through `RobotHost`, not directly from AST code.

9. Syntax/compile errors return diagnostics with useful source spans.

10. Runtime safety limits stop dangerous or runaway programs.

11. Unit tests cover lexer, parser, compiler, IDs, execution, and errors.



---



## Important Implementation Notes for Codex



- Keep the v1 language small.

- The source text is canonical.

- The compiler output is an internal AST.

- Node IDs are deterministic because the compiler traversal is deterministic.

- Events are the bridge from runtime to UI highlighting.



---



## Suggested First Milestone



Build the absolute minimum vertical slice:



```lisp

(repeat 4

  (drive 40 1000)

  (turn-right 90))

```



The vertical slice should include:



1. Lexer

2. Parser

3. Compiler

4. Deterministic IDs

5. Mock robot host

6. Interpreter

7. Event recording

8. CLI runner

9. Tests proving repeated node IDs



Do not add sensors, conditionals, arithmetic, or colors until this works end-to-end.



---



## Suggested Milestone Order



### Milestone 1: Minimal movement language



Supported:



```lisp

(drive SPEED MS)

(turn-right DEGREES)

(turn-left DEGREES)

(stop)

(wait MS)

(repeat COUNT ...)

(do ...)

```



### Milestone 2: Diagnostics and spans



Improve parser/compiler errors and verify spans.



### Milestone 3: Conditions and sensors



Supported:



```lisp

(when CONDITION ...)

(if CONDITION THEN ELSE)

(< A B)

(> A B)

(= A B)

(distance-cm)

(button)

```



### Milestone 4: Safety and cancellation



Add runtime limits, cancellation, and emergency stop behavior.



### Milestone 5: Embedded preparation



Prepare interface boundaries for ESP32 integration:



- no heap-heavy surprises in hot path if avoidable

- clear host interface

- no desktop-only dependencies in engine core

- optional compile flags for embedded builds



