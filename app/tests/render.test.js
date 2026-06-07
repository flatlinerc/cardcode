import test from 'node:test';
import assert from 'node:assert/strict';

class FakeClassList {
  constructor(element) {
    this.element = element;
    this.classes = new Set();
  }

  add(name) {
    this.classes.add(name);
    this.sync();
  }

  toggle(name, force) {
    const shouldAdd = force ?? !this.classes.has(name);
    if (shouldAdd) {
      this.classes.add(name);
    } else {
      this.classes.delete(name);
    }
    this.sync();
  }

  has(name) {
    return this.classes.has(name);
  }

  sync() {
    this.element.className = [...this.classes].join(' ');
  }
}

class FakeElement {
  constructor(tagName, id = '') {
    this.tagName = tagName;
    this.id = id;
    this.children = [];
    this.dataset = {};
    this.classList = new FakeClassList(this);
    this.className = '';
    this.textContent = '';
    this._value = '';
    this.valueSetCount = 0;
    this.checked = false;
    this.onclick = null;
    this.oninput = null;
    this.onchange = null;
  }

  appendChild(child) {
    this.children.push(child);
    return child;
  }

  set innerHTML(value) {
    this.children = [];
    this.textContent = value;
  }

  get innerHTML() {
    return this.textContent;
  }

  set value(nextValue) {
    this.valueSetCount++;
    this._value = nextValue;
  }

  get value() {
    return this._value;
  }
}

class FakeDocument {
  constructor() {
    this.elements = new Map();
  }

  add(tagName, id, classNames = []) {
    const element = new FakeElement(tagName, id);
    for (const name of classNames) element.classList.add(name);
    this.elements.set(id, element);
    return element;
  }

  getElementById(id) {
    return this.elements.get(id);
  }

  createElement(tagName) {
    return new FakeElement(tagName);
  }

  querySelector(selector) {
    if (selector === '.panel') return this.elements.get('panel');
    return null;
  }
}

function installDocument() {
  const document = new FakeDocument();
  for (const id of [
    'status',
    'code-view',
    'blocks-view',
    'diagnostics',
    'telemetry',
    'connect',
    'run',
    'stop',
    'reset',
    'blocks-tab',
    'code-tab',
    'url',
    'sensor-distance',
    'sensor-button',
    'sensor-line-left',
    'sensor-line-right'
  ]) {
    document.add(id === 'code-view' ? 'textarea' : 'div', id);
  }
  document.add('section', 'panel', ['panel']);
  globalThis.document = document;
  return document;
}

test('renderApp renders cards recursively and binds static controls once', async () => {
  const { renderApp } = await import('../src/render.js?render-test');
  const document = installDocument();
  const calls = [];
  const actions = {
    connect: (url) => calls.push(['connect', url]),
    run: () => calls.push(['run']),
    stop: () => calls.push(['stop']),
    reset: () => calls.push(['reset']),
    editSource: (source) => calls.push(['editSource', source]),
    setSensor: (sensor, value) => calls.push(['setSensor', sensor, value])
  };

  const state = {
    status: 'Running',
    source: '(repeat 1 (drive 40 1000))',
    activeCardIds: new Set(['repeat']),
    erroredCardIds: new Set(['branch-drive']),
    diagnostics: [{ severity: 'error', message: 'bad form', span: { startLine: 1, startColumn: 2 } }],
    telemetry: [{ type: 'robotCommand', command: 'drive_forward', args: { speed: 40 } }],
    cards: [
      {
        id: 'repeat',
        label: 'Repeat',
        category: 'loops',
        params: { count: { value: 1 } },
        children: [{ id: 'child-drive', label: 'Drive', category: 'motion', params: { speed: { value: 40 } } }],
        branches: {
          else: [{ id: 'branch-drive', label: 'Fallback', category: 'motion', params: {} }]
        }
      }
    ]
  };

  renderApp(state, actions);
  renderApp({ ...state, status: 'Done' }, actions);

  assert.equal(document.getElementById('status').textContent, 'Done');
  assert.equal(document.getElementById('code-view').value, state.source);
  assert.equal(document.getElementById('blocks-view').children.length, 1);

  const repeat = document.getElementById('blocks-view').children[0];
  assert.equal(repeat.dataset.category, 'loops');
  assert.equal(repeat.classList.has('active'), true);
  assert.equal(repeat.children.some((child) => child.textContent === 'Repeat'), true);
  assert.equal(findCardByTitle(repeat, 'Drive').dataset.category, 'motion');
  assert.equal(findCardByTitle(repeat, 'Fallback').classList.has('error'), true);

  assert.match(document.getElementById('diagnostics').children[0].textContent, /error: bad form \(1:2\)/);
  assert.match(document.getElementById('telemetry').children[0].textContent, /drive_forward/);

  document.getElementById('url').value = 'ws://robot';
  document.getElementById('connect').onclick();
  document.getElementById('run').onclick();
  document.getElementById('stop').onclick();
  document.getElementById('reset').onclick();
  document.getElementById('code-view').oninput({ target: { value: '(stop)' } });
  document.getElementById('sensor-distance').onchange({ target: { value: '12' } });
  document.getElementById('sensor-button').onchange({ target: { checked: true } });
  document.getElementById('code-tab').onclick();

  assert.deepEqual(calls, [
    ['connect', 'ws://robot'],
    ['run'],
    ['stop'],
    ['reset'],
    ['editSource', '(stop)'],
    ['setSensor', 'distance-cm', 12],
    ['setSensor', 'button', true]
  ]);
  assert.equal(document.getElementById('panel').classList.has('show-code'), true);
  assert.equal(document.getElementById('code-tab').classList.has('active'), true);
});

test('renderApp does not rewrite an unchanged code textarea value', async () => {
  const { renderApp } = await import('../src/render.js?render-preserve-test');
  const document = installDocument();
  const codeView = document.getElementById('code-view');
  codeView.value = '(drive 40 1000)\n';
  const writesBeforeRender = codeView.valueSetCount;

  renderApp({
    status: 'Ready',
    source: '(drive 40 1000)\n',
    activeCardIds: new Set(),
    erroredCardIds: new Set(),
    diagnostics: [],
    telemetry: [],
    cards: [],
    sensors: {}
  }, {
    connect() {},
    run() {},
    stop() {},
    reset() {},
    editSource() {},
    setSensor() {}
  });

  assert.equal(codeView.value, '(drive 40 1000)\n');
  assert.equal(codeView.valueSetCount, writesBeforeRender);
});

function findCardByTitle(root, title) {
  if (root.classList?.has('card') && root.children.some((child) => child.textContent === title)) return root;
  for (const child of root.children) {
    const match = findCardByTitle(child, title);
    if (match) return match;
  }
  return null;
}
