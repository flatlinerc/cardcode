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
the real one), wrap this in the shared `ForwardingRobotHost` pattern — but instead
of subclassing `MockRobotHost`, forward to an `Esp32RobotHost` member and emit the
JSON before delegating. (Sensors read the real device; `setSensor` is ignored or
used only as a bench override.)

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
      JsonEventSink sink(ws_emit);                 // ws_emit pushes to the WS sender
      ForwardingRobotHost robot(ws_emit);          // wraps Esp32RobotHost
      cardcode::RunOptions opts; opts.cancel = &g_cancel;
      cardcode::compile_and_run(source, robot, sink, opts);
      free(source);
    }
  }
}

// created with a generous stack — recursion + nested AST eval need headroom:
xTaskCreate(engine_task, "cardcode", /*stack*/ 8192, nullptr, 5, nullptr);
```

Sizing: the interpreter recurses (AST depth + function calls up to
`max_call_depth = 64`). Give the task several KB of stack and/or lower
`max_call_depth` in `ExecutionLimits` to fit your budget. Heap use during a run is
bounded: a per-call environment frame and per-event JSON string (see the root
README's hot-path note).

## 3. WebSocket transport (`esp_http_server`)

```cpp
#include "esp_http_server.h"

static int g_client_fd = -1;       // the connected UI socket
static httpd_handle_t g_server;

// URI handler for ws://<ip>/cardcode
static esp_err_t ws_handler(httpd_req_t* req) {
  if (req->method == HTTP_GET) {            // handshake
    g_client_fd = httpd_req_to_sockfd(req);
    return ESP_OK;
  }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  httpd_ws_recv_frame(req, &frame, 0);       // get length
  std::string msg(frame.len, '\0');
  frame.payload = reinterpret_cast<uint8_t*>(msg.data());
  httpd_ws_recv_frame(req, &frame, frame.len);

  protocol_bridge_handle(msg);               // run/cancel/setSensor/reset/compile
  return ESP_OK;
}
```

`run`/`compile` parse `msg`, push the source onto `g_run_queue` (run) or compile
inline and reply with `diagnostics`. `cancel` sets `g_cancel`.

**Sending events from the engine task.** `httpd_ws_send_frame_async` must run on
the httpd task, so marshal from the engine task with `httpd_queue_work`:

```cpp
void ws_emit(std::string json) {                 // called on the engine task
  auto* payload = new std::string(std::move(json));
  httpd_queue_work(g_server, [](void* arg){
    auto* p = static_cast<std::string*>(arg);
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = reinterpret_cast<uint8_t*>(p->data());
    f.len = p->size();
    if (g_client_fd >= 0) httpd_ws_send_frame_async(g_server, g_client_fd, &f);
    delete p;
  }, payload);
}
```

This preserves protocol ordering (the engine emits synchronously; work items run
FIFO on the httpd task). For higher throughput, replace the per-event `new` with a
ring buffer / FreeRTOS queue drained by a single sender.

## 4. Bring-up (`app_main`)

```cpp
extern "C" void app_main() {
  nvs_flash_init();
  wifi_connect();                       // STA (join the LAN) or SoftAP (robot is the AP)
  g_run_queue = xQueueCreate(1, sizeof(char*));
  start_httpd_with_ws("/cardcode", ws_handler, &g_server);
  xTaskCreate(engine_task, "cardcode", 8192, nullptr, 5, nullptr);
  // optionally serve the card UI's static files from SPIFFS on "/"
}
```

SoftAP mode is handy in a classroom: the robot is its own access point, the
student's browser joins it and opens the UI served from the device — no network
infrastructure.

## Parity checklist (robot == harness)

- Same engine build → identical deterministic node IDs and event ordering.
- Same `JsonEventSink` → identical `node` / `programError` messages.
- `ForwardingRobotHost` → identical `robotCommand` messages (the UI's on-screen
  robot mirrors the real one).
- Same `cancel`/limits semantics (Milestone 4) → identical stop-and-report
  behavior.

The only intended differences: sensors read real hardware (so `setSensor` is a
no-op/override), and actuator calls take real wall-clock time.
