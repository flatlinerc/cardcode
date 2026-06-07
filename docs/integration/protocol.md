# CardCode Wire Protocol (v1)

The JSON message protocol between the **UI** (client) and a **host** running the
engine (the local harness or the robot). Greenfield — defined here, implemented
identically by both targets.

## Framing

- Each message is a single JSON object with a `"type"` field.
- Over a websocket, one message per text frame.
- Over the dependency-free transport (local harness option A), one message per
  newline-delimited line (no embedded raw newlines; strings are JSON-escaped).

The protocol is transport-agnostic: the same objects flow over a browser
WebSocket, a raw TCP socket, stdio, or (future) a WASM `postMessage` channel.

## Coordinate types

A **span** locates source text for highlighting (mirrors `cardcode::SourceSpan`):

```json
{ "startOffset": 24, "endOffset": 39,
  "startLine": 2, "startColumn": 3, "endLine": 2, "endColumn": 18 }
```

A **node id** is the engine's deterministic ID as a string (`"n3"`), matching
`cardcode::node_id_to_string`. The same expression keeps the same id across loop
iterations and function calls, so the UI can map an id to one card.

> Highlighting can key off either the node id (stable identity) or the span (byte
> range into the source the UI already has). Spans are the robust choice when the
> UI re-parses source itself; node ids are best for "this exact card, every time
> it runs." Both are always provided.

---

## Client → Host (control)

### `compile` — editor feedback, no execution
Compile only, to surface diagnostics while the student edits.
```json
{ "type": "compile", "source": "(drive 40 1000)" }
```

### `run` — compile (if needed) and execute
```json
{ "type": "run",
  "source": "(repeat 4 (drive 40 1000) (turn-right 90))",
  "limits": { "maxTotalSteps": 1000, "maxRepeatCount": 100, "maxCallDepth": 64 } }
```
`limits` is optional; omitted fields use engine defaults (`ExecutionLimits`).
A `run` while a program is already running is rejected with an `error` message
(one program at a time).

### `cancel` — stop the running program
```json
{ "type": "cancel" }
```
Sets the engine's cancellation flag; the host stops the robot and ends with
`programError` (`"execution cancelled"`).

### `setSensor` — inject a simulated sensor reading (harness/testing)
```json
{ "type": "setSensor", "sensor": "distance-cm", "value": 15 }
```
`sensor` ∈ `distance-cm | button | line-left | line-right`. Integer for
`distance-cm`, boolean for the rest. On the robot this is typically ignored (real
hardware drives the sensors) or used as an override for bench testing.

### `reset` — return the (simulated) robot to a known state
```json
{ "type": "reset" }
```

---

## Host → Client (results & telemetry)

### `diagnostics` — result of `compile` or the compile phase of `run`
```json
{ "type": "diagnostics", "ok": true, "diagnostics": [] }
```
```json
{ "type": "diagnostics", "ok": false,
  "diagnostics": [
    { "severity": "error", "message": "unknown operation 'fly'",
      "span": { "startOffset": 1, "endOffset": 4, "startLine": 1,
                "startColumn": 2, "endLine": 1, "endColumn": 5 } }
  ] }
```
`severity` ∈ `error | warning`. When `ok` is false after a `run`, execution does
not start.

### `programStart` / `programDone`
```json
{ "type": "programStart" }
{ "type": "programDone" }
```

### `node` — execution reached a highlightable node
One message per `NodeStart` / `NodeDone` / `NodeSkipped` / `NodeError`.
```json
{ "type": "node", "event": "start", "id": "n1", "nodeId": 1,
  "op": "drive",
  "span": { "startOffset": 14, "endOffset": 29, "startLine": 2,
            "startColumn": 3, "endLine": 2, "endColumn": 18 } }
```
```json
{ "type": "node", "event": "done", "id": "n1", "nodeId": 1, "op": "drive" }
```
`event` maps directly to `ExecutionEventType` (`start`=NodeStart, `done`=NodeDone,
`skipped`=NodeSkipped, `error`=NodeError). `error` events also carry `message`.
Only highlightable nodes appear here (control flow, commands, sensors, `call`,
`define` var, `set!`); operators and literals never do — see the root README's
highlighting policy.

### `robotCommand` — the program actuated the robot
Emitted by the host's `RobotHost` as each call happens, interleaved **between**
the issuing node's `start` and `done` (the command runs during the node's body).
```json
{ "type": "robotCommand", "command": "drive_forward",
  "args": { "speed": 40, "durationMs": 1000 } }
```
Command vocabulary and args (one per `RobotHost` method):

| command          | args                          |
|------------------|-------------------------------|
| `drive_forward`  | `{ speed, durationMs }`       |
| `drive_backward` | `{ speed, durationMs }`       |
| `turn_left`      | `{ degrees }`                 |
| `turn_right`     | `{ degrees }`                 |
| `stop`           | `{}`                          |
| `wait`           | `{ durationMs }`              |
| `set_light`      | `{ color }` (`off`/`red`/…)   |
| `beep`           | `{}`                          |

### `sensorRead` — a sensor was read (optional, useful for the sim/debug view)
```json
{ "type": "sensorRead", "sensor": "distance-cm", "value": 15 }
```

### `log`
```json
{ "type": "log", "message": "…" }
```

### `programError`
```json
{ "type": "programError",
  "message": "division by zero",
  "span": { "startOffset": 30, "endOffset": 40, "startLine": 3,
            "startColumn": 8, "endLine": 3, "endColumn": 18 } }
```
Sent for runtime errors and cancellation. Before this is sent, the host emits
an implicit `robotCommand("stop")` as the last in-flight message (so a
cancellation or runtime-error trace is `… node done … robotCommand("stop") …
programError`, never `programError` on its own). Engine: `src/engine.cpp:359`
(cancellation) and `:364` (runtime error).

### `error` — protocol-level error from the host
```json
{ "type": "error", "message": "a program is already running" }
```
Emitted on the control thread, so it can interleave with messages from a run
that is already in progress. Triggered by:
- Malformed JSON (or a value that is not an object)
- Missing `type` field, or `type` that is not a string
- Missing required field for a typed message (`run`/`compile` without
  `source`, `setSensor` without `sensor` or `value`)
- `run` rejected because a program is already running
- `setSensor` with an unknown sensor name
- Unknown message `type`

Distinct from `programError` (which is a *runtime* error inside a running
program and is paired with the `programStart` boundary).

---

## A complete `run` exchange

For `(repeat 2 (drive 40 1000) (turn-right 90))`:

```
C→H  {"type":"run","source":"(repeat 2 (drive 40 1000) (turn-right 90))"}
H→C  {"type":"diagnostics","ok":true,"diagnostics":[]}
H→C  {"type":"programStart"}
H→C  {"type":"node","event":"start","id":"n0","nodeId":0,"op":"repeat","span":{…}}
H→C  {"type":"node","event":"start","id":"n1","nodeId":1,"op":"drive","span":{…}}
H→C  {"type":"robotCommand","command":"drive_forward","args":{"speed":40,"durationMs":1000}}
H→C  {"type":"node","event":"done","id":"n1","nodeId":1,"op":"drive"}
H→C  {"type":"node","event":"start","id":"n2","nodeId":2,"op":"turn-right","span":{…}}
H→C  {"type":"robotCommand","command":"turn_right","args":{"degrees":90}}
H→C  {"type":"node","event":"done","id":"n2","nodeId":2,"op":"turn-right"}
        … (second iteration repeats n1, n2 with the SAME ids) …
H→C  {"type":"node","event":"done","id":"n0","nodeId":0,"op":"repeat"}
H→C  {"type":"programDone"}
```

The UI highlights the card for an id on `start`, shows the interleaved
`robotCommand`, and clears the highlight on `done`. Identical bytes whether the
host is the desktop harness or the robot.

## A complete `cancel` exchange

Same program, but `cancel` arrives while the second iteration's `drive` is in
flight. The `n1` `node done` is the last in-flight event the engine emits for
the cancelled node; the host then synthesises `robotCommand("stop")` before
`programError`.

```
C→H  {"type":"run","source":"(repeat 2 (drive 40 1000) (turn-right 90))"}
H→C  {"type":"diagnostics","ok":true,"diagnostics":[]}
H→C  {"type":"programStart"}
H→C  {"type":"node","event":"start","id":"n0","nodeId":0,"op":"repeat","span":{…}}
H→C  {"type":"node","event":"start","id":"n1","nodeId":1,"op":"drive","span":{…}}
H→C  {"type":"robotCommand","command":"drive_forward","args":{"speed":40,"durationMs":1000}}
H→C  {"type":"node","event":"done","id":"n1","nodeId":1,"op":"drive"}
H→C  {"type":"node","event":"start","id":"n2","nodeId":2,"op":"turn-right","span":{…}}
H→C  {"type":"robotCommand","command":"turn_right","args":{"degrees":90}}
H→C  {"type":"node","event":"done","id":"n2","nodeId":2,"op":"turn-right"}
        … (second iteration: n1 starts) …
H→C  {"type":"node","event":"start","id":"n1","nodeId":1,"op":"drive","span":{…}}
H→C  {"type":"robotCommand","command":"drive_forward","args":{"speed":40,"durationMs":1000}}
C→H  {"type":"cancel"}
H→C  {"type":"node","event":"done","id":"n1","nodeId":1,"op":"drive"}
H→C  {"type":"robotCommand","command":"stop","args":{}}
H→C  {"type":"programError","message":"execution cancelled"}
```

## Ordering & threading guarantees the host must keep

- Messages for one `run` are strictly ordered: `diagnostics` → `programStart` →
  node/robotCommand/log… → (`programDone` | `programError`).
- `robotCommand` for a node arrives after that node's `start` and before its
  `done`.
- The engine is single-threaded per run and emits events synchronously from the
  execution thread; the host serializes them onto the transport in that order
  (see local-harness.md and esp32.md for the queue pattern).

## Versioning

This is protocol **v1**. Add a `"protocol": 1` field to the first message from
each side if/when a v2 is introduced; unknown `type` values must be ignored by
both sides so additive changes stay backward-compatible.
