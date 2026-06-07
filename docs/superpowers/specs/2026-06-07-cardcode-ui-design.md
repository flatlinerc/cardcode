# CardCode: Real Card UI & Robot Connection — Design

Date: 2026-06-07
Status: Draft for review

## Goal

Replace the single-file editor prototype with a real CardCode UI that can edit
the whole current language, round-trip between cards and source, and run the
current program against either the local mock harness or a real robot using the
existing JSON wire protocol.

Keep the implementation small and framework-free for now, but separate concerns
so parser, manifest, projection, source generation, transport, state, and DOM
rendering can be moved or replaced later without rewriting the whole app.

## Current state

`cardcode_editor.html` is a useful visual prototype, but it is not wired to the
engine:

- It stores a local ad hoc card tree and simulates execution by walking DOM cards.
- Its Code tab says Python and generates Python-like code, while the engine runs
  CardCode S-expressions.
- It has no websocket client, diagnostics display, cancel path, sensor controls,
  or source-span/node-id mapping.
- Some prototype cards do not correspond directly to current engine forms.

The backend side already exists. `cardcode-harness` speaks the protocol in
`docs/integration/protocol.md` over stdio or TCP, and can be exposed to a browser
with `websocketd` at `ws://localhost:8080`. The robot target should speak the
same messages.

## Architecture

Build a small app in `app/`:

```text
app/
  index.html
  styles.css
  cardcode-default.manifest.json
  src/
    app.js
    state.js
    parser.js
    manifest.js
    projection.js
    generator.js
    transport.js
    render.js
```

Responsibilities:

- **Parser**: CardCode source text -> syntax tree with spans. No DOM, manifest,
  source generation, or websocket code.
- **Manifest**: card definitions, validation, defaults, categories, parameter
  schemas, and template metadata. No DOM.
- **Projection**: syntax tree <-> card tree using the manifest. Unknown valid
  forms become generic S-expression cards.
- **Generator**: card tree -> canonical CardCode source plus source-span mapping.
  No DOM and no transport.
- **Transport**: websocket protocol client and runtime state machine. No card
  rendering and no source generation.
- **State**: current source, syntax tree, card tree, diagnostics, connection
  state, and run state.
- **Renderer**: DOM only. It consumes state and emits intent callbacks.

This deliberately leaves room to move modules into TypeScript, WASM, or a
framework later.

## Card manifest

Add a repo-owned manifest contract in `docs/integration/card-manifest.md` and a
default manifest in `app/cardcode-default.manifest.json`.

The manifest is versioned JSON. It describes the cards a CardCode implementation
can output or accept:

```json
{
  "manifestVersion": 1,
  "language": "cardcode",
  "cards": [
    {
      "id": "robot.drive",
      "form": "drive",
      "kind": "command",
      "category": "motion",
      "label": "Drive",
      "template": "(drive {{speed}} {{durationMs}})",
      "highlight": true,
      "params": [
        {
          "name": "speed",
          "kind": "integer",
          "label": "speed",
          "unit": "%",
          "min": 0,
          "max": 100,
          "default": 40
        },
        {
          "name": "durationMs",
          "kind": "integer",
          "label": "duration",
          "unit": "ms",
          "min": 0,
          "max": 60000,
          "default": 1000
        }
      ],
      "capability": {
        "type": "robotCommand",
        "name": "drive_forward"
      }
    }
  ]
}
```

Supported card kinds:

- `command`: emits a concrete command form, such as `drive`, `turn-left`,
  `light`, or `beep`.
- `control`: owns children or branches, such as `do`, `repeat`, `when`, `if`,
  and `while`.
- `expression`: emits value-producing forms, such as sensors, comparisons,
  boolean operators, arithmetic operators, literals, and variable references.
- `binding`: emits `define`, `set!`, and function definitions.
- `generic`: fallback for valid S-expression forms without a specialized card.

Robot-specific operations are manifest cards. Today the built-in manifest maps
to the engine's current forms, but a future robot can expose a different card set
as long as its host and engine support those forms. Unknown manifest fields are
ignored so additive changes remain backward-compatible.

## Language coverage

The UI model must cover the current language:

- `do`
- `repeat`, `when`, `if`, `while`
- `define` for variables
- `set!`
- function definitions with parameters
- function calls
- variable references
- commands: `drive`, `backward`, `turn-left`, `turn-right`, `stop`, `wait`,
  `light`, `beep`
- sensors: `distance-cm`, `button`, `line-left`, `line-right`
- comparisons, boolean operators, and arithmetic operators
- literals: integers, booleans, and colors

Common robot workflow cards get specialized controls. Advanced forms that do not
yet have polished visual editors still round-trip through generic cards or
expression editors.

## Source and cards round trip

Cards and source are two editable projections of the same CardCode syntax tree.

For v1, implement a small browser JavaScript parser. CardCode syntax is compact
S-expressions, and the UI only needs lexing, parsing, spans, and tree projection;
authoritative semantic validation still comes from `compile` on the host. WASM
reuse of the C++ parser remains a future option if exact compiler parity becomes
more important than keeping the browser app simple.

Flow:

1. Card edits update the card tree.
2. The generator produces canonical CardCode source and source spans.
3. Code edits parse source into a syntax tree.
4. Projection maps syntax forms to specialized cards through the manifest.
5. Unknown or advanced forms become generic S-expression cards.
6. Debounced `compile` asks the harness or robot for authoritative diagnostics.
7. `run` sends the exact current source.

V1 regenerates canonical formatting when source is produced from cards. Comments
and hand formatting are not preserved across a card-generation pass.

## Runtime and robot selection

Add a connection panel:

- Target selector: Mock harness, Robot, Custom URL.
- URL field, defaulting to `ws://localhost:8080`.
- Connect/disconnect state.
- Run, Stop, and Reset controls.
- Mock sensor controls: distance, button, line-left, and line-right.

The runtime client uses the existing protocol first:

- Send `compile` after edits, debounced.
- Send `run` on Run.
- Send `cancel` on Stop.
- Send `reset` from Reset.
- Send `setSensor` from mock sensor controls.

Received messages update state:

- `diagnostics`: show compile errors and attach them to cards by span overlap.
- `programStart`: enter running state.
- `node start/done/skipped/error`: highlight or mark cards by source span and
  node id.
- `robotCommand`: append telemetry and optionally update a simple robot-state
  panel.
- `sensorRead`: update the sensor display.
- `programDone`, `programError`, and `error`: exit running state and show status.

A future extension can let a robot advertise its manifest over the same
connection. V1 starts with the built-in manifest plus optional manifest URL.

## Highlight mapping

The generator records the source span for each generated card. Host diagnostics
and runtime node events also include spans, so span overlap is the primary
mapping from protocol messages back to cards.

When available, node ids are cached after compile/run events and used as a stable
runtime identity. Span mapping remains the fallback because generic cards and
code-edited source may not have cached ids before the first run.

## Testing

Start with pure JavaScript tests that do not require a browser framework:

- Parse all `examples/*.ccode` into syntax trees with spans.
- Generate source from card trees and reparse it.
- Project known forms into specialized cards.
- Fall back unknown valid forms into generic S-expression cards.
- Map diagnostic and node spans back to cards.
- Exercise the runtime client with a fake websocket.

Add a minimal browser smoke test later to confirm the app loads, switches between
Blocks and Code, connects to a fake or real websocket, and highlights a running
node.

## Out of scope / follow-ups

- Full visual grammar polish for every expression form. The model supports it,
  but v1 can use generic cards or expression editors for advanced cases.
- Preserving comments and hand formatting across card regeneration.
- Robot-advertised manifests over the protocol.
- A full Blockly-style drag/drop expression compositor.
- WASM parser integration.
