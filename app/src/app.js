import { parseCardCode } from './parser.js';
import { loadManifest } from './manifest.js';
import { projectProgram, projectValue } from './projection.js';
import { generateProgram } from './generator.js';
import { RuntimeClient } from './transport.js';
import { createInitialState, reduceState } from './state.js';
import { renderApp } from './render.js';
import { cloneCards, deleteCardById, insertCard, updateParamValue, buildCardFromManifest } from './edit.js';

const DEFAULT_SOURCE = '(repeat 4 (drive 40 1000) (turn-right 90))';
const RUN_LIMITS = { maxTotalSteps: 1000, maxRepeatCount: 100, maxCallDepth: 64 };
const COMPILE_DEBOUNCE_MS = 250;

let manifest = null;
let state = createInitialState();
let compileTimer = null;

const client = new RuntimeClient({
  onMessage(message) {
    state = reduceState(state, { type: 'runtimeMessage', message });
    render();
  },
  onStatus(status) {
    state = reduceState(state, { type: 'connection', connected: status.connected });
    if (status.error) {
      state = {
        ...state,
        diagnostics: [{ severity: 'error', message: status.error }]
      };
    }
    if (status.connected) scheduleCompile();
    render();
  }
});

const actions = {
  connect(url) {
    callRuntime(() => client.connect(url));
  },
  run() {
    callRuntime(() => client.run(state.source, RUN_LIMITS));
  },
  stop() {
    callRuntime(() => client.cancel());
  },
  reset() {
    callRuntime(() => client.reset());
  },
  setSensor(sensor, value) {
    state = { ...state, sensors: { ...state.sensors, [sensor]: value } };
    callRuntime(() => client.setSensor(sensor, value), { quietWhenDisconnected: true });
    render();
  },
  editSource(source) {
    applySource(source);
  },
  insertCard(target, manifestCardId) {
    const manifestCard = manifest?.byId.get(manifestCardId);
    if (!manifestCard) return;
    mutateTree((tree) => insertCard(tree, target, buildCardFromManifest(manifestCard)));
  },
  deleteCard(id) {
    mutateTree((tree) => deleteCardById(tree, id));
  },
  setParam(id, name, value) {
    mutateTree((tree) => updateParamValue(tree, id, name, value));
  },
  setParamExpression(id, name, text) {
    let value;
    try {
      value = parseExpressionValue(text);
    } catch (err) {
      state = { ...state, diagnostics: [{ severity: 'error', message: err.message }] };
      render();
      return;
    }
    mutateTree((tree) => updateParamValue(tree, id, name, value));
  }
};

// Apply a structural edit to the card tree, regenerate source, and re-apply it.
// Re-applying re-projects fresh cards (and reassigns ids/spans) so the model
// stays the single source of truth.
function mutateTree(mutator) {
  if (!manifest) return;
  const tree = cloneCards(state.cards);
  if (mutator(tree) === false) return;
  applySource(generateProgram(tree).source, { canonicalize: true });
}

function parseExpressionValue(text) {
  const ast = parseCardCode(text);
  if (ast.children.length !== 1) throw new Error('enter a single expression');
  return projectValue(ast.children[0]);
}

async function boot() {
  render();
  manifest = await loadManifest();
  state = { ...state, manifest };
  if (state.source === '') {
    applySource(DEFAULT_SOURCE, { canonicalize: true });
  } else {
    applySource(state.source);
  }
}

function applySource(source, options = {}) {
  if (!manifest) {
    state = { ...state, source };
    render();
    return;
  }

  const result = applySourceToState(state, manifest, source, options);
  state = result.state;
  if (result.ok) {
    scheduleCompile();
  }
  render();
}

export function applySourceToState(currentState, manifestValue, source, options = {}) {
  try {
    const projected = projectSource(source, manifestValue);
    const nextSource = options.canonicalize ? projected.generated.source : source;
    const nextProjection = options.canonicalize ? projectSource(nextSource, manifestValue) : projected;
    const nextState = reduceState(currentState, { type: 'edit', source: nextSource, cards: nextProjection.cards });
    return {
      ok: true,
      generated: nextProjection.generated,
      state: {
        ...nextState,
        generatedSource: nextProjection.generated.source,
        cardSpans: nextProjection.generated.cardSpans
      }
    };
  } catch (err) {
    return {
      ok: false,
      error: err,
      state: {
        ...currentState,
        source,
        diagnostics: [{ severity: 'error', message: err.message, span: err.span }],
        activeCardIds: new Set(),
        erroredCardIds: new Set()
      }
    };
  }
}

function projectSource(source, manifestValue) {
  const ast = parseCardCode(source);
  const cards = projectProgram(ast, manifestValue);
  const generated = generateProgram(cards);
  return { cards, generated };
}

function scheduleCompile() {
  clearTimeout(compileTimer);
  compileTimer = setTimeout(() => {
    if (!state.connected) return;
    callRuntime(() => client.compile(state.source));
  }, COMPILE_DEBOUNCE_MS);
}

function callRuntime(fn, options = {}) {
  try {
    fn();
  } catch (err) {
    if (options.quietWhenDisconnected && /not connected/.test(err.message)) return;
    state = {
      ...state,
      status: err.message,
      diagnostics: [{ severity: 'error', message: err.message }]
    };
    render();
  }
}

function render() {
  renderApp(state, actions);
}

if (typeof document !== 'undefined') {
  boot().catch((err) => {
    state = {
      ...state,
      status: err.message,
      diagnostics: [{ severity: 'error', message: err.message }]
    };
    render();
  });
}
