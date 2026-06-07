export function renderApp(state, actions) {
  document.getElementById('status').textContent = state.status;
  const codeView = document.getElementById('code-view');
  if (codeView.value !== state.source) codeView.value = state.source;
  renderCards(document.getElementById('blocks-view'), state.cards, state);
  renderDiagnostics(document.getElementById('diagnostics'), state.diagnostics);
  renderTelemetry(document.getElementById('telemetry'), state.telemetry);
  renderSensors(state.sensors || {});
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
  const showingCode = mode === 'code';
  document.querySelector('.panel').classList.toggle('show-code', showingCode);
  document.getElementById('blocks-tab').classList.toggle('active', !showingCode);
  document.getElementById('code-tab').classList.toggle('active', showingCode);
  document.getElementById('blocks-tab').setAttribute?.('aria-selected', String(!showingCode));
  document.getElementById('code-tab').setAttribute?.('aria-selected', String(showingCode));
}

function renderCards(root, cards, state) {
  root.innerHTML = '';
  if (!cards || cards.length === 0) {
    root.appendChild(emptyMessage('No blocks'));
    return;
  }
  for (const card of cards) root.appendChild(renderCard(card, state));
}

function renderCard(card, state) {
  const el = document.createElement('article');
  el.classList.add('card');
  el.dataset.category = card.category || 'generic';
  el.classList.toggle('active', state.activeCardIds?.has(card.id));
  el.classList.toggle('error', state.erroredCardIds?.has(card.id));

  const title = document.createElement('div');
  title.className = 'card-title';
  title.textContent = card.label || card.form || card.cardId || card.id;
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
      label.className = 'branch-label';
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
  if (cards.length === 0) {
    wrap.appendChild(emptyMessage('Empty'));
  } else {
    for (const child of cards) wrap.appendChild(renderCard(child, state));
  }
  parent.appendChild(wrap);
}

function renderDiagnostics(root, diagnostics) {
  root.innerHTML = '';
  if (!diagnostics || diagnostics.length === 0) {
    root.appendChild(emptyMessage('No diagnostics'));
    return;
  }
  for (const diagnostic of diagnostics) {
    const el = document.createElement('div');
    el.className = `diagnostic ${diagnostic.severity || 'error'}`;
    el.textContent = diagnostic.span
      ? `${diagnostic.severity}: ${diagnostic.message} (${diagnostic.span.startLine}:${diagnostic.span.startColumn})`
      : `${diagnostic.severity}: ${diagnostic.message}`;
    root.appendChild(el);
  }
}

function renderTelemetry(root, telemetry) {
  root.innerHTML = '';
  if (!telemetry || telemetry.length === 0) {
    root.appendChild(emptyMessage('No telemetry'));
    return;
  }
  for (const item of telemetry) {
    const el = document.createElement('li');
    el.textContent = item.command ? `${item.command} ${JSON.stringify(item.args || {})}` : JSON.stringify(item);
    root.appendChild(el);
  }
}

function renderSensors(sensors) {
  const distance = document.getElementById('sensor-distance');
  const button = document.getElementById('sensor-button');
  const lineLeft = document.getElementById('sensor-line-left');
  const lineRight = document.getElementById('sensor-line-right');

  if (document.activeElement !== distance) distance.value = String(sensors['distance-cm'] ?? 30);
  button.checked = Boolean(sensors.button);
  lineLeft.checked = Boolean(sensors['line-left']);
  lineRight.checked = Boolean(sensors['line-right']);
}

function emptyMessage(text) {
  const el = document.createElement('div');
  el.className = 'empty';
  el.textContent = text;
  return el;
}

function formatParam(value) {
  if (Array.isArray(value)) return value.join(' ');
  if (value && typeof value === 'object') {
    if (value.type === 'var') return value.name;
    if (value.type === 'call') {
      const args = value.args.length ? ` ${value.args.map(formatParam).join(' ')}` : '';
      return `(${value.form}${args})`;
    }
  }
  return String(value);
}
