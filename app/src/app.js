import { parseCardCode } from './parser.js';
import { loadManifest } from './manifest.js';
import { projectProgram } from './projection.js';
import { generateProgram } from './generator.js';
import { RuntimeClient } from './transport.js';
import { createInitialState, reduceState } from './state.js';
import { renderApp } from './render.js';

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
  }
};

async function boot() {
  render();
  manifest = await loadManifest();
  applySource(DEFAULT_SOURCE);
}

function applySource(source) {
  if (!manifest) {
    state = { ...state, source };
    render();
    return;
  }

  try {
    const ast = parseCardCode(source);
    const cards = projectProgram(ast, manifest);
    const generated = generateProgram(cards);
    state = reduceState(state, { type: 'edit', source: generated.source, cards });
    scheduleCompile();
  } catch (err) {
    state = {
      ...state,
      source,
      diagnostics: [{ severity: 'error', message: err.message, span: err.span }],
      activeCardIds: new Set(),
      erroredCardIds: new Set()
    };
  }
  render();
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

boot().catch((err) => {
  state = {
    ...state,
    status: err.message,
    diagnostics: [{ severity: 'error', message: err.message }]
  };
  render();
});
