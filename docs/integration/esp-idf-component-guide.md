# ESP-IDF Component Implementation Guide

**Converts the CardCode engine into a reusable ESP-IDF component** so any
ESP-IDF project can add `cardcode_engine` as a dependency with one line in
`idf_component.yml` or a `CMakeLists.txt` `EXTRA_COMPONENT_DIRS`.

This guide assumes you have read:
- [README.md](../../README.md) — the engine overview and embedded integration
- [protocol.md](protocol.md) — the JSON wire protocol
- [esp32.md](esp32.md) — the ESP32 firmware design sketch (this guide implements it)

---

## 1. Component layout

```
components/cardcode_engine/
  CMakeLists.txt              # idf_component_register wrapping the engine core
  include/
    cardcode/
      ast.hpp                 # copy from ../../include/cardcode/
      value.hpp
      lexer.hpp
      parser.hpp
      compiler.hpp
      engine.hpp
      events.hpp
      robot_host.hpp
      diagnostic.hpp
      source_span.hpp
      mock_robot_host.hpp
  src/
    lexer.cpp                 # copy from ../../src/
    parser.cpp
    compiler.cpp
    engine.cpp
```

The firmware-specific harness adapters (`JsonEventSink`, `ForwardingRobotHost`,
`ProtocolBridge`, `json.hpp`) live **outside** the component — they go in
`main/` alongside the concrete `Esp32RobotHost` and the websocket transport —
because they are firmware-specific wiring, not part of the reusable engine.

---

## 2. Component CMakeLists.txt

```cmake
# components/cardcode_engine/CMakeLists.txt
idf_component_register(
  SRCS "src/lexer.cpp"
       "src/parser.cpp"
       "src/compiler.cpp"
       "src/engine.cpp"
  INCLUDE_DIRS "include"
)
```

That is the entire file. ESP-IDF's CMake already sets `-fno-exceptions -fno-rtti`
by default, which matches the engine's requirements. No `CONFIG_COMPILER_CXX_EXCEPTIONS`
toggling is needed.

---

## 3. Adding to a project

### Option A: Managed component (idf_component.yml)

```yaml
# main/idf_component.yml
dependencies:
  cardcode_engine:
    path: components/cardcode_engine
```

### Option B: Extra component dirs

```cmake
# CMakeLists.txt (project root)
cmake_minimum_required(VERSION 5.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "components/cardcode_engine")
project(my_robot)
```

Either way, all public headers are available as `#include "cardcode/engine.hpp"`.

---

## 4. Firmware wiring (`main/`)

This follows the design in [esp32.md](esp32.md) exactly. The files below are
a complete, ready-to-customize reference.

### 4.1 `main/CMakeLists.txt`

```cmake
idf_component_register(
  SRCS "app_main.cpp"
       "robot_host_esp32.cpp"
       "ws_transport.cpp"
       "protocol_bridge_wrapper.cpp"
  INCLUDE_DIRS "."
)
```

### 4.2 `main/robot_host_esp32.hpp`

```cpp
#pragma once

#include "cardcode/robot_host.hpp"

class Esp32RobotHost : public cardcode::RobotHost {
public:
    void drive_forward(int speed, int duration_ms) override;
    void drive_backward(int speed, int duration_ms) override;
    void turn_left(int degrees) override;
    void turn_right(int degrees) override;
    void stop() override;
    void wait_ms(int ms) override;
    void set_light(cardcode::Color c) override;
    void beep() override;

    int  distance_cm() override;
    bool button_pressed() override;
    bool line_left() override;
    bool line_right() override;

private:
    void set_motors(int left, int right);
    void spin(int dir, int degrees);
    void write_rgb(cardcode::Color c);
};
```

### 4.3 `main/robot_host_esp32.cpp`

```cpp
#include "robot_host_esp32.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Stub implementations — replace with real driver calls for your hardware.
void Esp32RobotHost::drive_forward(int speed, int duration_ms) {
    set_motors(+speed, +speed);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_motors(0, 0);
}
void Esp32RobotHost::drive_backward(int speed, int duration_ms) {
    set_motors(-speed, -speed);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    set_motors(0, 0);
}
void Esp32RobotHost::turn_left(int degrees)  { spin(-1, degrees); }
void Esp32RobotHost::turn_right(int degrees) { spin(+1, degrees); }
void Esp32RobotHost::stop()       { set_motors(0, 0); }
void Esp32RobotHost::wait_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
void Esp32RobotHost::beep()       { /* buzzer_blip() — add a GPIO toggle */ }

void Esp32RobotHost::set_light(cardcode::Color c) { write_rgb(c); }

int  Esp32RobotHost::distance_cm()   { return 1000; /* hc_sr04_read_cm() */ }
bool Esp32RobotHost::button_pressed() { return false; /* gpio... */ }
bool Esp32RobotHost::line_left()      { return false; }
bool Esp32RobotHost::line_right()     { return false; }

void Esp32RobotHost::set_motors(int left, int right) {
    // Map -100..100 to LEDC duty + direction GPIO pins.
    (void)left; (void)right;
}
void Esp32RobotHost::spin(int dir, int degrees) {
    // Time- or IMU-based turn, blocking.
    (void)dir; (void)degrees;
}
void Esp32RobotHost::write_rgb(cardcode::Color c) {
    (void)c;
}
```

### 4.4 `main/ws_transport.hpp`

```cpp
#pragma once

#include <functional>
#include <string>

// Thread-safe emit: push a JSON event line onto the WebSocket output ring.
// Called from the engine task; schedules the actual send on the httpd task.
using WsEmit = std::function<void(const std::string&)>;

// Start the HTTP server with a /cardcode WebSocket handler.
// Returns the emit function the engine should use to push events.
WsEmit start_websocket_server(const char* uri);
```

### 4.5 `main/ws_transport.cpp`

```cpp
#include "ws_transport.hpp"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <string>

static httpd_handle_t    g_server   = nullptr;
static int               g_client_fd = -1;

// Ring buffer — one-slot-per-event, bounded, no heap allocation in the hot path.
static constexpr size_t kRingSize = 8;
static std::array<std::string, kRingSize> g_ring;
static std::atomic<uint8_t> g_head{0}, g_tail{0};

static void drain_one(httpd_handle_t, void*) {
    uint8_t t = g_tail.load(std::memory_order_relaxed);
    if (t == g_head.load(std::memory_order_acquire)) return;
    std::string& slot = g_ring[t];
    httpd_ws_frame_t frame = {};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(slot.data());
    frame.len     = slot.size();
    httpd_ws_send_frame_async(g_server, g_client_fd, &frame);
    slot.clear();
    g_tail.store((t + 1) % kRingSize, std::memory_order_release);
}

static void ws_emit(const std::string& json) {
    uint8_t h = g_head.load(std::memory_order_relaxed);
    uint8_t next = (h + 1) % kRingSize;
    if (next == g_tail.load(std::memory_order_acquire)) return; // drop on full
    g_ring[h] = json;
    g_head.store(next, std::memory_order_release);
    httpd_queue_work(g_server, drain_one, nullptr);
}

static esp_err_t ws_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        g_client_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
    if (frame.type != HTTPD_WS_TYPE_TEXT)              return ESP_FAIL;
    if (frame.len == 0 || frame.len > 8192)             return ESP_FAIL;

    std::string msg(frame.len, '\0');
    frame.payload = reinterpret_cast<uint8_t*>(msg.data());
    if (httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) return ESP_FAIL;

    // Forward the message to the protocol bridge.
    extern void protocol_bridge_handle(const std::string&);
    protocol_bridge_handle(msg);
    return ESP_OK;
}

WsEmit start_websocket_server(const char* uri) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 8;

    httpd_start(&g_server, &cfg);
    httpd_uri_t ws_uri = {
        .uri      = uri,
        .method   = HTTP_GET,
        .handler  = ws_handler,
        .user_ctx = nullptr,
        .is_websocket = true,
    };
    httpd_register_uri_handler(g_server, &ws_uri);
    return ws_emit;
}
```

### 4.6 `main/protocol_bridge_wrapper.hpp`

```cpp
#pragma once

#include <string>

void protocol_bridge_handle(const std::string& message);
```

### 4.7 `main/protocol_bridge_wrapper.cpp`

```cpp
// Thin wrapper: instantiates the harness adapters (JsonEventSink, protocol
// parsing) with the ESP32 websocket emit function and the real robot host.
//
// Reuses the shared adapter headers from the desktop harness.

#include "protocol_bridge_wrapper.hpp"

#include <atomic>

#include "cardcode/engine.hpp"

#include "json.hpp"                    // from harness/
#include "json_event_sink.hpp"        // from harness/
#include "ws_transport.hpp"

#include "robot_host_esp32.hpp"
#include <utility>
#include <vector>

using namespace cardcode::harness;

namespace {

Esp32RobotHost g_robot;
WsEmit         g_emit;    // set once during app_main

std::atomic<bool> g_cancel{false};
std::atomic<bool> g_running{false};

void do_run(const std::string& source, const JsonValue& msg) {
    if (g_running.exchange(true)) {
        g_emit(R"({"type":"error","message":"a program is already running"})");
        return;
    }

    g_cancel.store(false);

    // Parse optional limits from the run message.
    cardcode::ExecutionLimits limits;
    if (const JsonValue* l = msg.find("limits")) {
        if (auto* x = l->find("maxTotalSteps"))  limits.max_total_steps  = x->as_int();
        if (auto* x = l->find("maxRepeatCount")) limits.max_repeat_count = x->as_int();
        if (auto* x = l->find("maxCallDepth"))   limits.max_call_depth   = x->as_int();
    }

    // Compile.
    cardcode::CompileResult compiled = cardcode::compile(source);
    emit_diagnostics(g_emit, compiled);

    if (compiled.ok()) {
        JsonEventSink sink(g_emit);
        auto result = cardcode::execute(*compiled.root, g_robot, sink, limits, &g_cancel);
        (void)result;
    }

    g_running.store(false);
}

void do_compile(const std::string& source) {
    auto compiled = cardcode::compile(source);
    emit_diagnostics(g_emit, compiled);
}

} // namespace

void protocol_bridge_handle(const std::string& message) {
    auto parsed = json_parse(message);
    if (!parsed || !parsed->is_obj()) {
        g_emit(R"({"type":"error","message":"malformed message"})");
        return;
    }
    const JsonValue& msg = *parsed;

    const JsonValue* type = msg.find("type");
    if (!type || type->type != JsonValue::Type::Str) {
        g_emit(R"({"type":"error","message":"missing 'type'"})");
        return;
    }
    const std::string t = type->str;

    const JsonValue* src = msg.find("source");
    if (t == "compile") {
        if (!src) { g_emit(R"({"type":"error","message":"compile: missing 'source'"})"); return; }
        do_compile(src->as_str());
    } else if (t == "run") {
        if (!src) { g_emit(R"({"type":"error","message":"run: missing 'source'"})"); return; }
        do_run(src->as_str(), msg);
    } else if (t == "cancel") {
        g_cancel.store(true);
    } else if (t == "reset") {
        // No-op on device — real hardware state isn't reset by a control message.
    } else {
        g_emit(R"({"type":"error","message":"unknown message type ')" + t + R"('"})");
    }
}
```

### 4.8 `main/app_main.cpp`

```cpp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

extern void wifi_connect(void);
extern WsEmit start_websocket_server(const char* uri);
extern Esp32RobotHost g_robot;  // defined in protocol_bridge_wrapper.cpp

// The engine task: runs one program at a time, pulled from a queue.
// (Simplified here; a production build uses a queue. For the common case,
// the protocol bridge wrapper handles everything inline via the WS handler.)
//
// Full queue-based design is shown in docs/integration/esp32.md §2.

extern "C" void app_main() {
    nvs_flash_init();
    wifi_connect();               // STA (join LAN) or SoftAP (robot is AP)

    start_websocket_server("/cardcode");   // sets g_emit and g_server

    // The protocol bridge runs inline in the WS handler (synchronous compile,
    // then execute on the calling task). If you want the engine on a dedicated
    // task (recommended), push source to a queue as shown in esp32.md §2.
    //
    // For now, the ws_handler calls protocol_bridge_handle() directly on the
    // httpd task. With short programs (<1000 steps) this is fine; for longer
    // runs the httpd task is blocked during execution and can't service other
    // clients or send events. In that case, use the queue pattern.
}
```

---

## 5. Build & flash

```bash
# 1. Place the component
mkdir -p components/cardcode_engine/src components/cardcode_engine/include/cardcode
cp src/*.cpp              components/cardcode_engine/src/
cp include/cardcode/*.hpp components/cardcode_engine/include/cardcode/
cp CMakeLists.txt         components/cardcode_engine/CMakeLists.txt   # from §2

# 2. Copy the harness adapters (shared)
cp harness/json.hpp            main/
cp harness/json_event_sink.hpp main/

# 3. Copy the firmware files (from §4)
cp main/* main/

# 4. Configure and build
idf.py set-target esp32s3        # or your chip
idf.py menuconfig                # set WiFi SSID/password or SoftAP config
idf.py build
idf.py flash monitor
```

---

## 6. Verification

### 6.1 Compile check

```bash
idf.py build
```

If it compiles, the component is correctly wired. The engine itself has no
external dependencies beyond the C++ standard library and FreeRTOS (which
ESP-IDF provides).

### 6.2 Smoke test the WebSocket

Find the device IP from the serial monitor, then:

```bash
# Install websocat (or use any WS client)
cargo install websocat
echo '{"type":"compile","source":"(beep)"}' | websocat -U ws://<robot-ip>/cardcode
```

Expected response (one JSON line per event):

```json
{"type":"diagnostics","ok":true,"diagnostics":[]}
```

### 6.3 Run a program

```bash
echo '{"type":"run","source":"(repeat 3 (drive 40 1000) (turn-right 90))"}' \
  | websocat -U ws://<robot-ip>/cardcode
```

Expected sequence: `diagnostics` → `programStart` → `node` events → `programDone`.
If motors are wired, the robot drives a square.

### 6.4 Cancel mid-program

```bash
# In one terminal:
echo '{"type":"run","source":"(repeat 100 (drive 80 2000) (turn-right 90))"}' \
  | websocat -U ws://<robot-ip>/cardcode

# In another:
echo '{"type":"cancel"}' | websocat -U ws://<robot-ip>/cardcode
```

The robot should stop, and the first terminal should show a `programError`
with message `"execution cancelled"`.

---

## 7. Checklist

- [ ] Component builds with `idf.py build` (no linker errors)
- [ ] `/cardcode` WebSocket handshake succeeds (`HTTP_GET`)
- [ ] `compile` returns diagnostics JSON
- [ ] `run` executes a program and emits `programStart`/`programDone`
- [ ] `cancel` stops execution and the robot halts
- [ ] Engine invariants hold (no exceptions, no RTTI, no `<iostream>`)
- [ ] Task Watchdog does not trip during long actuator calls (§1 safety note)
- [ ] Single-client constraint observed (esp32.md §4)

---

## 8. Design decisions

| Decision | Rationale |
|---|---|
| **Engine as a standalone component** | Any ESP-IDF project can add it without forking. The engine has zero IDF dependencies. |
| **Harness adapters in `main/`, not the component** | The adapters (`JsonEventSink`, `ProtocolBridge`, `json.hpp`) are firmware-specific wiring — they decide which transport and which RobotHost to use. The component is just the pure engine. |
| **Ring buffer instead of per-event `new`/`delete`** | Avoids heap fragmentation on the hot path. 8 slots fit in BSS. |
| **Inline execution on httpd task (simplified)** | The simplest integration for short programs. For production, use the queue-based approach from esp32.md §2 to keep httpd responsive during long runs. |
| **No `ForwardingRobotHost` from harness reuse** | The desktop harness's forwarding host no-ops actuators by default; on real hardware the motors must move. Esp32.md §1 shows the correct wrapper. |

---

## 9. Production hardening (future)

- Move execution to a dedicated FreeRTOS task with a queue (esp32.md §2)
- Add `WDT` feeding for actuator calls longer than the TWDT period
- Serve the CardCode UI static files from SPIFFS on `/`
- Multi-client support (broadcast events or per-client ring buffers)
- `setSensor` override support (the `Esp32ForwardingRobotHost` pattern from esp32.md §1)
