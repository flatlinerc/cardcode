# Doc-vs-Code Fixes — Handoff

**Date:** 2026-06-07
**For:** the next agent picking up documentation work
**Read first:** the original review at the end of this document (the "Findings" section)
**Status:** APPLIED 2026-06-07. All 14 items below have been applied to the docs and (for item 1.2 option A) to the engine. A "Verification & corrections" section at the end of this document captures corrections from a fresh verification pass.

## Target environment (pin this)

- **ESP-IDF:** v6.0.1 (this is what the firmware will build against)
- **MCU:** ESP32 family — assume a generic ESP32 unless the integrator picks an ESP32-S2/S3/C3 variant
- **C++:** C++23 (matches the engine in `CMakeLists.txt:4`; ESP-IDF 6 builds C++20 by default, the engine headers are C++23-clean — set `CONFIG_COMPILER_CXX_STANDARD=23` in `sdkconfig.defaults`)
- **Build target:** the engine core compiled with `-fno-exceptions -fno-rtti` (IDF 6 defaults; mirrors `CARDCODE_EMBEDDED=ON`)
- **FreeRTOS:** IDF 6 ships FreeRTOS kernel V11 — `configSTACK_DEPTH_TYPE` is `uint32_t`; `xTaskCreate`/`xTaskCreatePinnedToCore` still take `usStackDepth` in *words* (4 bytes on a 32-bit core)

### IDF 6.0.1 API surface for this handoff

The handoff uses five `esp_http_server` APIs. All five are still present in IDF 6.0.1 with the same signatures. The only API change that affects `esp32.md` is two new `bool` fields on `httpd_ws_frame_t`. A full reference of the actual header is at `docs/integration/idf-6.0.1-http-server-reference.md`; the table below is the bottom line.

| API in `esp32.md` | Status in IDF 6.0.1 | Action |
|---|---|---|
| `httpd_queue_work` | Present, unchanged | Keep as written. There is no `esp_workqueue` component in 6.0.1. |
| `httpd_ws_send_frame_async` | Present, unchanged | Keep as written. |
| `httpd_ws_recv_frame` | Present, unchanged | Keep as written. |
| `httpd_req_to_sockfd` | Present, **not** deprecated | Keep as written. |
| `httpd_ws_frame_t` | Two new `bool` fields: `final`, `fragmented` | `= {}` zero-init still works (item 3.4). |

**Why this matters:** the `esp32.md` sketch in item 3.5 calls `httpd_queue_work(...)` from a producer task to marshal events onto the httpd task. That pattern is still the recommended one in IDF 6.0.1. Do not rewrite the sketch to use a non-existent `esp_workqueue` API.

**Source of truth for verification:** the raw `esp_http_server.h` from the `v6.0.1` branch (`docs/integration/idf-6.0.1-http-server-reference.md` has the full quoted section, plus the canonical URL). The official Espressif 6.0.1 release notes are not available as static markdown — the GitHub release page is a stub, and `release-notes.espressif.tools` is a client-rendered Next.js app. If you need to verify an API that is **not** in the table above, the API reference page is at `https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/protocols/esp_http_server.html` and the full header is at `https://raw.githubusercontent.com/espressif/esp-idf/v6.0.1/components/esp_http_server/include/esp_http_server.h`.

## What this handoff is

A previous agent reviewed `docs/` against the C++ sources and the harness, and produced a list of places where the docs contradict the code. This document turns each finding into a concrete change with a verification step. The work is doc-only except for items marked **[code change]**, which require touching `src/` or `harness/`.

The items are grouped by file. Within a group, they are ordered roughly by "this one will trip a real integrator first." Do them top to bottom.

## How to verify

After making a change, run the relevant subset of:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/cardcode-harness           # smoke-test the line protocol
./build/cardcode-run examples/patrol.ccode   # smoke-test the host CLI
```

The test suite is the canonical source of truth for behavior; the docs should match it.

---

## Group 1 — `docs/integration/protocol.md`

### 1.1  Add the missing `error` host-to-client message

**Where:** host→client section, after the `programError` block (around `protocol.md:147`).

**What to write:** the `error` message is emitted on the *request* thread (not the worker), so it can interleave with a `run` that is already in progress.

```json
{ "type": "error", "message": "a program is already running" }
```

Add an entry describing the conditions under which `error` is emitted:
- Malformed JSON
- Missing `type` field
- Missing required field for a typed message (e.g. `run` without `source`)
- `run` rejected because a program is already running
- `setSensor` with an unknown sensor name
- Unknown message `type`

**Why:** the harness emits `{"type":"error","message":"…"}` from `harness/protocol_bridge.hpp:127-129` for all the above cases, and the test at `tests/test_harness.cpp:161-167` pins the wire format. The protocol doc does not list `error` at all.

**Verify:** the test `malformed and unknown messages report an error, not a crash` still passes, and the new doc text describes exactly the cases that test covers plus the `setSensor` and "already running" cases from `protocol_bridge.hpp:91-101` and `:70`.

### 1.2  Resolve the `NodeSkipped` mismatch

**Where:** `protocol.md:100` (the `node` event description) and `protocol.md:111-113` (the `event` mapping table).

**Two options.** Pick one, do not do both:

**(A) Emit `NodeSkipped` from the engine** (recommended — keeps the protocol doc honest and gives the UI something to grey-out a card with). Touch only `src/engine.cpp`:

In `eval_inner` for `NodeKind::When` (`src/engine.cpp:141-152`), if `cond.truthy()` is false, emit `NodeSkipped` for each child before skipping it. Likewise for the un-taken branch of `NodeKind::If` (`src/engine.cpp:154-158`). The simplest spot is a small helper `void emit_skipped(const Node& n, ExecState& st)` that walks the children and emits `NodeSkipped` with the child's `id` and `span`. Do not call `eval()` on the skipped children — they must not run.

Then update `tests/test_harness.cpp:134-140` to assert that a `NodeSkipped` is present when the `when` body is skipped. Use `c.of_type("node")` and check at least one entry has `event == "skipped"`.

**(B) Drop `NodeSkipped` from the protocol.** Edit:
- `include/cardcode/events.hpp:16` — remove the enumerator
- `harness/json_event_sink.hpp:35` — remove the case
- `protocol.md:100,111-113` — remove "skipped" everywhere

Option B is smaller but throws away a useful signal. Prefer A.

**Why:** the engine never emits `NodeSkipped` today (`src/engine.cpp` only emits `NodeStart`/`NodeDone` at lines 330 and 336), but the enum, the sink, and the protocol doc all list it. Three places out of sync.

**Verify (option A):** with `setSensor distance-cm = 100`, run `(when (< (distance-cm) 20) (light red))` and check the harness emits a `node` message with `event: "skipped"`.

### 1.3  Show the implicit `stop` in the cancellation trace

**Where:** `protocol.md:154-156` and the "complete `run` exchange" example near the bottom of the file.

**What to add:** the `programError` block should note that the implicit `stop()` arrives as a `robotCommand("stop")` between the last in-flight `node done` and the `programError`. The example trace should include it.

**Why:** `src/engine.cpp:359` and `:365` call `robot.stop()` before `ProgramError`. The harness's `ForwardingRobotHost::stop()` (`forwarding_host.hpp:55`) emits `robotCommand("stop", {})`. So the actual wire stream is `… robotCommand("stop") … programError`. The doc's trace ends with `programError` and skips the stop.

**Verify:** run any program, send `cancel` mid-flight, and observe the sequence in the harness's stdout.

---

## Group 2 — `docs/integration/local-harness.md`

### 2.1  Fix the `ForwardingRobotHost` sketch

**Where:** `local-harness.md:111-136`.

**What to change:** the sketch derives from `MockRobotHost`. The real class derives from `RobotHost`. Update the sketch to match. Specifically:

- Line 114: `class ForwardingRobotHost : public cardcode::MockRobotHost` → `: public cardcode::RobotHost`.
- Add `void reset()` and the four `set_distance`/`set_button`/`set_line_left`/`set_line_right` setters as members of the class (the real one stores them in `std::atomic`s; the sketch can use plain fields since the doc says "Illustrative").
- The override of each actuator should `command(...)` then `delay(...)`, where `delay` is a private helper that does nothing in the sketch (or no-ops without a `realtime_` flag — the real one has a flag).
- Sensors should `emit_(sensorRead …)` and return a value from the field. The real one emits the `sensorRead` and returns the value as a single expression.

**Why:** the sketch would not compile against the actual headers if a reader copy-pasted it; more importantly, the inheritance is the load-bearing claim that this class is the harness-side host, not a `MockRobotHost` decorator.

**Verify:** the new sketch should match the structure of `harness/forwarding_host.hpp:20-92` line for line (modulo the realtime flag).

### 2.2  Rename the JSON helpers in the `JsonEventSink` sketch

**Where:** `local-harness.md:64-92` (the "JsonEventSink" code block).

**What to change:** `obj(...)` → `jobj(...)` and `span_json(...)` → `jspan(...)`. Also, the `node_json` function in the sketch builds a vector of fields; rename to `jobj_v` to match the real `harness/json.hpp:55-67`.

**Why:** the names `obj` and `span_json` do not exist in `harness/json.hpp`. The real helpers are `jobj` (initializer-list form) and `jobj_v` (vector form, for ordered output), plus `jspan` for the span object. The fields and order in the sketch are otherwise correct.

**Verify:** grep the doc for `obj(` — the only remaining hit should be inside prose, not code.

### 2.3  Document the wire `error` cases in the bridge sketch

**Where:** `local-harness.md:154-160` (the `ProtocolBridge::handle` sketch comments).

**What to add:** the actual dispatcher in `harness/protocol_bridge.hpp:42-57` does more than the sketch lists. Update the comment block to mention:
- Malformed JSON → `error("malformed message")`
- Missing `type` field → `error("missing 'type'")`
- Unknown `type` → `error("unknown message type '…'")`

**Why:** same as 1.1. The doc sketches a happy-path only; the harness is defensive and emits `error` for the unhappy path.

**Verify:** see 1.1.

---

## Group 3 — `docs/integration/esp32.md`

### 3.1  Replace `ForwardingRobotHost` with a real `Esp32ForwardingRobotHost`

**Where:** `esp32.md:81-85` (the wrapper-pattern paragraph) and `esp32.md:107-108` (the engine task snippet).

**What to add:** a new class (call it `Esp32ForwardingRobotHost`) that:
- Inherits from `cardcode::RobotHost`
- Holds an `Esp32RobotHost&` member (the real one) and a `ws_emit` callback
- For each actuator, calls `ws_emit(robotCommand …)`, then `real_.drive_forward(s, ms)` etc.
- For each sensor, calls `real_.distance_cm()` etc., then `ws_emit(sensorRead …)`, then returns the value
- Has a no-op `reset()` (or a real one that does `real_.reset()` if you define one)

Update the engine task snippet to construct this wrapper, not the desktop `ForwardingRobotHost`.

**Why:** the desktop `ForwardingRobotHost` does not drive any hardware. Its `drive_forward` emits the `robotCommand` and then sleeps for the duration (`harness/forwarding_host.hpp:41-47`). On a real ESP32, instantiating it would mean the wheels never move. The doc's own paragraph at `esp32.md:81-85` flags this, but the snippet on `:107-108` doesn't reflect it.

**Verify:** copy the new class into a sketch, then mentally trace `(drive 40 1000)`: the snippet should produce a `robotCommand("drive_forward", …)` *and* actually run the motors for 1 s. The current sketch produces the `robotCommand` and sleeps — motors never run.

### 3.2  Make compile-time diagnostics reach the WebSocket

**Where:** `esp32.md:107-111` (the engine task snippet).

**What to change:** replace the single `compile_and_run` call with the same `compile()` + `execute()` pattern the harness uses (`harness/protocol_bridge.hpp:77-85`). After `compile`, emit a `diagnostics` message via the WS regardless of `ok`. Only call `execute` when `compiled.ok()`.

```cpp
CompileResult compiled = cardcode::compile(source);
ws_emit_diagnostics(compiled);  // builds the {"type":"diagnostics",...} payload
if (compiled.ok()) {
    JsonEventSink sink(ws_emit);
    cardcode::execute(*compiled.root, robot, sink, limits, &g_cancel);
}
```

**Why:** `cardcode::compile_and_run` returns diagnostics in `RunResult.diagnostics` but does not push them through the sink (`src/engine.cpp:373-388`). A bad program on a real robot would produce `programStart` and `programDone` with no diagnostic — the user wouldn't know why nothing happened.

**Verify:** send a syntactically broken program (e.g. `{"type":"run","source":"(fly 1)"}`) over the WS. The client should see a `diagnostics` message with `ok: false` *and* no `programStart`/`programDone`. (Today it would see `programStart` + `programDone` with no diagnostics.)

### 3.3  Fix the stack-size comment and pick a real number

**Where:** `esp32.md:117` and the surrounding paragraph at `:120-124`.

**What to change:** explicitly state the unit. On ESP-IDF, `xTaskCreate`'s `usStackDepth` is in *words* (sizeof(StackType_t) == 4 on 32-bit cores), so `8192` = 32 KB. Either:

- Change the call to `xTaskCreate(engine_task, "cardcode", 2048, …)` and note "8 KB stack — adjust up if you also use a deep call depth", or
- Keep `8192` and add a comment: "8192 words = 32 KB on a 32-bit core. The interpreter is recursive (see ExecutionLimits::max_call_depth)."

**Why:** the comment on line 121 ("give the task several KB of stack") contradicts the literal `8192` value, which is 32 KB. A reader who treats the comment as the truth will either over- or under-provision by 4x. The most common failure is under-provisioning (silent stack overflow).

**Verify:** read the new text in isolation. A first-time reader should be able to tell, to within 2x, how much RAM the task will use.

### 3.4  Add a `source` size cap and a return-value check on `httpd_ws_recv_frame`

**Where:** `esp32.md:140-150` (the WS handler sketch).

**What to add:**
- Check the return value of both `httpd_ws_recv_frame` calls; bail on `ESP_FAIL`.
- Cap `frame.len` at, say, 8192 bytes; if a client sends a longer message, close the connection or drop the message. ESP-IDF's default WS RX buffer is 1024 bytes (`CONFIG_ESP_HTTPS_SERVER_RX_BUFFER_SIZE`) and the API can split a single client message across multiple frames if it exceeds the buffer.
- Mention that the message is delivered as a single text frame for messages under the buffer size (the v1 case).

**Why:** the current snippet allocates a `std::string` of `frame.len` bytes and assumes the receive succeeds. On a partial frame, malformed close, or hostile client, the buffer is leaked. A size cap also prevents a single huge message from OOMing the heap.

**IDF 6.0.1 notes:** the `httpd_ws_frame_t` struct gained two new `bool` fields in IDF 6: `final` and `fragmented` (see `docs/integration/idf-6.0.1-http-server-reference.md`). The struct is now `{bool final; bool fragmented; httpd_ws_type_t type; uint8_t* payload; size_t len;}`. The `= {}` zero-init the sketch already uses handles the new fields correctly (both default to `false`). The cardcode v1 protocol only ever sends single-frame text messages, so `fragmented` will never be `true` on the wire; you can ignore the field.

**Verify:** a hostile client sending a 1 MB `source` should not crash the device. The bridge should reject it with `error("source too large")`.

### 3.5  Replace the per-event `new std::string` with a small pool

**Where:** `esp32.md:159-171` (the `ws_emit` snippet).

**What to change:** instead of `new std::string(std::move(json))` per event, use a fixed-capacity ring buffer of `std::string` (move-only, lives in static storage). The producer is the engine task; the consumer is the httpd task, woken by `httpd_queue_work` to drain a slot.

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

Document the trade-off: a ring drops events on overflow (which is fine for telemetry but bad for diagnostics — pick a policy and pin it).

**IDF 6.0.1 notes:** `httpd_queue_work()` is still the right API to marshal a callback onto the httpd task. The full reference for IDF 6.0.1's work-queue and WS APIs is at `docs/integration/idf-6.0.1-http-server-reference.md` — do not substitute a non-existent `esp_workqueue` component.

**Why:** the current snippet allocates and frees a `std::string` from the heap on every event. The engine emits at least one `robotCommand` per highlightable node, and a tight `while` loop with `max_total_steps = 1000` produces 1000 events in a few seconds. That is exactly the heap-fragmentation path that ESP-IDF warns against (`heap_caps_print_heap_info` will show fragmented free blocks after a few hundred small allocs/frees).

**Verify:** run a program with 1000 highlightable nodes on a real device. Monitor `heap_caps_get_min_free_size(MALLOC_CAP_8BIT)` and confirm it stays within a few KB of steady state. The current snippet will see the min free size drop and stay low.

### 3.6  Document the single-client constraint

**Where:** `esp32.md:131` (the `g_client_fd` declaration) and the Parity checklist at `:194-204`.

**What to add:** a sentence in the bring-up section: "Only one UI connects at a time. Connecting a new client mid-run takes over the live stream; the in-flight program continues to run, and the new client does not receive any history."

**IDF 6.0.1 notes:** the fd-retrieval API is unchanged. `httpd_req_to_sockfd()` is the canonical way to get the socket fd from a request in IDF 6.0.1 — the function is fully documented in the header and is referenced by other docblocks as the right way to do it. There is no deprecation marker and no new field on `httpd_req_t` to read instead. Keep using it.

**Why:** `static int g_client_fd = -1;` is a single slot. A second client's handshake overwrites it. The first client's events then go to the second's WS frame queue (or fail silently if the first is gone). This is fine for a classroom but should be a stated constraint, not a side effect of the example.

**Verify:** open a second browser tab to the same URL, observe the takeover.

---

## Group 4 — root `README.md`

### 4.1  Soften the "identical C++" claim

**Where:** `README.md:45-48` (the integration-layer paragraph).

**What to change:** replace "is identical C++ that compiles on both desktop and ESP32" with "is identical C++ for the engine core; the harness pieces need a small device-side wrapper around the `RobotHost`." Link to the new `Esp32ForwardingRobotHost` from item 3.1.

**Why:** the engine core is host-header-free (verified — `src/*.cpp` and `include/cardcode/*.hpp` only use `<cstdint>`, `<string>`, `<string_view>`, `<vector>`, `<memory>`, `<atomic>`, `<optional>`, `<unordered_map>`, `<utility>`, `<map>`, `<limits>`). The harness pieces are intended to be reused on the device but `harness/forwarding_host.hpp:25` and `:82-84` use `<thread>` and `<chrono>` and the desktop `ForwardingRobotHost` is *not* the right host for the device (see 3.1). The README overpromises.

**Verify:** the new text should be true on both desktops (where the harness works as-is) and on ESP32 (where the integrator will need to write the wrapper). The link should resolve to a real doc section after 3.1 lands.

---

## Group 5 — UI plan and missing manifest

### 5.1  Create `docs/integration/card-manifest.md` or remove the reference

**Where:** the file doesn't exist (no `card-manifest.md` in `docs/integration/`). It is referenced from:
- `docs/superpowers/specs/2026-06-07-cardcode-ui-design.md:74-76`
- `docs/superpowers/plans/2026-06-07-cardcode-ui.md:16, 91, 240-288` (the latter contains the full intended content as Task 2 step 5)

**What to do:** copy the markdown from the plan (`docs/superpowers/plans/2026-06-07-cardcode-ui.md:244-288`) into a new file `docs/integration/card-manifest.md`. The plan content is good — it documents the top-level fields, card fields, parameter fields, and projection rules. Add a "Versioning" subsection at the end noting that `manifestVersion: 1` is the only valid value today and that unknown top-level/card fields must be ignored for forward compatibility.

**Why:** the file is promised in two places and absent. This causes a broken link and a phantom requirement.

**Verify:** `ls docs/integration/` shows `card-manifest.md`. The two references above resolve to a real file.

### 5.2  Add a "form is not unique" note to the manifest doc

**Where:** the new `docs/integration/card-manifest.md` (just created in 5.1), in the "Card fields" section.

**What to add:** a sentence after the `form` field: "`form` is *not* unique across cards. The built-in manifest has two cards with `form: "define"` — `binding.define-var` and `binding.define-func` — distinguished by `id` and `kind`. UI projection uses `id` (or `(form, kind, shape)`) as the primary key; `form` alone is insufficient."

**Why:** the plan's manifest (`docs/superpowers/plans/2026-06-07-cardcode-ui.md:209-211`) defines two `define` cards, and the test in the same plan (`app/tests/manifest.test.js` per the plan, task 2 step 1) calls `byForm.get('define').some(c => c.kind === 'binding')` — that returns two matches, which is fine for the test but will surprise any reader who assumed `form` was a key. The manifest doc, as drafted in the plan, doesn't flag the non-uniqueness.

**Verify:** read the new sentence in context; it should make the test's behavior unsurprising.

---

## Group 6 — UI plan parser (optional but recommended)

### 6.1  Document the parser-vs-engine lex divergence

**Where:** `docs/superpowers/plans/2026-06-07-cardcode-ui.md:423-433` (the `Parser` class) or as a comment block in `app/src/parser.js` once it exists.

**What to add:** a one-paragraph note: "The browser parser is a *tolerant round-trip* parser, not a faithful re-implementation of the C++ lexer. The C++ lexer (`src/lexer.cpp:9-20`) accepts `_ ? ! < > = + * / .` in addition to alphanumerics; the browser parser stops at whitespace, parens, or `;`. Tokens that contain `+` or `-` (e.g. `+1`, `-1`) may parse differently. The engine's `compile` is the authoritative parser; the browser parser exists to keep the card tree and the source in sync."

**Why:** the two parsers are *almost* compatible but not quite. The C++ lexer is permissive (any `is_symbol_char` continuation is one token); the JS parser in the plan treats only whitespace/paren/semicolon as separators. They will agree on every card the UI produces today, but a future change to the engine (e.g. accepting `<=` as a single token vs. the plan's `lt`/`eq` projections) will diverge. The plan currently doesn't flag this, so the divergence will be discovered at the worst possible time.

**Verify:** the new note should be in the file *before* the parser is written, not after, so the implementer makes an informed decision about the symbol-character set.

**This is optional.** The UI spec at `2026-06-07-cardcode-ui-design.md:167-168` already says the browser parser is a "small browser JavaScript parser" and that "WASM reuse of the C++ parser remains a future option if exact compiler parity becomes more important than keeping the browser app simple." So the divergence is acknowledged at a high level; this item just pins it to the code level.

---

## Items intentionally *not* in this handoff

- **The full ESP32 bring-up walkthrough** (sdkconfig, partition table, WiFi provisioning, static-asset serving, OTA) is a separate workstream. The doc gaps there are bigger than the code/doc mismatches this handoff addresses; the original review listed them under "Will block the port" and they remain open.
- **A worked `Esp32RobotHost`** with real GPIO/LEDC wiring. The original `esp32.md` only sketches the class; making it concrete is a hardware decision that should be made separately.
- **The `card-manifest.md` content beyond what the plan already specifies.** The plan's Task 2 step 5 is a complete draft; the only addition this handoff asks for is the "form is not unique" note (5.2) and a versioning subsection.

## Definition of done

- All 14 items in this handoff are applied.
- `cmake --build build && ctest --test-dir build` is green.
- `./build/cardcode-harness` smoke-tests a `run` and a `setSensor` round-trip.
- `./build/cardcode-run examples/patrol.ccode` runs and exits 0.
- The protocol doc's `error` and `NodeSkipped` items are either both added to the spec *and* emitted by the engine, or both removed from spec and code.
- The new `docs/integration/card-manifest.md` exists and is referenced from the design spec and the plan.
- Every IDF API name that appears in `esp32.md` exists in **ESP-IDF 6.0.1** (verify against `docs/integration/idf-6.0.1-http-server-reference.md` or the raw header at `https://raw.githubusercontent.com/espressif/esp-idf/v6.0.1/components/esp_http_server/include/esp_http_server.h`). The flagged APIs are: `httpd_queue_work`, `httpd_req_to_sockfd`, `httpd_ws_recv_frame`, `httpd_ws_send_frame_async`. They are all present in 6.0.1 with the same signatures — the only change is two new `bool` fields on `httpd_ws_frame_t` (`final`, `fragmented`).

---

## Appendix — Findings (verbatim from the review that produced this handoff)

The previous agent's full review identified 20 specific disagreements between docs and code. They are listed below, mapped to the items above so you can audit the scope.

| # | Original finding | Resolved by |
|---|------------------|-------------|
| 1 | `error` message type missing from protocol doc | **1.1** |
| 2 | `NodeSkipped` is in the spec but never emitted | **1.2** |
| 3 | `sensorRead` placement not in the ordering text | **1.3** (folded in) |
| 4 | Implicit `robotCommand("stop")` not shown in trace | **1.3** |
| 5 | `compile_and_run` doesn't push diagnostics to sink | **3.2** |
| 6 | `ForwardingRobotHost` sketch wrong base class | **2.1** |
| 7 | `obj`/`span_json` are wrong helper names | **2.2** |
| 8 | esp32.md engine task uses `ForwardingRobotHost` as a hardware driver | **3.1** |
| 9 | esp32.md `compile_and_run` drops diagnostics | **3.2** |
| 10 | `xTaskCreate(..., 8192, ...)` is 32 KB, not "several KB" | **3.3** |
| 11 | WS receive buffer null-termination | folded into **3.4** |
| 12 | No WS fragmented/ping/close handling | **3.4** |
| 13 | `ws_emit` heap fragmentation + UAF on teardown | **3.5** |
| 14 | Single-client constraint is implicit | **3.6** |
| 15 | "Identical C++" overpromises in root README | **4.1** |
| 16 | `card-manifest.md` referenced but doesn't exist | **5.1** |
| 17 | `form` is not unique in the manifest | **5.2** |
| 18 | JS parser lex rules differ from C++ lexer | **6.1** |
| 19 | `ForwardingRobotHost` reset / sensor docs | folded into **2.1** |
| 20 | `cardcode-harness` target / stdio-TCP / `--realtime` flags | verified — no change needed |

Items 19 and 20 were verified during the review; finding 20 (harness entry points and flags) matches the code exactly. Finding 19 is just the same data path as 6 with a slightly different angle; it doesn't need its own item.

---

## Verification & corrections (2026-06-07)

A fresh verification pass against the code was run before applying the fixes. Most items held up; the corrections below were applied during the doc edit. Citations are line numbers in the *post-fix* files.

| # | Handoff claim | Verified truth | Correction applied |
|---|---|---|---|
| 1.1 | `error` message missing from `protocol.md` | TRUE | Added new `### error` section in `protocol.md:160-175` |
| 1.2 | Two options; prefer A (emit) | — | **A** chosen. New `emit_skipped` helper in `src/engine.cpp:115-117`; `When` and `If` cases updated at `:145-169`; test at `tests/test_harness.cpp:134-148` extended; TDD red→green |
| 1.3 | `robot.stop()` at `engine.cpp:365` | Actually `:364`; `ProgramError` emit is at `:365` | Doc cites `:364` (and `:359` for the cancellation branch) |
| 1.3 | "The doc's trace ends with `programError`" | There was **no** existing error trace in `protocol.md`; only a success trace ending in `programDone` | Added a new `## A complete cancel exchange` section at `protocol.md:203-228` showing the implicit `robotCommand("stop")` |
| 2.1 | "command+delay for each actuator" | Only 3 of 8 actuators call `delay`: `drive_forward` (`:43`), `drive_backward` (`:47`), `wait_ms` (`:58`); the other 5 emit only | Sketch marks `delay` as motion/wait-only |
| 2.2 | "`node_json` builds a vector of fields; rename to `jobj_v`" | The sketch uses `obj({{...}})` (initializer list), not a vector | Renamed to `jobj` (initializer-list form), not `jobj_v` |
| 2.3 | Three `error()` cases listed | The real dispatcher has **8** `error()` calls | All 8 listed in the new sketch comment block (`:207-214` of `local-harness.md`) |
| 3.1 | Desktop `ForwardingRobotHost` "sleeps" | It **no-ops by default**; sleeps only when `realtime_=true` (default `false`) | Doc describes the wrapper as no-op-by-default |
| 3.4 | (Not flagged) `frame.type` not checked before allocation | TRUE — sketch allocated `frame.len` bytes regardless | Added a `frame.type == HTTPD_WS_TYPE_TEXT` check before allocation |
| 3.5 | (Not flagged) `httpd_queue_work` failure leaks the `new std::string` | TRUE for the original snippet | Ring-buffer sidesteps the leak; prose note added |
| 4.1 | README:45-48 says "is identical C++" | Lines 45-48 are inside a `lisp` code block; the phrase appears **nowhere** in README. Actual location: README:174 ("drop into ESP32 / ESP-IDF firmware") | Targeted the actual line; new text at `README.md:174-179`; link to `esp32.md#1-a-real-robothost` |
| 4.1 | `forwarding_host.hpp:25` includes `<thread>`/`<chrono>` | `#include`s are at `:9` and `:12`; the uses are at `:82-84` | README cites the actual include lines (not 25) |
| 5.1 | Copy plan `:240-288` to new `card-manifest.md` | The copyable body inside the ` ```markdown ` fence is `:245-287` | Used `:245-287` |
| 5.2 | "Two `define`-form cards at `:209-211`" | `:209-210` only — `:211` is `binding.set` with `form: "set!"` | Note cites `:209-210` |
| 6.1 | Symbol-char set: `_ ? ! < > = + * / .` (10 chars) | Also includes `-` (load-bearing for `is_integer_word`); total **11 chars** | Note uses the corrected 11-char set |
| 6.1 | "C++ is permissive, JS is strict" | **Inverted** — C++ uses an allowlist of 11 chars; JS accepts a strict superset (anything not whitespace/paren/semicolon) | Note rewritten: JS is the superset; C++ rejects chars like `~ @ & : , [ ]` |

### Implementation deviations

- **Item 1.2 helper shape.** The handoff sketched a `void emit_skipped(const Node& n, ExecState& st)` that walks `n.children`. That shape is natural for `When` (body is `n.children`) but awkward for `If` (un-taken branch is one `Node` in `*n.args[1]`/`*n.args[2]`, not a children list). The implementation makes the helper take a single node and emit `NodeSkipped` for it, then loops explicitly in the `When` case and calls once in the `If` case. Wire output is identical.
- **Item 1.2 emit method.** The handoff's sketch used `st.emit_node(ExecutionEventType::NodeSkipped, child.id, child.span)`. The actual `ExecState` method is `st.emit(ExecutionEventType::NodeSkipped, n, n.op)` — matches the existing `NodeStart`/`NodeDone` pattern at `src/engine.cpp:330,336` and includes `op` so the JSON sink's `op` field is populated.
- **Group 2 had to be re-dispatched.** The first agent returned an empty result and the file was not touched. The redo applied all three items cleanly; the doc diff is `local-harness.md | +77 −23`.

### Build & smoke-test verification

- `cmake --build build` → exit 0
- `ctest --test-dir build` → 1/1 test suites passed, 0 failed; 84/84 assertions within the suite
- `./build/cardcode-harness` with `(define x 1) (drive 40 100)` → `diagnostics ok:true`, `programStart` … `programDone` (item 1.1 / 1.2 not triggered, as expected)
- `./build/cardcode-harness` with `setSensor distance-cm = 100` then `(when (< (distance-cm) 20) (light red))` → emits a `node` message with `event: "skipped"` for the un-taken `light` body (item 1.2 working end-to-end)
- `./build/cardcode-run examples/patrol.ccode` → exits 0

### Items still open

- The `app/tests/manifest.test.js` test mentioned in item 5.2 does not yet exist; the "form is not unique" note in the new `card-manifest.md` anticipates it.
- Finding #20 in the appendix under-enumerates the harness CLI flags (it omits `--port`, `--help`, the unknown-arg exit path, and the `BridgeOptions.async` flag). The fix for #20 was "no change needed" because the doc already matches; this row in the appendix is now historical.
- The original review's "Will block the port" list (sdkconfig, partition table, WiFi provisioning, static-asset serving, OTA, a worked `Esp32RobotHost`) is still open; see the "Items intentionally not in this handoff" section above.
