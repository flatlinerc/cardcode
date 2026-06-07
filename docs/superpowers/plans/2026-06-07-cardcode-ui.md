# CardCode UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a small, modular CardCode browser app that round-trips between cards and source, validates through the existing harness/robot protocol, and can run against `ws://localhost:8080` or a robot websocket.

**Architecture:** Use plain browser ES modules in `app/` with strict module boundaries: parser, manifest, projection, generator, transport, state, and renderer. Use a JavaScript S-expression parser for browser-side source/card round trip, and keep semantic validation authoritative through the existing `compile` protocol message.

**Tech Stack:** Vanilla HTML/CSS/JavaScript ES modules, Node.js built-in `node:test`, existing C++ `cardcode-harness` websocket protocol, static file serving via `python3 -m http.server`.

---

## File Structure

- Create `package.json`: declares ESM mode and JS test scripts.
- Create `docs/integration/card-manifest.md`: durable manifest contract.
- Create `app/index.html`: static app shell.
- Create `app/styles.css`: app layout and card styling, adapted from `cardcode_editor.html`.
- Create `app/cardcode-default.manifest.json`: built-in CardCode card manifest.
- Create `app/src/parser.js`: lexer/parser, syntax tree, spans.
- Create `app/src/manifest.js`: manifest loading and validation.
- Create `app/src/projection.js`: syntax tree to card tree and card tree helpers.
- Create `app/src/generator.js`: card tree to canonical source and span mapping.
- Create `app/src/transport.js`: websocket protocol client.
- Create `app/src/state.js`: app state reducer and selectors.
- Create `app/src/render.js`: DOM rendering and event binding.
- Create `app/src/app.js`: startup orchestration.
- Create `app/tests/*.test.js`: Node tests for pure modules and fake websocket transport.

Implementation order keeps pure logic ahead of UI. Each task should be committed independently.

## Task 1: JavaScript Test Harness

**Files:**
- Create: `package.json`
- Create: `app/tests/smoke.test.js`

- [ ] **Step 1: Write the failing smoke test**

Create `app/tests/smoke.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';

test('js test harness runs', () => {
  assert.equal(2 + 2, 4);
});
```

- [ ] **Step 2: Add the package script**

Create `package.json`:

```json
{
  "type": "module",
  "scripts": {
    "test:js": "node --test app/tests/*.test.js"
  }
}
```

- [ ] **Step 3: Run the JS smoke test**

Run: `npm run test:js`

Expected: PASS with one passing test.

- [ ] **Step 4: Run the existing C++ tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: existing C++ tests pass. If `build/` does not exist, run `cmake -S . -B build` first.

- [ ] **Step 5: Commit**

```bash
git add package.json app/tests/smoke.test.js
git commit -m "test: add js test harness"
```

## Task 2: Manifest Contract and Default Manifest

**Files:**
- Create: `docs/integration/card-manifest.md`
- Create: `app/cardcode-default.manifest.json`
- Create: `app/src/manifest.js`
- Create: `app/tests/manifest.test.js`

- [ ] **Step 1: Write manifest validation tests**

Create `app/tests/manifest.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadManifestFromObject } from '../src/manifest.js';

test('default manifest validates and indexes current language cards', async () => {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  const manifest = loadManifestFromObject(raw);

  assert.equal(manifest.manifestVersion, 1);
  assert.equal(manifest.language, 'cardcode');
  assert.ok(manifest.byForm.get('drive').some((card) => card.id === 'robot.drive'));
  assert.ok(manifest.byForm.get('define').some((card) => card.kind === 'binding'));
  assert.ok(manifest.byForm.get('+').some((card) => card.kind === 'expression'));
  assert.ok(manifest.byId.has('control.if'));
});

test('manifest validation rejects duplicate card ids', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      { id: 'x', form: 'drive', kind: 'command', category: 'motion', label: 'A', template: '(drive {{speed}} {{durationMs}})', params: [] },
      { id: 'x', form: 'wait', kind: 'command', category: 'time', label: 'B', template: '(wait {{durationMs}})', params: [] }
    ]
  }), /duplicate card id 'x'/);
});
```

- [ ] **Step 2: Run tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/manifest.js` and `app/cardcode-default.manifest.json` do not exist.

- [ ] **Step 3: Create the manifest loader**

Create `app/src/manifest.js`:

```js
const VALID_KINDS = new Set(['command', 'control', 'expression', 'binding', 'generic']);

export function loadManifestFromObject(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('manifest must be an object');
  if (raw.manifestVersion !== 1) throw new Error('manifestVersion must be 1');
  if (raw.language !== 'cardcode') throw new Error("language must be 'cardcode'");
  if (!Array.isArray(raw.cards)) throw new Error('cards must be an array');

  const byId = new Map();
  const byForm = new Map();
  const cards = raw.cards.map((card) => validateCard(card, byId));

  for (const card of cards) {
    if (!byForm.has(card.form)) byForm.set(card.form, []);
    byForm.get(card.form).push(card);
  }

  return {
    manifestVersion: raw.manifestVersion,
    language: raw.language,
    cards,
    byId,
    byForm
  };
}

export async function loadManifest(url = './cardcode-default.manifest.json') {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`failed to load manifest: ${res.status}`);
  return loadManifestFromObject(await res.json());
}

function validateCard(card, byId) {
  for (const key of ['id', 'form', 'kind', 'category', 'label', 'template']) {
    if (typeof card[key] !== 'string' || card[key].length === 0) {
      throw new Error(`card ${card.id || '<unknown>'}: ${key} must be a non-empty string`);
    }
  }
  if (byId.has(card.id)) throw new Error(`duplicate card id '${card.id}'`);
  if (!VALID_KINDS.has(card.kind)) throw new Error(`card ${card.id}: invalid kind '${card.kind}'`);
  const params = Array.isArray(card.params) ? card.params.map(validateParam) : [];
  const normalized = { ...card, params };
  byId.set(normalized.id, normalized);
  return normalized;
}

function validateParam(param) {
  if (!param || typeof param !== 'object') throw new Error('param must be an object');
  if (typeof param.name !== 'string' || param.name.length === 0) throw new Error('param name must be a non-empty string');
  if (typeof param.kind !== 'string' || param.kind.length === 0) throw new Error(`param ${param.name}: kind must be a non-empty string`);
  return { ...param };
}
```

- [ ] **Step 4: Add the default manifest**

Create `app/cardcode-default.manifest.json` with this full current-language card set:

```json
{
  "manifestVersion": 1,
  "language": "cardcode",
  "cards": [
    { "id": "control.do", "form": "do", "kind": "control", "category": "control", "label": "Do", "template": "(do {{children}})", "children": "sequence", "params": [] },
    { "id": "control.repeat", "form": "repeat", "kind": "control", "category": "loops", "label": "Repeat", "template": "(repeat {{count}} {{children}})", "children": "sequence", "params": [{ "name": "count", "kind": "expression", "label": "count", "default": 4 }] },
    { "id": "control.when", "form": "when", "kind": "control", "category": "logic", "label": "When", "template": "(when {{condition}} {{children}})", "children": "sequence", "params": [{ "name": "condition", "kind": "expression", "label": "condition", "default": true }] },
    { "id": "control.if", "form": "if", "kind": "control", "category": "logic", "label": "If", "template": "(if {{condition}} {{then}} {{else}})", "branches": ["then", "else"], "params": [{ "name": "condition", "kind": "expression", "label": "condition", "default": true }] },
    { "id": "control.while", "form": "while", "kind": "control", "category": "loops", "label": "While", "template": "(while {{condition}} {{children}})", "children": "sequence", "params": [{ "name": "condition", "kind": "expression", "label": "condition", "default": true }] },
    { "id": "binding.define-var", "form": "define", "kind": "binding", "category": "variables", "label": "Define variable", "template": "(define {{name}} {{value}})", "params": [{ "name": "name", "kind": "symbol", "label": "name", "default": "x" }, { "name": "value", "kind": "expression", "label": "value", "default": 0 }] },
    { "id": "binding.define-func", "form": "define", "kind": "binding", "category": "functions", "label": "Define function", "template": "(define ({{name}} {{params}}) {{body}})", "children": "body", "params": [{ "name": "name", "kind": "symbol", "label": "name", "default": "my-function" }, { "name": "params", "kind": "symbolList", "label": "params", "default": [] }] },
    { "id": "binding.set", "form": "set!", "kind": "binding", "category": "variables", "label": "Set variable", "template": "(set! {{name}} {{value}})", "params": [{ "name": "name", "kind": "symbol", "label": "name", "default": "x" }, { "name": "value", "kind": "expression", "label": "value", "default": 0 }] },
    { "id": "robot.drive", "form": "drive", "kind": "command", "category": "motion", "label": "Drive", "template": "(drive {{speed}} {{durationMs}})", "highlight": true, "params": [{ "name": "speed", "kind": "expression", "label": "speed", "unit": "%", "min": 0, "max": 100, "default": 40 }, { "name": "durationMs", "kind": "expression", "label": "duration", "unit": "ms", "min": 0, "max": 60000, "default": 1000 }], "capability": { "type": "robotCommand", "name": "drive_forward" } },
    { "id": "robot.backward", "form": "backward", "kind": "command", "category": "motion", "label": "Backward", "template": "(backward {{speed}} {{durationMs}})", "highlight": true, "params": [{ "name": "speed", "kind": "expression", "label": "speed", "unit": "%", "min": 0, "max": 100, "default": 40 }, { "name": "durationMs", "kind": "expression", "label": "duration", "unit": "ms", "min": 0, "max": 60000, "default": 1000 }], "capability": { "type": "robotCommand", "name": "drive_backward" } },
    { "id": "robot.turn-left", "form": "turn-left", "kind": "command", "category": "motion", "label": "Turn left", "template": "(turn-left {{degrees}})", "highlight": true, "params": [{ "name": "degrees", "kind": "expression", "label": "degrees", "unit": "deg", "min": 0, "max": 360, "default": 90 }], "capability": { "type": "robotCommand", "name": "turn_left" } },
    { "id": "robot.turn-right", "form": "turn-right", "kind": "command", "category": "motion", "label": "Turn right", "template": "(turn-right {{degrees}})", "highlight": true, "params": [{ "name": "degrees", "kind": "expression", "label": "degrees", "unit": "deg", "min": 0, "max": 360, "default": 90 }], "capability": { "type": "robotCommand", "name": "turn_right" } },
    { "id": "robot.stop", "form": "stop", "kind": "command", "category": "motion", "label": "Stop", "template": "(stop)", "highlight": true, "params": [], "capability": { "type": "robotCommand", "name": "stop" } },
    { "id": "robot.wait", "form": "wait", "kind": "command", "category": "time", "label": "Wait", "template": "(wait {{durationMs}})", "highlight": true, "params": [{ "name": "durationMs", "kind": "expression", "label": "duration", "unit": "ms", "min": 0, "max": 60000, "default": 1000 }], "capability": { "type": "robotCommand", "name": "wait" } },
    { "id": "robot.light", "form": "light", "kind": "command", "category": "output", "label": "Light", "template": "(light {{color}})", "highlight": true, "params": [{ "name": "color", "kind": "enum", "label": "color", "values": ["off", "red", "green", "blue", "yellow", "white"], "default": "red" }], "capability": { "type": "robotCommand", "name": "set_light" } },
    { "id": "robot.beep", "form": "beep", "kind": "command", "category": "output", "label": "Beep", "template": "(beep)", "highlight": true, "params": [], "capability": { "type": "robotCommand", "name": "beep" } },
    { "id": "sensor.distance-cm", "form": "distance-cm", "kind": "expression", "category": "sensors", "label": "Distance cm", "template": "(distance-cm)", "highlight": true, "params": [] },
    { "id": "sensor.button", "form": "button", "kind": "expression", "category": "sensors", "label": "Button", "template": "(button)", "highlight": true, "params": [] },
    { "id": "sensor.line-left", "form": "line-left", "kind": "expression", "category": "sensors", "label": "Line left", "template": "(line-left)", "highlight": true, "params": [] },
    { "id": "sensor.line-right", "form": "line-right", "kind": "expression", "category": "sensors", "label": "Line right", "template": "(line-right)", "highlight": true, "params": [] },
    { "id": "op.lt", "form": "<", "kind": "expression", "category": "operators", "label": "Less than", "template": "(< {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 0 }, { "name": "right", "kind": "expression", "label": "right", "default": 1 }] },
    { "id": "op.gt", "form": ">", "kind": "expression", "category": "operators", "label": "Greater than", "template": "(> {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 1 }, { "name": "right", "kind": "expression", "label": "right", "default": 0 }] },
    { "id": "op.eq", "form": "=", "kind": "expression", "category": "operators", "label": "Equals", "template": "(= {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 1 }, { "name": "right", "kind": "expression", "label": "right", "default": 1 }] },
    { "id": "op.lte", "form": "<=", "kind": "expression", "category": "operators", "label": "Less or equal", "template": "(<= {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 0 }, { "name": "right", "kind": "expression", "label": "right", "default": 1 }] },
    { "id": "op.gte", "form": ">=", "kind": "expression", "category": "operators", "label": "Greater or equal", "template": "(>= {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 1 }, { "name": "right", "kind": "expression", "label": "right", "default": 0 }] },
    { "id": "op.and", "form": "and", "kind": "expression", "category": "operators", "label": "And", "template": "(and {{args}})", "variadic": "args", "params": [] },
    { "id": "op.or", "form": "or", "kind": "expression", "category": "operators", "label": "Or", "template": "(or {{args}})", "variadic": "args", "params": [] },
    { "id": "op.not", "form": "not", "kind": "expression", "category": "operators", "label": "Not", "template": "(not {{value}})", "params": [{ "name": "value", "kind": "expression", "label": "value", "default": true }] },
    { "id": "op.add", "form": "+", "kind": "expression", "category": "operators", "label": "Add", "template": "(+ {{args}})", "variadic": "args", "params": [] },
    { "id": "op.sub", "form": "-", "kind": "expression", "category": "operators", "label": "Subtract", "template": "(- {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 1 }, { "name": "right", "kind": "expression", "label": "right", "default": 1 }] },
    { "id": "op.mul", "form": "*", "kind": "expression", "category": "operators", "label": "Multiply", "template": "(* {{args}})", "variadic": "args", "params": [] },
    { "id": "op.div", "form": "/", "kind": "expression", "category": "operators", "label": "Divide", "template": "(/ {{left}} {{right}})", "params": [{ "name": "left", "kind": "expression", "label": "left", "default": 10 }, { "name": "right", "kind": "expression", "label": "right", "default": 2 }] }
  ]
}
```

- [ ] **Step 5: Add the manifest docs**

Create `docs/integration/card-manifest.md`:

```markdown
# CardCode Card Manifest v1

The card manifest is a versioned JSON document that describes which CardCode
forms a UI can render as specialized cards. The canonical program remains
CardCode source; the manifest only describes UI projection, defaults, categories,
parameter metadata, and optional robot capabilities.

## Top-level fields

- `manifestVersion`: integer, currently `1`.
- `language`: string, currently `cardcode`.
- `cards`: array of card definitions.

Unknown fields must be ignored by readers so additive changes stay compatible.

## Card fields

- `id`: stable unique id, such as `robot.drive`.
- `form`: S-expression head symbol, such as `drive`, `if`, or `+`.
- `kind`: one of `command`, `control`, `expression`, `binding`, or `generic`.
- `category`: UI grouping.
- `label`: short display label.
- `template`: canonical source template with `{{param}}` placeholders.
- `params`: array of parameter schemas.
- `children`: optional child sequence role for control/binding cards.
- `branches`: optional ordered branch names.
- `variadic`: optional repeated expression parameter name.
- `highlight`: optional boolean indicating whether the engine should emit node events.
- `capability`: optional robot capability metadata.

## Parameter fields

- `name`: placeholder name.
- `kind`: `integer`, `boolean`, `enum`, `symbol`, `symbolList`, or `expression`.
- `label`: UI label.
- `default`: default value.
- `unit`, `min`, `max`, and `values`: optional UI constraints.

## Projection

The UI maps parsed S-expressions to specialized cards by matching `form` and
card shape. If no specialized card matches, the UI must preserve the expression
as a generic S-expression card so source can still round-trip.
```

- [ ] **Step 6: Run manifest tests**

Run: `npm run test:js`

Expected: PASS for smoke and manifest tests.

- [ ] **Step 7: Commit**

```bash
git add docs/integration/card-manifest.md app/cardcode-default.manifest.json app/src/manifest.js app/tests/manifest.test.js
git commit -m "feat: add card manifest contract"
```

## Task 3: Browser CardCode Parser

**Files:**
- Create: `app/src/parser.js`
- Create: `app/tests/parser.test.js`

- [ ] **Step 1: Write parser tests**

Create `app/tests/parser.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';

test('parses nested forms with spans', () => {
  const source = '(repeat 2 (drive 40 1000) (turn-right 90))';
  const ast = parseCardCode(source);

  assert.equal(ast.type, 'program');
  assert.equal(ast.children.length, 1);
  assert.equal(ast.children[0].type, 'list');
  assert.equal(ast.children[0].children[0].value, 'repeat');
  assert.deepEqual(ast.children[0].span, {
    startOffset: 0,
    endOffset: source.length,
    startLine: 1,
    startColumn: 1,
    endLine: 1,
    endColumn: source.length + 1
  });
});

test('skips semicolon comments and tracks following lines', () => {
  const source = '; lead comment\n(drive 40 1000)';
  const ast = parseCardCode(source);
  const drive = ast.children[0];

  assert.equal(drive.children[0].value, 'drive');
  assert.equal(drive.span.startLine, 2);
  assert.equal(drive.span.startColumn, 1);
});

test('reports unmatched paren with source span', () => {
  assert.throws(() => parseCardCode('(drive 40 1000'), /expected '\)'/);
});

test('parses all examples', async () => {
  for (const name of ['square', 'obstacle', 'line', 'patrol', 'approach']) {
    const source = await readFile(new URL(`../../examples/${name}.ccode`, import.meta.url), 'utf8');
    const ast = parseCardCode(source);
    assert.ok(ast.children.length > 0, `${name} should have top-level forms`);
  }
});
```

- [ ] **Step 2: Run parser tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/parser.js` does not exist.

- [ ] **Step 3: Implement the parser**

Create `app/src/parser.js`:

```js
export function parseCardCode(source) {
  const parser = new Parser(source);
  const children = [];
  while (!parser.atEnd()) {
    parser.skipIgnored();
    if (parser.atEnd()) break;
    children.push(parser.parseExpr());
  }
  return {
    type: 'program',
    children,
    span: spanFromOffsets(source, 0, source.length)
  };
}

class Parser {
  constructor(source) {
    this.source = source;
    this.offset = 0;
  }

  atEnd() {
    return this.offset >= this.source.length;
  }

  skipIgnored() {
    for (;;) {
      while (!this.atEnd() && /\s/.test(this.source[this.offset])) this.offset++;
      if (this.source[this.offset] !== ';') return;
      while (!this.atEnd() && this.source[this.offset] !== '\n') this.offset++;
    }
  }

  parseExpr() {
    this.skipIgnored();
    if (this.source[this.offset] === '(') return this.parseList();
    if (this.source[this.offset] === ')') throw this.error("unexpected ')'");
    return this.parseAtom();
  }

  parseList() {
    const start = this.offset++;
    const children = [];
    for (;;) {
      this.skipIgnored();
      if (this.atEnd()) throw this.error("expected ')'");
      if (this.source[this.offset] === ')') {
        this.offset++;
        return { type: 'list', children, span: spanFromOffsets(this.source, start, this.offset) };
      }
      children.push(this.parseExpr());
    }
  }

  parseAtom() {
    const start = this.offset;
    while (!this.atEnd() && !/\s|\(|\)|;/.test(this.source[this.offset])) this.offset++;
    const raw = this.source.slice(start, this.offset);
    const number = /^-?\d+$/.test(raw) ? Number(raw) : null;
    const value = number === null ? raw : number;
    const type = number === null ? 'symbol' : 'integer';
    return { type, value, raw, span: spanFromOffsets(this.source, start, this.offset) };
  }

  error(message) {
    const span = spanFromOffsets(this.source, this.offset, Math.min(this.source.length, this.offset + 1));
    const err = new Error(`${message} at ${span.startLine}:${span.startColumn}`);
    err.span = span;
    return err;
  }
}

export function spanFromOffsets(source, startOffset, endOffset) {
  const start = lineColumnAt(source, startOffset);
  const end = lineColumnAt(source, endOffset);
  return {
    startOffset,
    endOffset,
    startLine: start.line,
    startColumn: start.column,
    endLine: end.line,
    endColumn: end.column
  };
}

function lineColumnAt(source, offset) {
  let line = 1;
  let column = 1;
  for (let i = 0; i < offset; i++) {
    if (source[i] === '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return { line, column };
}
```

**Note on parser parity.** The browser parser above is a *tolerant round-trip* parser, not a faithful re-implementation of the C++ lexer. The C++ lexer (`src/lexer.cpp:9-20`) accepts a fixed allowlist of 11 punctuation chars (`_ - ? ! < > = + * / .`) plus alphanumerics; the browser parser stops only at whitespace, parens, or `;`. That means the C++ lexer will reject characters like `~ @ & : , [ ] { } "` that the JS parser would happily let flow into a single symbol — but no card in the v1 manifest uses any of those characters, so the two parsers agree on every program the UI produces today. If a future engine change adds a character outside the allowlist (for example, adopting `:` for keyword arguments or `[` for vector literals), or if a card template emits one, the parsers will diverge. Treat the engine's `compile` message as the authoritative parser; the browser parser exists only to keep the card tree and the source view in sync, and semantic validation always round-trips through `compile`.

- [ ] **Step 4: Run parser tests**

Run: `npm run test:js`

Expected: PASS for smoke, manifest, and parser tests.

- [ ] **Step 5: Commit**

```bash
git add app/src/parser.js app/tests/parser.test.js
git commit -m "feat: add browser cardcode parser"
```

## Task 4: Projection From Syntax Tree to Cards

**Files:**
- Create: `app/src/projection.js`
- Create: `app/tests/projection.test.js`

- [ ] **Step 1: Write projection tests**

Create `app/tests/projection.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';
import { loadManifestFromObject } from '../src/manifest.js';
import { projectProgram } from '../src/projection.js';

async function defaultManifest() {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return loadManifestFromObject(raw);
}

test('projects commands and controls into specialized cards', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(repeat 2 (drive 40 1000) (turn-right 90))');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards.length, 1);
  assert.equal(cards[0].cardId, 'control.repeat');
  assert.equal(cards[0].params.count.value, 2);
  assert.equal(cards[0].children.length, 2);
  assert.equal(cards[0].children[0].cardId, 'robot.drive');
  assert.equal(cards[0].children[0].params.speed.value, 40);
});

test('projects function definitions and calls', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(define (square n) (* n n)) (drive (square 6) 1000)');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards[0].cardId, 'binding.define-func');
  assert.equal(cards[0].params.name.value, 'square');
  assert.deepEqual(cards[0].params.params.value, ['n']);
  assert.equal(cards[1].cardId, 'robot.drive');
  assert.equal(cards[1].params.speed.value.type, 'call');
  assert.equal(cards[1].params.speed.value.form, 'square');
});

test('falls back to generic cards for unknown forms', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(custom-thing 1 true)');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards[0].cardId, 'generic.form');
  assert.equal(cards[0].form, 'custom-thing');
  assert.equal(cards[0].args.length, 2);
});
```

- [ ] **Step 2: Run projection tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/projection.js` does not exist.

- [ ] **Step 3: Implement projection**

Create `app/src/projection.js`:

```js
const RESERVED_LITERAL_SYMBOLS = new Set(['true', 'false', 'off', 'red', 'green', 'blue', 'yellow', 'white']);
const ZERO_PARAM_FORMS = new Set(['stop', 'beep', 'distance-cm', 'button', 'line-left', 'line-right']);

export function projectProgram(programAst, manifest) {
  return programAst.children.map((expr, index) => projectStatement(expr, manifest, `card-${index + 1}`));
}

function projectStatement(expr, manifest, id) {
  if (expr.type !== 'list' || expr.children.length === 0 || expr.children[0].type !== 'symbol') {
    return genericExpressionCard(expr, id);
  }

  const form = expr.children[0].value;
  if (form === 'define') return projectDefine(expr, manifest, id);
  if (form === 'repeat') return projectSequenceCard(expr, manifest, id, 'control.repeat', ['count'], 2);
  if (form === 'when') return projectSequenceCard(expr, manifest, id, 'control.when', ['condition'], 2);
  if (form === 'while') return projectSequenceCard(expr, manifest, id, 'control.while', ['condition'], 2);
  if (form === 'if') return projectIf(expr, manifest, id);

  const card = chooseCard(manifest, form, ['command', 'binding', 'control']);
  if (!card) return genericExpressionCard(expr, id);

  const args = expr.children.slice(1);
  const params = {};
  for (let i = 0; i < card.params.length; i++) {
    const param = card.params[i];
    params[param.name] = { value: projectValue(args[i]), span: args[i]?.span || expr.span };
  }
  return baseCard(id, card, expr.span, params);
}

function projectDefine(expr, manifest, id) {
  const head = expr.children[1];
  if (head?.type === 'list') {
    const card = manifest.byId.get('binding.define-func');
    const name = head.children[0]?.value || 'function';
    const params = head.children.slice(1).map((p) => p.value);
    return {
      ...baseCard(id, card, expr.span, {
        name: { value: name, span: head.children[0]?.span || head.span },
        params: { value: params, span: head.span }
      }),
      children: expr.children.slice(2).map((child, index) => projectStatement(child, manifest, `${id}-${index + 1}`))
    };
  }

  const card = manifest.byId.get('binding.define-var');
  return baseCard(id, card, expr.span, {
    name: { value: head?.value || 'x', span: head?.span || expr.span },
    value: { value: projectValue(expr.children[2]), span: expr.children[2]?.span || expr.span }
  });
}

function projectSequenceCard(expr, manifest, id, cardId, paramNames, childStart) {
  const card = manifest.byId.get(cardId);
  const params = {};
  for (let i = 0; i < paramNames.length; i++) {
    const valueExpr = expr.children[i + 1];
    params[paramNames[i]] = { value: projectValue(valueExpr), span: valueExpr?.span || expr.span };
  }
  return {
    ...baseCard(id, card, expr.span, params),
    children: expr.children.slice(childStart).map((child, index) => projectStatement(child, manifest, `${id}-${index + 1}`))
  };
}

function projectIf(expr, manifest, id) {
  const card = manifest.byId.get('control.if');
  const condition = expr.children[1];
  return {
    ...baseCard(id, card, expr.span, {
      condition: { value: projectValue(condition), span: condition?.span || expr.span }
    }),
    branches: {
      then: expr.children[2] ? [projectStatement(expr.children[2], manifest, `${id}-then-1`)] : [],
      else: expr.children[3] ? [projectStatement(expr.children[3], manifest, `${id}-else-1`)] : []
    }
  };
}

function chooseCard(manifest, form, kinds) {
  return (manifest.byForm.get(form) || []).find((card) => kinds.includes(card.kind));
}

function baseCard(id, manifestCard, span, params) {
  return {
    id,
    cardId: manifestCard.id,
    form: manifestCard.form,
    kind: manifestCard.kind,
    category: manifestCard.category,
    label: manifestCard.label,
    params,
    span
  };
}

export function projectValue(expr) {
  if (!expr) return null;
  if (expr.type === 'integer') return expr.value;
  if (expr.type === 'symbol') {
    if (expr.value === 'true') return true;
    if (expr.value === 'false') return false;
    if (RESERVED_LITERAL_SYMBOLS.has(expr.value)) return expr.value;
    return { type: 'var', name: expr.value, span: expr.span };
  }
  if (expr.type === 'list') {
    const form = expr.children[0]?.value || '';
    if (ZERO_PARAM_FORMS.has(form)) return { type: 'call', form, args: [], span: expr.span };
    return {
      type: 'call',
      form,
      args: expr.children.slice(1).map(projectValue),
      span: expr.span
    };
  }
  return null;
}

function genericExpressionCard(expr, id) {
  const form = expr.type === 'list' && expr.children[0]?.type === 'symbol' ? expr.children[0].value : 'expr';
  return {
    id,
    cardId: 'generic.form',
    form,
    kind: 'generic',
    category: 'generic',
    label: form,
    args: expr.type === 'list' ? expr.children.slice(1).map(projectValue) : [projectValue(expr)],
    span: expr.span
  };
}
```

- [ ] **Step 4: Run projection tests**

Run: `npm run test:js`

Expected: PASS for all current JS tests.

- [ ] **Step 5: Commit**

```bash
git add app/src/projection.js app/tests/projection.test.js
git commit -m "feat: project cardcode syntax to cards"
```

## Task 5: Card Tree Source Generation and Span Mapping

**Files:**
- Create: `app/src/generator.js`
- Create: `app/tests/generator.test.js`

- [ ] **Step 1: Write generator tests**

Create `app/tests/generator.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';
import { loadManifestFromObject } from '../src/manifest.js';
import { projectProgram } from '../src/projection.js';
import { generateProgram } from '../src/generator.js';

async function cardsFromSource(source) {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return projectProgram(parseCardCode(source), loadManifestFromObject(raw));
}

test('generates canonical source from projected cards', async () => {
  const cards = await cardsFromSource('(repeat 2 (drive 40 1000) (turn-right 90))');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(repeat 2\n  (drive 40 1000)\n  (turn-right 90))');
  assert.equal(generated.cardSpans.get(cards[0].id).startOffset, 0);
  assert.ok(generated.cardSpans.get(cards[0].children[0].id).startOffset > 0);
  assert.doesNotThrow(() => parseCardCode(generated.source));
});

test('generates functions and expression arguments', async () => {
  const cards = await cardsFromSource('(define (square n) (* n n)) (drive (square 6) 1000)');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(define (square n)\n  (* n n))\n(drive (square 6) 1000)');
});

test('generates generic forms without losing arguments', async () => {
  const cards = await cardsFromSource('(custom-thing 1 true)');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(custom-thing 1 true)');
});
```

- [ ] **Step 2: Run generator tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/generator.js` does not exist.

- [ ] **Step 3: Implement generation**

Create `app/src/generator.js`:

```js
export function generateProgram(cards) {
  const ctx = { source: '', cardSpans: new Map() };
  cards.forEach((card, index) => {
    if (index > 0) ctx.source += '\n';
    writeCard(ctx, card, 0);
  });
  return { source: ctx.source, cardSpans: ctx.cardSpans };
}

function writeCard(ctx, card, indent) {
  const start = ctx.source.length;
  switch (card.cardId) {
    case 'control.repeat':
      writeSequence(ctx, card, indent, 'repeat', emitValue(card.params.count.value));
      break;
    case 'control.when':
      writeSequence(ctx, card, indent, 'when', emitValue(card.params.condition.value));
      break;
    case 'control.while':
      writeSequence(ctx, card, indent, 'while', emitValue(card.params.condition.value));
      break;
    case 'control.if':
      writeIf(ctx, card, indent);
      break;
    case 'binding.define-func':
      writeFunction(ctx, card, indent);
      break;
    case 'binding.define-var':
      ctx.source += `${pad(indent)}(define ${card.params.name.value} ${emitValue(card.params.value.value)})`;
      break;
    case 'binding.set':
      ctx.source += `${pad(indent)}(set! ${card.params.name.value} ${emitValue(card.params.value.value)})`;
      break;
    case 'generic.form':
      ctx.source += `${pad(indent)}(${card.form}${card.args.length ? ' ' + card.args.map(emitValue).join(' ') : ''})`;
      break;
    default:
      writeSimpleForm(ctx, card, indent);
      break;
  }
  ctx.cardSpans.set(card.id, spanFromSource(ctx.source, start, ctx.source.length));
}

function writeSequence(ctx, card, indent, form, firstArg) {
  ctx.source += `${pad(indent)}(${form} ${firstArg}`;
  for (const child of card.children || []) {
    ctx.source += '\n';
    writeCard(ctx, child, indent + 1);
  }
  ctx.source += ')';
}

function writeIf(ctx, card, indent) {
  ctx.source += `${pad(indent)}(if ${emitValue(card.params.condition.value)}`;
  const thenCard = card.branches.then[0];
  const elseCard = card.branches.else[0];
  ctx.source += '\n';
  thenCard ? writeCard(ctx, thenCard, indent + 1) : ctx.source += `${pad(indent + 1)}(do)`;
  ctx.source += '\n';
  elseCard ? writeCard(ctx, elseCard, indent + 1) : ctx.source += `${pad(indent + 1)}(do)`;
  ctx.source += ')';
}

function writeFunction(ctx, card, indent) {
  const params = card.params.params.value.join(' ');
  ctx.source += `${pad(indent)}(define (${card.params.name.value}${params ? ' ' + params : ''})`;
  for (const child of card.children || []) {
    ctx.source += '\n';
    writeCard(ctx, child, indent + 1);
  }
  ctx.source += ')';
}

function writeSimpleForm(ctx, card, indent) {
  const args = Object.values(card.params || {}).map((param) => emitValue(param.value));
  ctx.source += `${pad(indent)}(${card.form}${args.length ? ' ' + args.join(' ') : ''})`;
}

export function emitValue(value) {
  if (value === null || value === undefined) return '()';
  if (typeof value === 'number') return String(value);
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'string') return value;
  if (value.type === 'var') return value.name;
  if (value.type === 'call') return `(${value.form}${value.args.length ? ' ' + value.args.map(emitValue).join(' ') : ''})`;
  throw new Error(`cannot emit value ${JSON.stringify(value)}`);
}

function pad(indent) {
  return '  '.repeat(indent);
}

function spanFromSource(source, startOffset, endOffset) {
  const start = lineColumnAt(source, startOffset);
  const end = lineColumnAt(source, endOffset);
  return {
    startOffset,
    endOffset,
    startLine: start.line,
    startColumn: start.column,
    endLine: end.line,
    endColumn: end.column
  };
}

function lineColumnAt(source, offset) {
  let line = 1;
  let column = 1;
  for (let i = 0; i < offset; i++) {
    if (source[i] === '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return { line, column };
}
```

- [ ] **Step 4: Run generator tests**

Run: `npm run test:js`

Expected: PASS for all current JS tests.

- [ ] **Step 5: Commit**

```bash
git add app/src/generator.js app/tests/generator.test.js
git commit -m "feat: generate cardcode from cards"
```

## Task 6: Runtime WebSocket Client

**Files:**
- Create: `app/src/transport.js`
- Create: `app/tests/transport.test.js`

- [ ] **Step 1: Write transport tests**

Create `app/tests/transport.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { RuntimeClient } from '../src/transport.js';

class FakeSocket {
  constructor() {
    this.sent = [];
    this.readyState = FakeSocket.OPEN;
  }
  send(data) {
    this.sent.push(JSON.parse(data));
  }
  close() {
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.();
  }
  receive(obj) {
    this.onmessage?.({ data: JSON.stringify(obj) });
  }
}
FakeSocket.OPEN = 1;
FakeSocket.CLOSED = 3;

test('runtime client sends protocol messages', () => {
  const socket = new FakeSocket();
  const client = new RuntimeClient({ socketFactory: () => socket });
  client.connect('ws://robot');
  client.compile('(drive 40 1000)');
  client.run('(drive 40 1000)', { maxTotalSteps: 20 });
  client.cancel();
  client.reset();
  client.setSensor('distance-cm', 12);

  assert.deepEqual(socket.sent, [
    { type: 'compile', source: '(drive 40 1000)' },
    { type: 'run', source: '(drive 40 1000)', limits: { maxTotalSteps: 20 } },
    { type: 'cancel' },
    { type: 'reset' },
    { type: 'setSensor', sensor: 'distance-cm', value: 12 }
  ]);
});

test('runtime client dispatches parsed messages', () => {
  const socket = new FakeSocket();
  const received = [];
  const client = new RuntimeClient({ socketFactory: () => socket, onMessage: (msg) => received.push(msg) });
  client.connect('ws://robot');

  socket.receive({ type: 'diagnostics', ok: true, diagnostics: [] });
  socket.receive({ type: 'programStart' });

  assert.deepEqual(received, [
    { type: 'diagnostics', ok: true, diagnostics: [] },
    { type: 'programStart' }
  ]);
});
```

- [ ] **Step 2: Run transport tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/transport.js` does not exist.

- [ ] **Step 3: Implement transport**

Create `app/src/transport.js`:

```js
export class RuntimeClient {
  constructor({ socketFactory = (url) => new WebSocket(url), onMessage = () => {}, onStatus = () => {} } = {}) {
    this.socketFactory = socketFactory;
    this.onMessage = onMessage;
    this.onStatus = onStatus;
    this.socket = null;
  }

  connect(url) {
    this.disconnect();
    this.socket = this.socketFactory(url);
    this.socket.onopen = () => this.onStatus({ connected: true, url });
    this.socket.onclose = () => this.onStatus({ connected: false, url });
    this.socket.onerror = () => this.onStatus({ connected: false, url, error: 'websocket error' });
    this.socket.onmessage = (event) => {
      try {
        this.onMessage(JSON.parse(event.data));
      } catch (err) {
        this.onMessage({ type: 'error', message: `malformed runtime message: ${err.message}` });
      }
    };
    return this.socket;
  }

  disconnect() {
    if (this.socket && this.socket.readyState !== 3) this.socket.close();
    this.socket = null;
  }

  compile(source) {
    this.send({ type: 'compile', source });
  }

  run(source, limits = undefined) {
    this.send(limits ? { type: 'run', source, limits } : { type: 'run', source });
  }

  cancel() {
    this.send({ type: 'cancel' });
  }

  reset() {
    this.send({ type: 'reset' });
  }

  setSensor(sensor, value) {
    this.send({ type: 'setSensor', sensor, value });
  }

  send(message) {
    if (!this.socket || this.socket.readyState !== 1) {
      throw new Error('runtime socket is not connected');
    }
    this.socket.send(JSON.stringify(message));
  }
}
```

- [ ] **Step 4: Run transport tests**

Run: `npm run test:js`

Expected: PASS for all current JS tests.

- [ ] **Step 5: Commit**

```bash
git add app/src/transport.js app/tests/transport.test.js
git commit -m "feat: add cardcode runtime client"
```

## Task 7: App State and Span Mapping

**Files:**
- Create: `app/src/state.js`
- Create: `app/tests/state.test.js`

- [ ] **Step 1: Write state tests**

Create `app/tests/state.test.js`:

```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { createInitialState, reduceState, cardsForSpan } from '../src/state.js';

test('state tracks diagnostics and run state', () => {
  let state = createInitialState();
  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'programStart' } });
  assert.equal(state.running, true);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'programError', message: 'execution cancelled' } });
  assert.equal(state.running, false);
  assert.equal(state.status, 'execution cancelled');
});

test('cardsForSpan maps overlapping spans', () => {
  const cards = [
    { id: 'a', span: { startOffset: 0, endOffset: 20 } },
    { id: 'b', span: { startOffset: 8, endOffset: 18 } },
    { id: 'c', span: { startOffset: 30, endOffset: 40 } }
  ];
  const hits = cardsForSpan(cards, { startOffset: 10, endOffset: 12 });

  assert.deepEqual(hits.map((card) => card.id), ['a', 'b']);
});
```

- [ ] **Step 2: Run state tests and verify failure**

Run: `npm run test:js`

Expected: FAIL because `app/src/state.js` does not exist.

- [ ] **Step 3: Implement state**

Create `app/src/state.js`:

```js
export function createInitialState() {
  return {
    source: '',
    cards: [],
    diagnostics: [],
    telemetry: [],
    connected: false,
    running: false,
    status: 'Disconnected',
    activeCardIds: new Set(),
    erroredCardIds: new Set(),
    sensors: {
      'distance-cm': 30,
      button: false,
      'line-left': false,
      'line-right': false
    }
  };
}

export function reduceState(state, action) {
  switch (action.type) {
    case 'connection':
      return { ...state, connected: action.connected, status: action.connected ? 'Connected' : 'Disconnected' };
    case 'edit':
      return { ...state, source: action.source, cards: action.cards, diagnostics: [], activeCardIds: new Set(), erroredCardIds: new Set() };
    case 'runtimeMessage':
      return applyRuntimeMessage(state, action.message);
    default:
      return state;
  }
}

function applyRuntimeMessage(state, message) {
  if (message.type === 'diagnostics') {
    return { ...state, diagnostics: message.diagnostics || [], status: message.ok ? 'Compiled' : 'Compile error' };
  }
  if (message.type === 'programStart') {
    return { ...state, running: true, status: 'Running', activeCardIds: new Set(), erroredCardIds: new Set() };
  }
  if (message.type === 'programDone') {
    return { ...state, running: false, status: 'Done', activeCardIds: new Set() };
  }
  if (message.type === 'programError' || message.type === 'error') {
    return { ...state, running: false, status: message.message || 'Error', activeCardIds: new Set() };
  }
  if (message.type === 'robotCommand') {
    return { ...state, telemetry: [...state.telemetry.slice(-49), message] };
  }
  if (message.type === 'sensorRead') {
    return { ...state, sensors: { ...state.sensors, [message.sensor]: message.value } };
  }
  if (message.type === 'node') {
    return applyNodeMessage(state, message);
  }
  return state;
}

function applyNodeMessage(state, message) {
  const activeCardIds = new Set(state.activeCardIds);
  const erroredCardIds = new Set(state.erroredCardIds);
  const hits = message.span ? cardsForSpan(flattenCards(state.cards), message.span) : [];
  for (const card of hits) {
    if (message.event === 'start') activeCardIds.add(card.id);
    if (message.event === 'done' || message.event === 'skipped') activeCardIds.delete(card.id);
    if (message.event === 'error') erroredCardIds.add(card.id);
  }
  return { ...state, activeCardIds, erroredCardIds };
}

export function cardsForSpan(cards, span) {
  return cards.filter((card) => card.span && overlaps(card.span, span));
}

export function flattenCards(cards) {
  const out = [];
  for (const card of cards) {
    out.push(card);
    if (card.children) out.push(...flattenCards(card.children));
    if (card.branches) {
      for (const branchCards of Object.values(card.branches)) out.push(...flattenCards(branchCards));
    }
  }
  return out;
}

function overlaps(a, b) {
  return a.startOffset < b.endOffset && b.startOffset < a.endOffset;
}
```

- [ ] **Step 4: Run state tests**

Run: `npm run test:js`

Expected: PASS for all current JS tests.

- [ ] **Step 5: Commit**

```bash
git add app/src/state.js app/tests/state.test.js
git commit -m "feat: add cardcode ui state model"
```

## Task 8: Browser App Shell and Renderer

**Files:**
- Create: `app/index.html`
- Create: `app/styles.css`
- Create: `app/src/render.js`
- Create: `app/src/app.js`

- [ ] **Step 1: Create the app shell**

Create `app/index.html`:

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CardCode</title>
  <link rel="stylesheet" href="./styles.css">
</head>
<body>
  <main class="app">
    <header class="toolbar">
      <div>
        <h1>CardCode</h1>
        <p id="status">Disconnected</p>
      </div>
      <label>
        Target
        <select id="target">
          <option value="mock">Mock harness</option>
          <option value="robot">Robot</option>
          <option value="custom">Custom URL</option>
        </select>
      </label>
      <label class="url-field">
        URL
        <input id="url" value="ws://localhost:8080">
      </label>
      <button id="connect">Connect</button>
      <button id="run">Run</button>
      <button id="stop">Stop</button>
      <button id="reset">Reset</button>
    </header>
    <section class="main-grid">
      <section class="panel">
        <div class="tabs">
          <button id="blocks-tab" class="active">Blocks</button>
          <button id="code-tab">Code</button>
        </div>
        <div id="blocks-view"></div>
        <textarea id="code-view" spellcheck="false"></textarea>
      </section>
      <aside class="side">
        <section>
          <h2>Diagnostics</h2>
          <div id="diagnostics"></div>
        </section>
        <section>
          <h2>Mock Sensors</h2>
          <label>Distance <input id="sensor-distance" type="number" value="30"></label>
          <label><input id="sensor-button" type="checkbox"> Button</label>
          <label><input id="sensor-line-left" type="checkbox"> Line left</label>
          <label><input id="sensor-line-right" type="checkbox"> Line right</label>
        </section>
        <section>
          <h2>Telemetry</h2>
          <ol id="telemetry"></ol>
        </section>
      </aside>
    </section>
  </main>
  <script type="module" src="./src/app.js"></script>
</body>
</html>
```

- [ ] **Step 2: Add CSS adapted from the prototype**

Create `app/styles.css`:

```css
:root {
  --bg: #f7f4ec;
  --panel: #ffffff;
  --ink: #1b1713;
  --muted: #6c6258;
  --line: #ded7ca;
  --accent: #f05a28;
  --motion: #2563eb;
  --logic: #0f766e;
  --loops: #b45309;
  --output: #15803d;
  --error: #b91c1c;
}

* { box-sizing: border-box; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--ink);
  font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
}
button, input, select, textarea { font: inherit; }
.app { min-height: 100vh; display: flex; flex-direction: column; }
.toolbar {
  display: grid;
  grid-template-columns: minmax(160px, 1fr) auto minmax(220px, 320px) repeat(4, auto);
  gap: 10px;
  align-items: end;
  padding: 14px 18px;
  border-bottom: 1px solid var(--line);
  background: var(--panel);
}
h1 { margin: 0; font-size: 20px; }
h2 { margin: 0 0 10px; font-size: 14px; }
#status { margin: 3px 0 0; color: var(--muted); font-size: 13px; }
label { display: grid; gap: 4px; color: var(--muted); font-size: 12px; }
input, select, textarea, button {
  border: 1px solid var(--line);
  border-radius: 8px;
  background: white;
  color: var(--ink);
  padding: 8px 10px;
}
button { cursor: pointer; font-weight: 650; }
#run { background: var(--ink); color: white; }
#stop { color: var(--error); }
.main-grid { display: grid; grid-template-columns: minmax(0, 1fr) 320px; gap: 16px; padding: 16px; min-height: 0; }
.panel, .side section {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
}
.panel { min-height: calc(100vh - 110px); overflow: hidden; }
.tabs { display: flex; gap: 6px; padding: 10px; border-bottom: 1px solid var(--line); }
.tabs button.active { background: var(--ink); color: white; }
#blocks-view { padding: 14px; overflow: auto; max-height: calc(100vh - 170px); }
#code-view {
  display: none;
  width: 100%;
  min-height: calc(100vh - 160px);
  border: 0;
  border-radius: 0;
  font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
  font-size: 14px;
  line-height: 1.5;
}
.show-code #blocks-view { display: none; }
.show-code #code-view { display: block; }
.side { display: grid; align-content: start; gap: 16px; }
.side section { padding: 14px; }
.card {
  border: 1px solid var(--line);
  border-left: 6px solid var(--muted);
  border-radius: 8px;
  background: white;
  margin: 0 0 8px;
  padding: 10px;
}
.card[data-category="motion"] { border-left-color: var(--motion); }
.card[data-category="logic"] { border-left-color: var(--logic); }
.card[data-category="loops"] { border-left-color: var(--loops); }
.card[data-category="output"] { border-left-color: var(--output); }
.card.active { outline: 2px solid var(--accent); }
.card.error { outline: 2px solid var(--error); }
.card-title { font-weight: 700; margin-bottom: 6px; }
.param { color: var(--muted); font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 12px; }
.children { margin: 8px 0 0 14px; padding-left: 10px; border-left: 1px dashed var(--line); }
.diagnostic { color: var(--error); font-size: 13px; margin-bottom: 8px; }
#telemetry { margin: 0; padding-left: 20px; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 12px; }
@media (max-width: 860px) {
  .toolbar { grid-template-columns: 1fr 1fr; }
  .url-field { grid-column: 1 / -1; }
  .main-grid { grid-template-columns: 1fr; }
}
```

- [ ] **Step 3: Create the renderer**

Create `app/src/render.js`:

```js
export function renderApp(state, actions) {
  document.getElementById('status').textContent = state.status;
  document.getElementById('code-view').value = state.source;
  renderCards(document.getElementById('blocks-view'), state.cards, state);
  renderDiagnostics(document.getElementById('diagnostics'), state.diagnostics);
  renderTelemetry(document.getElementById('telemetry'), state.telemetry);
  bindStaticActions(actions);
}

let bound = false;
function bindStaticActions(actions) {
  if (bound) return;
  bound = true;
  document.getElementById('connect').onclick = () => actions.connect(document.getElementById('url').value);
  document.getElementById('run').onclick = () => actions.run();
  document.getElementById('stop').onclick = () => actions.stop();
  document.getElementById('reset').onclick = () => actions.reset();
  document.getElementById('blocks-tab').onclick = () => showMode('blocks');
  document.getElementById('code-tab').onclick = () => showMode('code');
  document.getElementById('code-view').oninput = (event) => actions.editSource(event.target.value);
  document.getElementById('sensor-distance').onchange = (event) => actions.setSensor('distance-cm', Number(event.target.value));
  document.getElementById('sensor-button').onchange = (event) => actions.setSensor('button', event.target.checked);
  document.getElementById('sensor-line-left').onchange = (event) => actions.setSensor('line-left', event.target.checked);
  document.getElementById('sensor-line-right').onchange = (event) => actions.setSensor('line-right', event.target.checked);
}

function showMode(mode) {
  document.querySelector('.panel').classList.toggle('show-code', mode === 'code');
  document.getElementById('blocks-tab').classList.toggle('active', mode === 'blocks');
  document.getElementById('code-tab').classList.toggle('active', mode === 'code');
}

function renderCards(root, cards, state) {
  root.innerHTML = '';
  for (const card of cards) root.appendChild(renderCard(card, state));
}

function renderCard(card, state) {
  const el = document.createElement('article');
  el.className = 'card';
  el.dataset.category = card.category;
  el.classList.toggle('active', state.activeCardIds.has(card.id));
  el.classList.toggle('error', state.erroredCardIds.has(card.id));

  const title = document.createElement('div');
  title.className = 'card-title';
  title.textContent = card.label;
  el.appendChild(title);

  for (const [name, param] of Object.entries(card.params || {})) {
    const p = document.createElement('div');
    p.className = 'param';
    p.textContent = `${name}: ${formatParam(param.value)}`;
    el.appendChild(p);
  }

  if (card.args) {
    const p = document.createElement('div');
    p.className = 'param';
    p.textContent = `args: ${card.args.map(formatParam).join(' ')}`;
    el.appendChild(p);
  }

  if (card.children?.length) appendChildCards(el, card.children, state);
  if (card.branches) {
    for (const [branch, branchCards] of Object.entries(card.branches)) {
      const label = document.createElement('div');
      label.className = 'param';
      label.textContent = branch;
      el.appendChild(label);
      appendChildCards(el, branchCards, state);
    }
  }

  return el;
}

function appendChildCards(parent, cards, state) {
  const wrap = document.createElement('div');
  wrap.className = 'children';
  for (const child of cards) wrap.appendChild(renderCard(child, state));
  parent.appendChild(wrap);
}

function renderDiagnostics(root, diagnostics) {
  root.innerHTML = '';
  for (const diagnostic of diagnostics) {
    const el = document.createElement('div');
    el.className = 'diagnostic';
    el.textContent = diagnostic.span
      ? `${diagnostic.severity}: ${diagnostic.message} (${diagnostic.span.startLine}:${diagnostic.span.startColumn})`
      : `${diagnostic.severity}: ${diagnostic.message}`;
    root.appendChild(el);
  }
}

function renderTelemetry(root, telemetry) {
  root.innerHTML = '';
  for (const item of telemetry) {
    const el = document.createElement('li');
    el.textContent = item.command ? `${item.command} ${JSON.stringify(item.args || {})}` : JSON.stringify(item);
    root.appendChild(el);
  }
}

function formatParam(value) {
  if (value && typeof value === 'object') {
    if (value.type === 'var') return value.name;
    if (value.type === 'call') return `(${value.form} ${value.args.map(formatParam).join(' ')})`;
  }
  return String(value);
}
```

- [ ] **Step 4: Create app orchestration**

Create `app/src/app.js`:

```js
import { parseCardCode } from './parser.js';
import { loadManifest } from './manifest.js';
import { projectProgram } from './projection.js';
import { generateProgram } from './generator.js';
import { RuntimeClient } from './transport.js';
import { createInitialState, reduceState } from './state.js';
import { renderApp } from './render.js';

let manifest;
let state = createInitialState();
let compileTimer = null;

const client = new RuntimeClient({
  onMessage(message) {
    state = reduceState(state, { type: 'runtimeMessage', message });
    render();
  },
  onStatus(status) {
    state = reduceState(state, { type: 'connection', connected: status.connected });
    render();
  }
});

const actions = {
  connect(url) {
    client.connect(url);
  },
  run() {
    client.run(state.source, { maxTotalSteps: 1000, maxRepeatCount: 100, maxCallDepth: 64 });
  },
  stop() {
    client.cancel();
  },
  reset() {
    client.reset();
  },
  setSensor(sensor, value) {
    client.setSensor(sensor, value);
  },
  editSource(source) {
    applySource(source);
  }
};

async function boot() {
  manifest = await loadManifest();
  applySource('(repeat 4 (drive 40 1000) (turn-right 90))');
  render();
}

function applySource(source) {
  try {
    const ast = parseCardCode(source);
    const cards = projectProgram(ast, manifest);
    const generated = generateProgram(cards);
    state = reduceState(state, { type: 'edit', source: generated.source, cards });
    scheduleCompile();
  } catch (err) {
    state = { ...state, source, diagnostics: [{ severity: 'error', message: err.message, span: err.span }] };
  }
  render();
}

function scheduleCompile() {
  clearTimeout(compileTimer);
  compileTimer = setTimeout(() => {
    if (state.connected) client.compile(state.source);
  }, 250);
}

function render() {
  renderApp(state, actions);
}

boot().catch((err) => {
  state = { ...state, status: err.message };
  render();
});
```

- [ ] **Step 5: Run JS tests**

Run: `npm run test:js`

Expected: PASS for all pure module tests.

- [ ] **Step 6: Start a static app server**

Run: `python3 -m http.server 9000 --directory app`

Expected: server prints `Serving HTTP on 0.0.0.0 port 9000`.

- [ ] **Step 7: Manually smoke test the app**

Open `http://localhost:9000`.

Expected:

- Blocks view shows Repeat, Drive, and Turn right cards.
- Code tab shows `(repeat 4 (drive 40 1000) (turn-right 90))` in canonical multi-line formatting.
- Editing the Code tab to `(drive 40 1000)` updates Blocks to one Drive card.

- [ ] **Step 8: Commit**

```bash
git add app/index.html app/styles.css app/src/render.js app/src/app.js
git commit -m "feat: add cardcode browser app shell"
```

## Task 9: Harness Connection Smoke Test and README Link

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Build the harness**

Run:

```bash
cmake -S . -B build
cmake --build build
```

Expected: build succeeds and `build/cardcode-harness` exists.

- [ ] **Step 2: Start websocket harness**

Run:

```bash
websocketd --port=8080 ./build/cardcode-harness
```

Expected: websocketd listens on port 8080 and starts `cardcode-harness` per connection.

- [ ] **Step 3: Start app server**

Run: `python3 -m http.server 9000 --directory app`

Expected: static server listens on port 9000.

- [ ] **Step 4: Browser smoke test with harness**

Open `http://localhost:9000`, click Connect, then Run.

Expected:

- Status changes to Connected.
- Run sends the current CardCode source.
- Diagnostics panel stays empty for the starter program.
- Cards highlight as `node` messages arrive.
- Telemetry shows `drive_forward` and `turn_right`.
- Stop sends `cancel` and exits running state.

- [ ] **Step 5: Add README app instructions**

Modify `README.md` under the Integration section, after the local harness quick check:

````markdown
### Browser card UI

The browser UI lives in `app/` and talks to the same JSON protocol as a robot.

```bash
cmake -S . -B build
cmake --build build
websocketd --port=8080 ./build/cardcode-harness
python3 -m http.server 9000 --directory app
```

Open `http://localhost:9000`, connect to `ws://localhost:8080`, and run the
current CardCode program. The mock harness can also receive sensor overrides
from the UI.
````

- [ ] **Step 6: Run final verification**

Run:

```bash
npm run test:js
ctest --test-dir build --output-on-failure
```

Expected: JS tests and existing C++ tests pass.

- [ ] **Step 7: Commit**

```bash
git add README.md
git commit -m "docs: document cardcode browser ui"
```

## Self-Review

Spec coverage:

- Parser and source/card round trip: Task 3, Task 4, Task 5, Task 8.
- Manifest contract and default manifest: Task 2.
- Full current language coverage: Task 2 default manifest, Task 4 projection fallback, Task 5 generator.
- Runtime connection to mock harness or robot: Task 6, Task 8, Task 9.
- Diagnostics, node highlight, telemetry, sensor controls: Task 6, Task 7, Task 8, Task 9.
- Separation of concerns: File Structure plus Tasks 2 through 8.
- Tests: Tasks 1 through 7 and Task 9 final verification.

No placeholders remain in the implementation tasks. Deferred work is limited to
the spec's out-of-scope items: full visual grammar polish, comment preservation,
robot-advertised manifests, drag/drop expression composition, and WASM parser
integration.
