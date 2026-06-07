# CardCode Integration Guide

How to embed the CardCode execution engine into a host application — a **local
desktop harness** for developing the card UI, and **ESP32 firmware** for the real
robot. Both use the *same engine* and the *same wire protocol*; they differ only
in their **transport** and their **`RobotHost`**.

> Scope: this is design/architecture documentation with illustrative code
> sketches. The sketches show how to wire the existing public API
> (`include/cardcode/*`); they are not part of the built library.

## The one idea

The engine already exposes everything a host needs:

- `compile(source) -> CompileResult` — text → typed AST + diagnostics.
- `execute(root, robot, events, limits, cancel)` — runs the AST, calling a
  `RobotHost` for actions and pushing `ExecutionEvent`s to an `ExecutionEventSink`.
- `ExecutionEvent { type, node_id, span, message }` — the bridge to UI
  highlighting (deterministic node IDs + source spans).

So an integration is just three adapters around that core:

```
                         ┌───────────────── Integration layer (shared, portable) ─────────────────┐
   Browser UI            │                                                                          │
   (renders cards,  ⇄────┤  Transport          ProtocolBridge              Engine                  │
    highlights)          │  (moves JSON    →    parse control msgs   →   compile()/execute()        │
        ▲                │   bytes both        serialize events    ←    ExecutionEventSink          │
        │  JSON           │   directions)       to JSON            ←    RobotHost (forwards          │
        │  protocol       │                                              commands as JSON)          │
        └────────────────┤                                                                          │
                         └──────────────────────────────────────────────────────────────────────────┘
                              ╱                                                      ╲
            ┌────────────────────────────────┐                  ┌─────────────────────────────────────┐
            │ LOCAL HARNESS (desktop)         │                  │ ESP32 FIRMWARE                       │
            │ • Transport: TCP/stdio (A) or   │                  │ • Transport: on-device WiFi          │
            │   header-only WS (B); WASM (C)  │                  │   websocket (esp_http_server)        │
            │ • RobotHost: MockRobotHost      │                  │ • RobotHost: real motors/LEDs/sensors│
            │   (programmable sensors)        │                  │   over ESP-IDF drivers               │
            │ • Engine runs in a worker thread│                  │ • Engine runs in a FreeRTOS task     │
            └────────────────────────────────┘                  └─────────────────────────────────────┘
```

Only the two boxes at the bottom are target-specific. Everything in the
"Integration layer" is identical C++ that compiles on both desktop and ESP32
(the engine is exception/RTTI-free and host-header-free — see the root README's
*Embedded integration* section).

## Documents

- **[protocol.md](protocol.md)** — the greenfield JSON event/control protocol the
  UI speaks. Read this first; it is the contract both targets implement.
- **[local-harness.md](local-harness.md)** — the desktop harness: the
  `ExecutionEventSink`→JSON bridge, the forwarding `RobotHost`, threading and
  cancellation, and the three transport options (A dependency-free, B WS library,
  C WASM).
- **[esp32.md](esp32.md)** — ESP-IDF firmware: a concrete `RobotHost` over real
  drivers, running the engine in a FreeRTOS task, hosting the websocket, and
  cancellation/safety on-device.

## Why one protocol for both

The whole point of CardCode is that runtime events carry deterministic node IDs
and source spans so the UI can highlight the matching card. If the desktop
harness and the robot emit the *same* event stream, then:

- The card UI is written **once** and tested against the desktop harness, with no
  hardware in the loop.
- "Run on the robot" is just pointing the same UI at the device's websocket URL.
- A program that highlights correctly in the harness highlights identically on
  the robot, because the engine and its ID assignment are byte-for-byte the same
  build of the same code.
