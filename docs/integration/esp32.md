# ESP32 Firmware (ESP-IDF)

Run the engine on the robot and host the [wire protocol](protocol.md) from the
device itself, so the **same card UI** connects to `ws://<robot-ip>/cardcode` and
highlights cards exactly as it does against the desktop harness.

The engine core already builds for ESP32: it is exception/RTTI-free and uses no
host-only headers (see the root README's *Embedded integration* and build with
`-DCARDCODE_EMBEDDED=ON`, which mirrors the ESP-IDF defaults `-fno-exceptions
-fno-rtti`). What firmware adds is: a real `RobotHost`, a FreeRTOS task to run
programs, and an `esp_http_server` websocket transport. The
`JsonEventSink` / `ForwardingRobotHost` / `ProtocolBridge` adapters from
[local-harness.md](local-harness.md) are reused **unchanged**.

## Component layout

```
components/cardcode_engine/      # the library, as an ESP-IDF component
  CMakeLists.txt                 # idf_component_register wrapping src/*.cpp + include/
main/
  app_main.cpp                   # wifi + httpd + engine task
  robot_host_esp32.{hpp,cpp}     # concrete RobotHost over drivers
  ws_transport.cpp               # esp_http_server websocket <-> ProtocolBridge
  # json.hpp, json_event_sink.hpp, forwarding_host.hpp, protocol_bridge.hpp
  # are shared with the desktop harness
```

`components/cardcode_engine/CMakeLists.txt`:
```cmake
idf_component_register(
  SRCS "src/lexer.cpp" "src/parser.cpp" "src/compiler.cpp" "src/engine.cpp"
  INCLUDE_DIRS "include")
# ESP-IDF C++ is built -fno-exceptions -fno-rtti by default; the engine is clean
# under both. Set CONFIG_COMPILER_CXX_EXCEPTIONS=n (default) in sdkconfig.
```

## 1. A real `RobotHost`

Implement the same interface the mock implements, over device drivers. Actuator
calls with a duration **block** (the engine is synchronous): run the motor, delay,
stop. Sensors read hardware.

```cpp
#include "cardcode/robot_host.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// driver headers: driver/ledc.h (motor PWM), driver/gpio.h, an HC-SR04 helper…

class Esp32RobotHost : public cardcode::RobotHost {
public:
  void drive_forward(int speed, int duration_ms) override {
    set_motors(+speed, +speed);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_motors(0, 0);
  }
  void drive_backward(int speed, int duration_ms) override {
    set_motors(-speed, -speed);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_motors(0, 0);
  }
  void turn_left(int degrees) override  { spin(-1, degrees); }
  void turn_right(int degrees) override { spin(+1, degrees); }
  void stop() override { set_motors(0, 0); }
  void wait_ms(int ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  void set_light(cardcode::Color c) override { write_rgb(c); }
  void beep() override { buzzer_blip(); }

  int  distance_cm() override   { return hc_sr04_read_cm(); }
  bool button_pressed() override{ return gpio_get_level(kButtonGpio) == 0; }
  bool line_left() override     { return gpio_get_level(kLineLeftGpio) == 0; }
  bool line_right() override    { return gpio_get_level(kLineRightGpio) == 0; }

private:
  void set_motors(int left, int right);     // map -100..100 to LEDC duty + dir pins
  void spin(int dir, int degrees);          // time- or IMU-based turn, blocking
  void write_rgb(cardcode::Color c);
  // …
};
```

To also stream `robotCommand` messages to the UI (so the on-screen robot mirrors
the real one), wrap `Esp32RobotHost` in a thin forwarding host that emits the
JSON before delegating to the real driver:

```cpp
// Illustrative — not compiled into the firmware. Mirrors the shape of the
// desktop harness's ForwardingRobotHost (harness/forwarding_host.hpp), but
// every actuator call hits real hardware instead of no-op'ing.
class Esp32ForwardingRobotHost : public cardcode::RobotHost {
public:
  using Emit = std::function<void(const std::string&)>;

  Esp32ForwardingRobotHost(Esp32RobotHost& real, Emit emit)
      : real_(real), emit_(std::move(emit)) {}

  void reset() override { /* no-op, or real_.reset() if you define one */ }

  // Actuators: emit a robotCommand, then run the motors for real.
  void drive_forward(int s, int ms) override {
    command("drive_forward", {{"speed", jnum(s)}, {"durationMs", jnum(ms)}});
    real_.drive_forward(s, ms);
  }
  void drive_backward(int s, int ms) override {
    command("drive_backward", {{"speed", jnum(s)}, {"durationMs", jnum(ms)}});
    real_.drive_backward(s, ms);
  }
  void turn_left(int deg) override {
    command("turn_left",  {{"degrees", jnum(deg)}});
    real_.turn_left(deg);
  }
  void turn_right(int deg) override {
    command("turn_right", {{"degrees", jnum(deg)}});
    real_.turn_right(deg);
  }
  void stop() override {
    command("stop", {});
    real_.stop();
  }
  void wait_ms(int ms) override {
    command("wait", {{"durationMs", jnum(ms)}});
    real_.wait_ms(ms);
  }
  void set_light(cardcode::Color c) override {
    command("set_light", {{"color", jstr(color_name(c))}});
    real_.set_light(c);
  }
  void beep() override {
    command("beep", {});
    real_.beep();
  }

  // Sensors: read hardware, then echo the value as a sensorRead event.
  int  distance_cm() override {
    int v = real_.distance_cm();
    sensor("distance-cm", jnum(v));
    return v;
  }
  bool button_pressed() override {
    bool v = real_.button_pressed();
    sensor("button", jbool(v));
    return v;
  }
  bool line_left() override {
    bool v = real_.line_left();
    sensor("line-left", jbool(v));
    return v;
  }
  bool line_right() override {
    bool v = real_.line_right();
    sensor("line-right", jbool(v));
    return v;
  }

private:
  void command(const char* name, std::initializer_list<JField> args) {
    emit_(jobj({{"type",    jstr("robotCommand")},
                {"command", jstr(name)},
                {"args",    jobj(args)}}));
  }
  void sensor(const char* name, const std::string& value_json) {
    emit_(jobj({{"type",   jstr("sensorRead")},
                {"sensor", jstr(name)},
                {"value",  value_json}}));
  }

  Esp32RobotHost& real_;
  Emit            emit_;
};
```

**Why not reuse the shared `cardcode::harness::ForwardingRobotHost`?** Its
actuator overrides no-op by default — the `delay` it calls only sleeps when
constructed with `realtime=true` (which defaults to `false`). On a real ESP32,
instantiating it would mean the wheels never move: the UI would see the
`robotCommand`, the robot would not respond. The wrapper above calls the real
driver unconditionally. (`setSensor` is ignored or used only as a bench
override, since the real hardware wins.)

### Safety: the task watchdog
Long blocking actuator calls (`drive 100 10000` = 10 s) must not trip the Task
Watchdog. Either run the engine task **without** TWDT subscription, or split long
delays into chunks and feed the WDT. The engine's own limits already bound things
(`max_total_steps`, `max_call_depth`, per-command range caps; duration ≤ 10 000 ms).

## 2. Run the engine in a FreeRTOS task

One program at a time. The websocket handler hands the source to a dedicated
engine task; cancellation flips an atomic the engine polls.

```cpp
static std::atomic<bool> g_cancel{false};
static QueueHandle_t     g_run_queue;   // holds heap-owned source strings

static void engine_task(void*) {
  for (;;) {
    char* source = nullptr;
    if (xQueueReceive(g_run_queue, &source, portMAX_DELAY) == pdTRUE) {
      g_cancel.store(false);
      // Compile first; if it fails, surface the diagnostics over the WS and
      // do NOT call execute (no programStart/programDone on a broken program).
      CompileResult compiled = cardcode::compile(source);
      ws_emit_diagnostics(compiled);     // builds {"type":"diagnostics", ...}
      if (compiled.ok()) {
        Esp32ForwardingRobotHost robot(g_esp32_robot, ws_emit);
        JsonEventSink sink(ws_emit);     // ws_emit pushes to the WS sender
        RunOptions opts; opts.cancel = &g_cancel;
        cardcode::execute(*compiled.root, robot, sink, limits, &g_cancel);
      }
      free(source);
    }
  }
}

// 8192 words = 32 KB on a 32-bit core (sizeof(StackType_t) == 4). The
// interpreter is recursive (see ExecutionLimits::max_call_depth), so 32 KB
// is the safe headroom; lower this only after measuring peak stack.
xTaskCreate(engine_task, "cardcode", /*stack (words)*/ 8192, nullptr, 5, nullptr);
```

Sizing: `usStackDepth` is in *words* on ESP-IDF, so the call above allocates
**32 KB** of stack for the engine task. The interpreter recurses (AST depth +
function calls up to `max_call_depth = 64`), and a 32 KB budget is the
realistic floor — shrink only after measuring peak stack with
`uxTaskGetStackHighWaterMark`. Heap use during a run is bounded: a per-call
environment frame and per-event JSON string (see the root README's hot-path
note).

## 3. WebSocket transport (`esp_http_server`)

**IDF 6.0.1 note:** the fd-retrieval API is unchanged. `httpd_req_to_sockfd()`
is the canonical way to get the socket fd from a request — it is fully
documented in `esp_http_server.h`, is referenced by other docblocks in that
header as the right way to do it, and has **no** deprecation marker. There is
no new `httpd_req_t` field to read instead. Keep using it. (Full reference:
`docs/integration/idf-6.0.1-http-server-reference.md`.)

```cpp
#include "esp_http_server.h"

static int g_client_fd = -1;       // the connected UI socket (single slot — see below)
static httpd_handle_t g_server;

// URI handler for ws://<ip>/cardcode
static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {            // handshake
    g_client_fd = httpd_req_to_sockfd(req);
    return ESP_OK;
  }

  // Read the frame header (no payload yet). The default WS RX buffer is
  // CONFIG_ESP_HTTPS_SERVER_RX_BUFFER_SIZE (1024 bytes in stock IDF 6.0.1),
  // so a single client message can be delivered as one text frame (the v1
  // case) or as multiple fragmented/continued frames (a hostile or large
  // client). The cardcode v1 protocol only ever sends single-frame text
  // messages, so we reject anything else.
  httpd_ws_frame_t frame = {};      // = {} zero-inits the new IDF 6 bool
                                    // fields (final, fragmented) to false
  if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
  if (frame.type != HTTPD_WS_TYPE_TEXT) {
    return ESP_FAIL;                // close: binary/ping/close/continue
  }
  if (frame.len == 0 || frame.len > 8192) {
    return ESP_FAIL;                // cap: refuse empty or oversized source
  }

  std::string msg(frame.len, '\0');
  frame.payload = reinterpret_cast<uint8_t*>(msg.data());
  if (httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) return ESP_FAIL;

  protocol_bridge_handle(msg);               // run/cancel/setSensor/reset/compile
  return ESP_OK;
}
```

`run`/`compile` parse `msg`, push the source onto `g_run_queue` (run) or compile
inline and reply with `diagnostics`. `cancel` sets `g_cancel`.

**Sending events from the engine task.** `httpd_ws_send_frame_async` must run on
the httpd task, so marshal from the engine task with `httpd_queue_work`. The
naive `new std::string` + `delete` in the callback allocates on the producer
path for every event, which fragments the heap; under a tight `while` loop
(`max_total_steps = 1000` can produce 1000 events in seconds), `heap_caps_get_
min_free_size(MALLOC_CAP_8BIT)` will drop and stay low. Use a small fixed-
capacity ring of `std::string` (move-only) in static storage instead — the
producer path then has bounded memory and **no allocation at all**:

```cpp
// 8-slot ring of move-only strings. Producer: engine task. Consumer: httpd task.
static std::array<std::string, 8> g_ring;
static std::atomic<uint8_t> g_head{0}, g_tail{0};

static void drain_one(httpd_handle_t, void* /*arg*/) {
    // Pull one slot, hand it to httpd_ws_send_frame_async for the live client.
    // If the slot is empty, no-op. If the client is gone, no-op.
    uint8_t t = g_tail.load(std::memory_order_relaxed);
    if (t == g_head.load(std::memory_order_acquire)) return;
    // ... build httpd_ws_frame_t from g_ring[t], send, advance tail ...
}

void ws_emit(std::string json) {
    uint8_t h = g_head.load(std::memory_order_relaxed);
    uint8_t next = (h + 1) % g_ring.size();
    if (next == g_tail.load(std::memory_order_acquire)) {
        return;  // ring full; drop (or overwrite oldest — pick a policy)
    }
    g_ring[h] = std::move(json);
    g_head.store(next, std::memory_order_release);
    httpd_queue_work(g_server, &drain_one, nullptr);
}
```

**Trade-off — pick an overflow policy and pin it.** A ring drops events on
overflow (fine for telemetry like `sensorRead`; bad for diagnostics like
`programError`). The snippet above drops new events; if you prefer to drop the
oldest, advance the tail in the same critical section. The current snippet
also never sends the same slot twice: `drain_one` pulls exactly one entry per
invocation, so protocol ordering is preserved (work items run FIFO on the
httpd task, same as the naive version).

**IDF 6.0.1 note:** `httpd_queue_work()` is still the right API to marshal a
callback onto the httpd task — the signature is unchanged from IDF 5.x and
there is no `esp_workqueue` component to substitute. The full reference is at
`docs/integration/idf-6.0.1-http-server-reference.md`.

## 4. Bring-up (`app_main`)

```cpp
extern "C" void app_main() {
  nvs_flash_init();
  wifi_connect();                       // STA (join the LAN) or SoftAP (robot is the AP)
  g_run_queue = xQueueCreate(1, sizeof(char*));
  start_httpd_with_ws("/cardcode", ws_handler, &g_server);
  xTaskCreate(engine_task, "cardcode", /*stack (words)*/ 8192, nullptr, 5, nullptr);
  // optionally serve the card UI's static files from SPIFFS on "/"
}
```

SoftAP mode is handy in a classroom: the robot is its own access point, the
student's browser joins it and opens the UI served from the device — no network
infrastructure.

**Single-client constraint.** `g_client_fd` is a single slot. Only one UI
connects at a time. Connecting a new client mid-run takes over the live
stream — the in-flight program continues to run, and the new client does not
receive any history (no replay of the `node`/`programError` events that
already happened). A second browser tab to the same URL is enough to
demonstrate the takeover. The default WS RX buffer is 1024 bytes
(`CONFIG_ESP_HTTPS_SERVER_RX_BUFFER_SIZE`); messages under that size arrive
as a single text frame, which is all the v1 protocol ever sends.

## Parity checklist (robot == harness)

- Same engine build → identical deterministic node IDs and event ordering.
- Same `JsonEventSink` → identical `node` / `programError` messages.
- `Esp32ForwardingRobotHost` (see §1) → identical `robotCommand` messages
  (the UI's on-screen robot mirrors the real one).
- Same `cancel`/limits semantics (Milestone 4) → identical stop-and-report
  behavior.
- Single live client at a time → see the bring-up note above; multi-client
  multiplexing is a future workstream.

The only intended differences: sensors read real hardware (so `setSensor` is a
no-op/override), and actuator calls take real wall-clock time.
