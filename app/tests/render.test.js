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
    'target',
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
  globalThis.window = {
    location: {
      protocol: 'http:',
      host: 'localhost:9000'
    }
  };
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
  assert.equal(document.getElementById('url').value, 'ws://localhost:9000/runtime');
  assert.equal(document.getElementById('code-view').value, state.source);
  const blocksView = document.getElementById('blocks-view');
  const topCards = blocksView.children.filter((child) => child.classList.has('card'));
  assert.equal(topCards.length, 1);
  assert.equal(blocksView.children.some((child) => (child.className || '').includes('add-step')), true);

  const repeat = topCards[0];
  assert.equal(repeat.dataset.category, 'loops');
  assert.equal(repeat.dataset.label, 'Repeat');
  assert.equal(repeat.classList.has('active'), true);
  assert.equal(cardOwnTexts(repeat).includes('Repeat'), true);
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
  document.getElementById('target').value = 'robot';
  document.getElementById('target').onchange();

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
  assert.equal(document.getElementById('url').value, 'ws://cardcode.local/cardcode');
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

function isCard(node) {
  return Boolean(node.classList?.has?.('card'));
}

function allCards(root, acc = []) {
  if (isCard(root)) acc.push(root);
  for (const child of root.children || []) allCards(child, acc);
  return acc;
}

// Text belonging to a card, excluding text inside any nested card.
function cardOwnTexts(card) {
  const out = [];
  (function walk(node, isRoot) {
    if (!isRoot && isCard(node)) return;
    if (node.textContent) out.push(node.textContent);
    for (const child of node.children || []) walk(child, false);
  })(card, true);
  return out;
}

function findCardByTitle(root, title) {
  return allCards(root).find((card) => cardOwnTexts(card).includes(title)) || null;
}
