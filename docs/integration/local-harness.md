# Local Harness (desktop)

A desktop program that runs the engine and speaks the [wire protocol](protocol.md)
so you can develop and test the **card UI with no hardware**. It forwards every
robot call as a `robotCommand`, returns UI-injected sensor values, and runs the
engine on a worker thread.

> **Status: implemented.** The harness lives in [`harness/`](../../harness/) and
> builds as the `cardcode-harness` target. This doc explains
> its design; the remaining transports (C header-only WS, D WASM) are documented
> for when they're needed.

## Quickstart

```bash
cmake -S . -B build && cmake --build build      # builds cardcode-harness
printf '%s\n' \
  '{"type":"setSensor","sensor":"distance-cm","value":12}' \
  '{"type":"run","source":"(when (< (distance-cm) 20) (light red) (beep))"}' \
  | ./build/cardcode-harness                     # emits the full protocol stream

# Serve the browser UI on 0.0.0.0 and expose the mock runtime at /runtime:
npm run dev

# Or speak the line protocol directly over TCP for non-browser tests:
./build/cardcode-harness --port 8080
# Add --realtime to sleep for each command's duration (realistic highlight timing).
```

This doc shows the two reusable adapters — a JSON-emitting `ExecutionEventSink`
and a command-forwarding `RobotHost` — then the transport options.

## Pieces

```
incoming JSON  ──▶ ProtocolBridge ──▶ compile()/execute() on a worker thread
                        │                    │            │
                        │                    │            ├─▶ JsonEventSink ─┐
                        │                    │            └─▶ ForwardingHost ─┤
                        │                    │                                ▼
                        └──────────── outgoing message queue ──────────▶ Transport ──▶ UI
```

The two adapters and the bridge are **portable C++** (no host-only headers); the
ESP32 build reuses them verbatim. Only the Transport differs per target.

### 1. `JsonEventSink` — `ExecutionEvent` → protocol JSON

`ExecutionEventSink::on_event` is called synchronously from the execution thread.
Translate each event to a protocol message and hand it to a thread-safe sink
(`emit`), which the transport drains in order.

```cpp
// Illustrative. Uses only the public API in include/cardcode/.
#include "cardcode/events.hpp"
#include "cardcode/ast.hpp"
#include <functional>
#include <string>

class JsonEventSink : public cardcode::ExecutionEventSink {
public:
  explicit JsonEventSink(std::function<void(std::string)> emit) : emit_(std::move(emit)) {}

  void on_event(const cardcode::ExecutionEvent& e) override {
    using T = cardcode::ExecutionEventType;
    switch (e.type) {
      case T::ProgramStart: emit_(R"({"type":"programStart"})"); break;
      case T::ProgramDone:  emit_(R"({"type":"programDone"})");  break;
      case T::ProgramError:
        emit_(jobj({{"type","\"programError\""},
                   {"message", jstr(e.message)},
                   {"span", jspan(e.span)}}));
        break;
      case T::NodeStart:    emit_(node_json("start", e));    break;
      case T::NodeDone:     emit_(node_json("done", e));     break;
      case T::NodeSkipped:  emit_(node_json("skipped", e));  break;
      case T::NodeError:    emit_(node_json("error", e));    break;
      case T::Log:
        emit_(jobj({{"type","\"log\""}, {"message", jstr(e.message)}}));
        break;
    }
  }

private:
  std::string node_json(const char* ev, const cardcode::ExecutionEvent& e) {
    // id, nodeId, op(=message), span; plus message for the error case.
    return jobj({{"type","\"node\""},
                {"event", std::string("\"") + ev + "\""},
                {"id", jstr(cardcode::node_id_to_string(e.node_id))},
                {"nodeId", std::to_string(e.node_id)},
                {"op", jstr(e.message)},
                {"span", jspan(e.span)}});
  }
  // jstr() = JSON-escaped quoted string; jobj() = {k:v,...} (initializer-list
  // form); jspan() mirrors SourceSpan field names from protocol.md. (Small
  // hand-rolled JSON — no dep. Real impl lives in harness/json.hpp.)
  std::function<void(std::string)> emit_;
};
```

> Why hand-rolled JSON: it keeps the harness dependency-free (consistent with the
> exception/host-header-free engine), and the payloads are tiny and fixed-shape.
> If you'd rather pull in a JSON library on the desktop, swap `jobj/jstr/jspan`
> for it — the rest is unchanged. (On ESP32, prefer the hand-rolled version or
> ESP-IDF's bundled cJSON to avoid RAM/flash cost.)

### 2. `ForwardingRobotHost` — `RobotHost` calls → `robotCommand` JSON

Subclass `RobotHost` directly. Hold UI-injected sensor values as plain fields,
set them via the `setSensor` control messages, and forward each actuator call as
a `robotCommand` message. (The real class in
[`harness/forwarding_host.hpp`](../../harness/forwarding_host.hpp) uses
`std::atomic`s for the sensor fields so the engine and control threads don't
race; the sketch keeps them plain since the doc is illustrative.)

```cpp
#include "cardcode/robot_host.hpp"
#include <functional>
#include <string>

class ForwardingRobotHost : public cardcode::RobotHost {
public:
  explicit ForwardingRobotHost(std::function<void(std::string)> emit) : emit_(std::move(emit)) {}

  // --- UI-injected sensor values (from setSensor control messages) ---
  void reset() {
    distance_   = 1000;
    button_     = false;
    line_left_  = false;
    line_right_ = false;
  }
  void set_distance(int cm)  { distance_   = cm; }
  void set_button(bool v)    { button_     = v; }
  void set_line_left(bool v) { line_left_  = v; }
  void set_line_right(bool v){ line_right_ = v; }

  // --- Actuators: emit robotCommand, then (optionally) take real time ---
  // drive_forward / drive_backward / wait_ms sleep for their durationMs; turns
  // are paced at 90 deg/sec and beep takes 250 ms. stop / set_light are instant.
  void drive_forward(int s, int ms) override {
    emit_(cmd("drive_forward", {{"speed", std::to_string(s)}, {"durationMs", std::to_string(ms)}}));
    delay(ms);
  }
  void drive_backward(int s, int ms) override {
    emit_(cmd("drive_backward", {{"speed", std::to_string(s)}, {"durationMs", std::to_string(ms)}}));
    delay(ms);
  }
  void turn_left(int d)  override { emit_(cmd("turn_left",  {{"degrees", std::to_string(d)}})); delay(turn_ms(d)); }
  void turn_right(int d) override { emit_(cmd("turn_right", {{"degrees", std::to_string(d)}})); delay(turn_ms(d)); }
  void stop()            override { emit_(cmd("stop", {})); }
  void wait_ms(int ms) override {
    emit_(cmd("wait", {{"durationMs", std::to_string(ms)}}));
    delay(ms);
  }
  void set_light(cardcode::Color c) override {
    emit_(cmd("set_light", {{"color", std::string("\"") + cardcode::color_name(c) + "\""}}));
  }
  void beep() override { emit_(cmd("beep", {})); delay(250); }

  // --- Sensors: report the injected value and echo it as sensorRead ---
  int  distance_cm()    override { int  v = distance_;   emit_(sensor("distance-cm", std::to_string(v)));     return v; }
  bool button_pressed() override { bool v = button_;     emit_(sensor("button",     v ? "true" : "false"));    return v; }
  bool line_left()     override  { bool v = line_left_;  emit_(sensor("line-left",  v ? "true" : "false"));    return v; }
  bool line_right()    override  { bool v = line_right_; emit_(sensor("line-right", v ? "true" : "false"));    return v; }

private:
  std::function<void(std::string)> emit_;
  int  distance_   = 1000;
  bool button_     = false;
  bool line_left_  = false;
  bool line_right_ = false;

  void delay(int /*ms*/) { /* no-op in the sketch; see Timing note below */ }
  // Turns carry only an angle, so derive ms from a fixed 90 deg/sec turn rate.
  static int turn_ms(int d) { return (d < 0 ? -d : d) * 1000 / 90; }
  // cmd() and sensor() build the protocol JSON strings — see the JsonEventSink
  // sketch for the helper style. (Real impl uses jobj/jstr/jspan from
  // harness/json.hpp; the sketch is hand-rolled for clarity.)
  std::string cmd(const char* /*name*/, std::initializer_list<std::pair<std::string,std::string>> /*args*/);
  std::string sensor(const char* /*name*/, const std::string& /*value_json*/);
};
```

> Timing: `drive_forward` / `drive_backward` / `wait_ms` sleep for their
> `durationMs`; `turn_left` / `turn_right` are paced at 90 deg/sec and `beep`
> takes 250 ms, so every motion is visible. `stop` / `set_light` return
> instantly. For UI realism, gate the sleep on a `realtime_` flag (default off)
> so headless tests don't actually wait. Do the sleep on the worker thread,
> never the transport thread.

### 3. `ProtocolBridge` — parse control messages, drive the engine

```cpp
#include "cardcode/engine.hpp"
#include <atomic>
#include <thread>

class ProtocolBridge {
public:
  explicit ProtocolBridge(std::function<void(std::string)> emit) : emit_(std::move(emit)) {}

  void handle(const std::string& json_line) {
    // parse {"type":...}; dispatch:
    //  "compile"  -> compile(), emit diagnostics
    //  "run"      -> emit diagnostics; if ok, start worker (below)
    //  "cancel"   -> cancel_.store(true)
    //  "setSensor"-> set robot_.distance_value / button_value / line_*_value
    //  "reset"    -> reset robot_ state
    //
    // On any rejection, emit {"type":"error","message":...} and return. Cases
    // (see harness/protocol_bridge.hpp):
    //   - Malformed JSON              -> "malformed message"
    //   - Missing "type"              -> "missing 'type'"
    //   - Unknown message "type"      -> "unknown message type '...'"
    //   - "compile" w/o "source"      -> "compile: missing 'source'"
    //   - "run" w/o "source"          -> "run: missing 'source'"
    //   - "run" while already running -> "a program is already running"
    //   - "setSensor" w/o sensor/value-> "setSensor: missing 'sensor' or 'value'"
    //   - "setSensor" unknown sensor  -> "setSensor: unknown sensor '...'"
  }

  void run_program(std::string source) {
    if (running_.exchange(true)) { emit_(R"({"type":"error","message":"already running"})"); return; }
    cancel_.store(false);
    worker_ = std::thread([this, src = std::move(source)] {
      JsonEventSink sink(emit_);
      cardcode::RunOptions opts; opts.cancel = &cancel_;
      cardcode::compile_and_run(src, robot_, sink, opts);  // emits diagnostics+events via sink+host
      running_.store(false);
    });
    worker_.detach();
  }

private:
  std::function<void(std::string)> emit_;
  ForwardingRobotHost robot_{emit_};
  std::atomic<bool> cancel_{false};
  std::atomic<bool> running_{false};
  std::thread worker_;
};
```

Notes:
- Run the engine on a **worker thread** so `cancel`/`setSensor` messages keep
  flowing while a program (with real `sleep`s) executes.
- `cancel_` is the `std::atomic<bool>*` the engine polls before each node — exactly
  the cancellation hook added in Milestone 4.
- `compile_and_run` emits `diagnostics` itself only if you wrap it; the sketch
  emits diagnostics from the `RunResult.diagnostics`. Or call `compile()` then
  `execute()` to control the `diagnostics`→`programStart` ordering precisely.
- `emit_` must be thread-safe: push onto a mutex/lock-free queue that the
  transport thread drains, preserving order.

## Transports

The bridge above is transport-agnostic. Pick how bytes move:

### (A) Dependency-free dev server  *(recommended to start)*

The browser UI uses WebSockets, while `cardcode-harness` uses newline-delimited
JSON on stdin/stdout or raw TCP. `tools/dev-server.js` is the local relay: it
serves `app/` over HTTP and starts one `cardcode-harness` process per WebSocket
connection at `/runtime`.

```bash
cmake -S . -B build
cmake --build build
npm run dev
```

The server listens on `0.0.0.0` by default. Open `http://localhost:9000/` from
the same machine, or use the machine's LAN IP from another device. The UI's mock
target defaults to same-origin `/runtime`, such as `ws://localhost:9000/runtime`.

Useful options:

```bash
npm run dev -- --port 9001
npm run dev -- --harness ./build/cardcode-harness --realtime
```

### (B) Other relays — TCP or stdio

The harness reads/writes **newline-delimited JSON** on a TCP socket (or
stdin/stdout). Browsers can't open raw TCP, so bridge to a WebSocket with a
small relay:

```bash
# websocketd exposes the harness over ws:// with zero glue code.
websocketd --port=8080 ./build/cardcode-harness

# Raw TCP is useful for scripts, but browser WebSocket cannot connect to it.
./build/cardcode-harness --port 8080
```

If you use `websocketd`, serve `app/` separately and connect the UI to
`ws://localhost:8080`.

### (C) In-process WebSocket (header-only library)
Link a header-only WS server (e.g. a single-header library you vendor into
`third_party/`) and have the harness speak `ws://` directly — no relay. The
adapters above are unchanged; only `emit_`/`handle` are wired to the library's
send/receive callbacks. Use this once you want a one-process `./cardcode-harness`
with no helper.

### (D) WASM (future) — no server at all
Compile the engine + the two adapters to WebAssembly (Emscripten). The
"transport" becomes a direct function-call boundary: the page calls an exported
`run(source)`; `emit_` calls a JS callback instead of a socket. Because the
adapters are pure C++ over the public API, the same `JsonEventSink` /
`ForwardingRobotHost` / `ProtocolBridge` compile to WASM unchanged — only
`main()`/transport is replaced by Emscripten bindings. This is the path to an
offline, serverless playground in the browser.

> All three transports carry the **same protocol messages**, so the UI code does
> not change between them.

## Actual layout

```
harness/
  json.hpp              # tiny hand-rolled JSON writer + parser (no deps)
  json_event_sink.hpp   # JsonEventSink   (ExecutionEvent -> protocol JSON)
  forwarding_host.hpp   # ForwardingRobotHost (RobotHost -> robotCommand/sensorRead)
  protocol_bridge.hpp   # ProtocolBridge  (parse control msgs, drive the engine)
  main.cpp              # stdio (default) + --port TCP
  # (transport C header-only WS / transport D WASM bindings drop in here later)
tests/test_harness.cpp  # drives ProtocolBridge inline (async=false) and asserts
                        # the emitted protocol messages
```

`json.hpp`, the sink, the host, and the bridge are header-only and shared with
the ESP32 build (see [esp32.md](esp32.md)); only `main.cpp` is desktop-specific.

Note on threading: `ProtocolBridge` runs each program on a worker thread and
joins it in its destructor (and reaps the prior finished run before starting a
new one), so events always flush before teardown. Tests construct it with
`async=false` for deterministic, inline execution.
