export function renderApp(state, actions) {
  document.getElementById('status').textContent = state.status;
  const codeView = document.getElementById('code-view');
  if (codeView.value !== state.source) codeView.value = state.source;
  renderCards(document.getElementById('blocks-view'), state.cards, state, actions);
  renderDiagnostics(document.getElementById('diagnostics'), state.diagnostics);
  renderTelemetry(document.getElementById('telemetry'), state.telemetry);
  renderSensors(state.sensors || {});
  bindStaticActions(actions);
}

let bound = false;

function bindStaticActions(actions) {
  if (bound) return;
  bound = true;

  const target = document.getElementById('target');
  const url = document.getElementById('url');
  const mockUrl = defaultMockUrl();
  if (mockUrl && (url.value === '' || url.value === 'ws://localhost:9000/runtime')) url.value = mockUrl;

  target.onchange = () => {
    if (target.value === 'mock' && mockUrl) url.value = mockUrl;
    if (target.value === 'robot') url.value = 'ws://cardcode.local/cardcode';
  };

  document.getElementById('connect').onclick = () => actions.connect(url.value);
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

function defaultMockUrl() {
  if (typeof window === 'undefined' || !window.location?.host) return '';
  const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${scheme}://${window.location.host}/runtime`;
}

function showMode(mode) {
  const showingCode = mode === 'code';
  document.querySelector('.panel').classList.toggle('show-code', showingCode);
  document.getElementById('blocks-tab').classList.toggle('active', !showingCode);
  document.getElementById('code-tab').classList.toggle('active', showingCode);
  document.getElementById('blocks-tab').setAttribute?.('aria-selected', String(!showingCode));
  document.getElementById('code-tab').setAttribute?.('aria-selected', String(showingCode));
}

// ============== ICONS ==============
// Keyed first by form, then by category. Inline single-stroke SVGs.
const ICONS = {
  drive: '<svg viewBox="0 0 24 24"><path d="M12 19V5M5 12l7-7 7 7"/></svg>',
  backward: '<svg viewBox="0 0 24 24"><path d="M12 5v14M5 12l7 7 7-7"/></svg>',
  'turn-left': '<svg viewBox="0 0 24 24"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>',
  'turn-right': '<svg viewBox="0 0 24 24"><path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/></svg>',
  stop: '<svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="2"/></svg>',
  wait: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>',
  beep: '<svg viewBox="0 0 24 24"><path d="M11 5L6 9H2v6h4l5 4V5z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/></svg>',
  light: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="5"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2"/></svg>',
  repeat: '<svg viewBox="0 0 24 24"><path d="M3 12a9 9 0 0 1 15-6.7L21 8M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-15 6.7L3 16M3 21v-5h5"/></svg>',
  while: '<svg viewBox="0 0 24 24"><path d="M3 12a9 9 0 0 1 15-6.7L21 8M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-15 6.7L3 16M3 21v-5h5"/></svg>',
  if: '<svg viewBox="0 0 24 24"><path d="M6 3v12M18 9v12M6 9a6 6 0 0 0 6 6h6"/></svg>',
  when: '<svg viewBox="0 0 24 24"><path d="M6 3v12M18 9v12M6 9a6 6 0 0 0 6 6h6"/></svg>',
  do: '<svg viewBox="0 0 24 24"><path d="M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01"/></svg>',
  define: '<svg viewBox="0 0 24 24"><path d="M4 7V4h16v3M9 20h6M12 4v16"/></svg>',
  'set!': '<svg viewBox="0 0 24 24"><path d="M4 7V4h16v3M9 20h6M12 4v16"/></svg>',
  motion: '<svg viewBox="0 0 24 24"><path d="M5 12h14M13 6l6 6-6 6"/></svg>',
  logic: '<svg viewBox="0 0 24 24"><path d="M6 4v16M18 4v16M6 8h12M18 16H6"/></svg>',
  loops: '<svg viewBox="0 0 24 24"><path d="M17 2l4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="M7 22l-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/></svg>',
  variables: '<svg viewBox="0 0 24 24"><path d="M4 7V4h16v3M9 20h6M12 4v16"/></svg>',
  functions: '<svg viewBox="0 0 24 24"><path d="M4 7V4h16v3M9 20h6M12 4v16"/></svg>',
  output: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/></svg>',
  control: '<svg viewBox="0 0 24 24"><path d="M5 12h14M13 6l6 6-6 6"/></svg>',
  time: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>',
  sensors: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M5.6 18.4l2.1-2.1M16.3 7.7l2.1-2.1"/></svg>',
  operators: '<svg viewBox="0 0 24 24"><path d="M5 9h14M5 15h14"/></svg>',
  generic: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M9 9a3 3 0 1 1 4 2.8c-.6.3-1 .9-1 1.7M12 17h.01"/></svg>'
};

const PLUS_ICON = '<svg viewBox="0 0 24 24"><path d="M12 5v14M5 12h14"/></svg>';

function iconFor(card) {
  return ICONS[card.form] || ICONS[card.category] || ICONS.generic;
}

// ============== CARD TREE ==============
function renderCards(root, cards, state, actions) {
  root.innerHTML = '';
  renderList(root, cards, state, actions, { parentId: null, branch: null });
}

// Renders a list of cards followed by an "add step" button bound to `target`.
function renderList(container, cards, state, actions, target) {
  if (!cards || cards.length === 0) {
    container.appendChild(emptyMessage('No blocks yet'));
  } else {
    for (const card of cards) container.appendChild(renderCard(card, state, actions));
  }
  container.appendChild(addStepButton(actions, state, target));
}

function renderCard(card, state, actions) {
  const el = document.createElement('article');
  el.classList.add('card');
  el.dataset.category = card.category || 'generic';
  el.dataset.label = card.label || card.form || '';
  el.classList.toggle('active', state.activeCardIds?.has(card.id));
  el.classList.toggle('error', state.erroredCardIds?.has(card.id));

  const body = document.createElement('div');
  body.className = 'card-body';

  const stripe = document.createElement('div');
  stripe.className = 'card-stripe';
  body.appendChild(stripe);

  const icon = document.createElement('div');
  icon.className = 'card-icon';
  icon.innerHTML = iconFor(card);
  body.appendChild(icon);

  const label = document.createElement('div');
  label.className = 'card-label';
  label.textContent = card.label || card.form || card.cardId || card.id;
  appendParamPills(label, card, state, actions);
  appendArgPills(label, card);
  body.appendChild(label);

  const del = document.createElement('button');
  del.className = 'card-delete';
  del.title = 'Delete block';
  del.setAttribute?.('aria-label', 'Delete block');
  del.innerHTML = '<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></svg>';
  del.onclick = (event) => {
    event.stopPropagation();
    actions.deleteCard?.(card.id);
  };
  body.appendChild(del);

  el.appendChild(body);

  if (card.children) {
    appendNested(el, card.children, state, actions, { parentId: card.id, branch: null });
  }
  if (card.branches) {
    for (const [branch, branchCards] of Object.entries(card.branches)) {
      const wrap = document.createElement('div');
      wrap.className = 'container-content';
      const branchLabel = document.createElement('div');
      branchLabel.className = 'branch-label';
      branchLabel.textContent = branch;
      wrap.appendChild(branchLabel);
      appendNested(wrap, branchCards, state, actions, { parentId: card.id, branch });
      el.appendChild(wrap);
    }
  }

  return el;
}

function appendNested(parent, cards, state, actions, target) {
  const wrap = document.createElement('div');
  wrap.className = 'children';
  renderList(wrap, cards, state, actions, target);
  parent.appendChild(wrap);
}

function appendParamPills(label, card, state, actions) {
  if (!card.params) return;
  for (const [name, param] of Object.entries(card.params)) {
    const pill = document.createElement('span');
    const compound = isCompound(param.value);
    pill.className = compound ? 'pill compound' : 'pill';
    pill.textContent = formatParam(param.value) + unitFor(card, name, state);
    pill.title = `Edit ${name}`;
    pill.onclick = (event) => {
      event.stopPropagation();
      openParamEditor(card, name, state, actions);
    };
    label.appendChild(pill);
  }
}

function appendArgPills(label, card) {
  if (!card.args) return;
  for (const arg of card.args) {
    const pill = document.createElement('span');
    pill.className = 'pill readonly';
    pill.textContent = formatParam(arg);
    label.appendChild(pill);
  }
}

function addStepButton(actions, state, target) {
  const btn = document.createElement('button');
  btn.className = 'add-step';
  btn.innerHTML = `${PLUS_ICON}<span>Add step</span>`;
  btn.onclick = (event) => {
    event.stopPropagation();
    openPicker(target, state, actions);
  };
  return btn;
}

// ============== MODAL SHELL ==============
let currentOverlay = null;

function openModal(build) {
  closeModal();
  if (!document.body) return;
  const overlay = document.createElement('div');
  overlay.className = 'cc-overlay';
  overlay.onclick = (event) => {
    if (event.target === overlay) closeModal();
  };
  const sheet = document.createElement('div');
  sheet.className = 'cc-sheet';
  overlay.appendChild(sheet);
  build(sheet, closeModal);
  document.body.appendChild(overlay);
  currentOverlay = overlay;
}

function closeModal() {
  if (currentOverlay) {
    currentOverlay.remove?.();
    currentOverlay = null;
  }
}

function sheetHeader(sheet, title, subtitle) {
  const head = document.createElement('div');
  head.className = 'cc-sheet-title';
  head.textContent = title;
  if (subtitle) {
    const small = document.createElement('small');
    small.textContent = subtitle;
    head.appendChild(small);
  }
  sheet.appendChild(head);
}

// ============== BLOCK PICKER ==============
const PICKABLE_KINDS = new Set(['command', 'control', 'binding']);

function openPicker(target, state, actions) {
  const manifest = state.manifest;
  if (!manifest) return;
  const groups = new Map();
  for (const card of manifest.cards) {
    if (!PICKABLE_KINDS.has(card.kind)) continue;
    if (!groups.has(card.category)) groups.set(card.category, []);
    groups.get(card.category).push(card);
  }

  openModal((sheet) => {
    sheetHeader(sheet, 'Add a step', 'Choose what the robot should do next');
    const content = document.createElement('div');
    content.className = 'cc-sheet-content';

    const categories = [...groups.keys()];
    let active = categories[0];

    const grid = document.createElement('div');
    grid.className = 'cat-grid';
    const list = document.createElement('div');
    list.className = 'block-list';

    const drawList = () => {
      list.innerHTML = '';
      for (const card of groups.get(active) || []) {
        const opt = document.createElement('button');
        opt.className = 'block-option';
        opt.innerHTML = `<span class="block-option-icon">${ICONS[card.form] || ICONS[card.category] || ICONS.generic}</span>`;
        const text = document.createElement('span');
        text.className = 'block-option-text';
        const nameEl = document.createElement('span');
        nameEl.className = 'block-option-name';
        nameEl.textContent = card.label;
        const descEl = document.createElement('span');
        descEl.className = 'block-option-desc';
        descEl.textContent = describeCard(card);
        text.appendChild(nameEl);
        text.appendChild(descEl);
        opt.appendChild(text);
        opt.onclick = () => {
          closeModal();
          actions.insertCard?.(target, card.id);
        };
        list.appendChild(opt);
      }
    };

    const drawTiles = () => {
      grid.innerHTML = '';
      for (const category of categories) {
        const tile = document.createElement('button');
        tile.className = 'cat-tile' + (category === active ? ' active' : '');
        tile.dataset.category = category;
        tile.innerHTML = `<span class="cat-tile-icon">${ICONS[category] || ICONS.generic}</span>`;
        const lbl = document.createElement('span');
        lbl.className = 'cat-tile-label';
        lbl.textContent = labelForCategory(category);
        tile.appendChild(lbl);
        tile.onclick = () => {
          active = category;
          drawTiles();
          drawList();
        };
        grid.appendChild(tile);
      }
    };

    drawTiles();
    drawList();
    content.appendChild(grid);
    content.appendChild(list);
    sheet.appendChild(content);
  });
}

function describeCard(card) {
  if (!card.params || card.params.length === 0) return 'No parameters';
  return card.params.map((p) => p.label).join(', ');
}

function labelForCategory(category) {
  return category.charAt(0).toUpperCase() + category.slice(1);
}

// ============== PARAM EDITOR ==============
function openParamEditor(card, name, state, actions) {
  const meta = paramMeta(state, card, name);
  const current = card.params[name].value;
  const kind = meta?.kind || inferKind(current);

  openModal((sheet) => {
    sheetHeader(sheet, `Set ${name}`, card.label || card.form);
    const content = document.createElement('div');
    content.className = 'cc-sheet-content';
    sheet.appendChild(content);

    if (kind === 'enum' && meta?.values) {
      buildEnumEditor(content, meta.values, current, (value) => {
        closeModal();
        actions.setParam?.(card.id, name, value);
      });
      return;
    }

    if (kind === 'symbol' || kind === 'symbolList') {
      buildTextEditor(content, card, name, current, meta, actions);
      return;
    }

    if (isNumericEditable(current)) {
      buildNumberEditor(content, card, name, current, meta, state, actions);
      return;
    }

    // expressions, booleans, variables and operator calls → visual builder
    buildValueBuilder(content, card, name, current, state, actions);
  });
}

function buildEnumEditor(content, values, current, commit) {
  const row = document.createElement('div');
  row.className = 'enum-row';
  for (const value of values) {
    const btn = document.createElement('button');
    btn.className = 'enum-btn' + (value === current ? ' active' : '');
    btn.textContent = value;
    btn.onclick = () => commit(value);
    row.appendChild(btn);
  }
  content.appendChild(row);
}

// ============== EXPRESSION BUILDER ==============
// Recursive editor for value expressions. Every slot can become a number,
// a true/false, a variable, a sensor, or an operator call (which expands into
// editable operands). The working value is the projected value shape the
// generator already understands, so committing is a plain setParam.
function buildValueBuilder(content, card, name, current, state, actions) {
  const rootRef = { value: normalizeValue(current === undefined ? true : current) };
  const vars = collectVariableNames(state?.cards || []);

  content.appendChild(make('div', 'cc-hint', 'Tap a part to change it — combine sensors, numbers, variables and operators.'));
  const preview = make('div', 'cc-preview');
  const editor = make('div', 'val-editor');
  content.appendChild(preview);
  content.appendChild(editor);

  const updatePreview = () => { preview.textContent = formatParam(rootRef.value); };
  const redraw = () => {
    editor.innerHTML = '';
    editor.appendChild(renderValueNode(rootRef, [], state, vars, redraw, updatePreview));
    updatePreview();
  };

  const asText = make('button', 'cc-link', 'Type it as code instead');
  asText.onclick = () => {
    content.innerHTML = '';
    buildExpressionTextEditor(content, card, name, rootRef.value, actions);
  };
  content.appendChild(asText);

  const done = make('button', 'sheet-confirm', 'Done');
  done.onclick = () => {
    closeModal();
    actions.setParam?.(card.id, name, rootRef.value);
  };
  content.appendChild(done);

  redraw();
}

function renderValueNode(rootRef, path, state, vars, redraw, updatePreview) {
  const value = getAt(rootRef.value, path);
  const node = make('div', 'val-node');
  const head = make('div', 'val-head');
  head.appendChild(buildKindSelect(value, state, (encoding) => {
    setAt(rootRef, path, transformValue(value, encoding, state, vars));
    redraw();
  }));
  node.appendChild(head);

  const kind = kindOf(value);

  if (kind === 'number') {
    const input = make('input', 'cc-input val-inline');
    input.type = 'number';
    input.value = String(value);
    input.oninput = () => {
      setAt(rootRef, path, input.value === '' ? 0 : Number(input.value));
      updatePreview();
    };
    head.appendChild(input);
  } else if (kind === 'bool') {
    const row = make('div', 'enum-row val-inline');
    for (const v of [true, false]) {
      const btn = make('button', 'enum-btn' + (v === value ? ' active' : ''), v ? 'true' : 'false');
      btn.onclick = () => { setAt(rootRef, path, v); redraw(); };
      row.appendChild(btn);
    }
    head.appendChild(row);
  } else if (kind === 'var') {
    const input = make('input', 'cc-input val-inline');
    input.type = 'text';
    input.value = value.name || '';
    input.placeholder = 'variable name';
    input.oninput = () => { setAt(rootRef, path, { type: 'var', name: input.value }); updatePreview(); };
    head.appendChild(input);
    if (vars.length) {
      const chips = make('div', 'val-chips');
      for (const v of vars) {
        const chip = make('button', 'val-chip', v);
        chip.onclick = () => { setAt(rootRef, path, { type: 'var', name: v }); redraw(); };
        chips.appendChild(chip);
      }
      node.appendChild(chips);
    }
  } else if (kind === 'call') {
    const mc = expressionCardByForm(state, value.form);
    const variadic = Boolean(mc?.variadic) || !mc;
    if (value.args.length || variadic) {
      const operands = make('div', 'val-operands');
      value.args.forEach((arg, i) => {
        const row = make('div', 'val-operand');
        row.appendChild(renderValueNode(rootRef, path.concat(i), state, vars, redraw, updatePreview));
        if (variadic) {
          const rm = make('button', 'val-op-btn', '−');
          rm.title = 'Remove value';
          rm.onclick = () => { getAt(rootRef.value, path).args.splice(i, 1); redraw(); };
          row.appendChild(rm);
        }
        operands.appendChild(row);
      });
      if (variadic) {
        const add = make('button', 'val-op-btn add', '+ value');
        add.onclick = () => { getAt(rootRef.value, path).args.push(defaultOperand(value.form)); redraw(); };
        operands.appendChild(add);
      }
      node.appendChild(operands);
    }
  }

  return node;
}

function buildKindSelect(value, state, onPick) {
  const select = make('select', 'val-kind');
  const groups = [['Value', [['lit:number', '123  Number'], ['lit:bool', '✓  true / false'], ['lit:var', 'x  Variable']]]];
  const sensors = expressionCards(state, 'sensors').map((c) => ['call:' + c.form, c.label]);
  const operators = expressionCards(state, 'operators').map((c) => ['call:' + c.form, c.label]);
  if (sensors.length) groups.push(['Sensors', sensors]);
  if (operators.length) groups.push(['Operators', operators]);

  const currentEnc = encodingOf(value);
  let matched = false;
  for (const [label, opts] of groups) {
    const og = make('optgroup');
    og.label = label;
    for (const [val, text] of opts) {
      const option = make('option', null, text);
      option.value = val;
      if (val === currentEnc) { option.selected = true; matched = true; }
      og.appendChild(option);
    }
    select.appendChild(og);
  }
  if (!matched) {
    const og = make('optgroup');
    og.label = 'Current';
    const option = make('option', null, formatParam(value));
    option.value = currentEnc;
    option.selected = true;
    og.appendChild(option);
    select.appendChild(og);
  }
  select.value = currentEnc;
  select.onchange = () => onPick(select.value);
  return select;
}

function buildNumberEditor(content, card, name, current, meta, state, actions) {
  const min = Number.isFinite(meta?.min) ? meta.min : 0;
  const max = Number.isFinite(meta?.max) ? meta.max : 100;
  const unit = meta?.unit || '';
  let value = typeof current === 'number' ? current : (Number.isFinite(meta?.default) ? meta.default : min);

  const display = document.createElement('div');
  display.className = 'num-display';
  const valueEl = document.createElement('span');
  valueEl.className = 'num-value';
  valueEl.textContent = String(value);
  const unitEl = document.createElement('span');
  unitEl.className = 'num-unit';
  unitEl.textContent = unit;
  display.appendChild(valueEl);
  display.appendChild(unitEl);
  content.appendChild(display);

  const slider = document.createElement('input');
  slider.type = 'range';
  slider.className = 'num-slider';
  slider.min = String(min);
  slider.max = String(max);
  slider.step = '1';
  slider.value = String(value);
  slider.oninput = (event) => {
    value = Number(event.target.value);
    valueEl.textContent = String(value);
  };
  content.appendChild(slider);

  const presets = document.createElement('div');
  presets.className = 'num-presets';
  for (const preset of presetValues(min, max, meta?.default)) {
    const btn = document.createElement('button');
    btn.className = 'num-preset';
    btn.textContent = preset + unit;
    btn.onclick = () => {
      value = preset;
      slider.value = String(preset);
      valueEl.textContent = String(preset);
    };
    presets.appendChild(btn);
  }
  content.appendChild(presets);

  const adv = document.createElement('button');
  adv.className = 'cc-link';
  adv.textContent = 'Use sensors, variables or operators…';
  adv.onclick = () => {
    content.innerHTML = '';
    buildValueBuilder(content, card, name, current, state, actions);
  };
  content.appendChild(adv);

  const done = document.createElement('button');
  done.className = 'sheet-confirm';
  done.textContent = 'Done';
  done.onclick = () => {
    closeModal();
    actions.setParam?.(card.id, name, value);
  };
  content.appendChild(done);
}

function buildTextEditor(content, card, name, current, meta, actions) {
  const list = meta?.kind === 'symbolList';
  const input = document.createElement('input');
  input.className = 'cc-input';
  input.type = 'text';
  input.value = list ? toSymbolList(current) : formatParam(current);
  input.placeholder = list ? 'space-separated names' : 'value';
  content.appendChild(input);

  const done = document.createElement('button');
  done.className = 'sheet-confirm';
  done.textContent = 'Done';
  done.onclick = () => {
    const raw = input.value.trim();
    closeModal();
    if (list) {
      actions.setParam?.(card.id, name, raw.length ? raw.split(/\s+/) : []);
    } else if (meta?.kind === 'symbol' || meta?.kind === 'enum') {
      actions.setParam?.(card.id, name, raw);
    } else {
      actions.setParamExpression?.(card.id, name, raw);
    }
  };
  content.appendChild(done);
}

function buildExpressionTextEditor(content, card, name, current, actions) {
  const input = document.createElement('input');
  input.className = 'cc-input';
  input.type = 'text';
  input.value = formatParam(current);
  input.placeholder = 'e.g. (< (distance-cm) 20)';
  content.appendChild(input);

  const hint = document.createElement('div');
  hint.className = 'cc-hint';
  hint.textContent = 'Enter a number or a CardCode expression.';
  content.appendChild(hint);

  const done = document.createElement('button');
  done.className = 'sheet-confirm';
  done.textContent = 'Done';
  done.onclick = () => {
    const raw = input.value.trim();
    closeModal();
    actions.setParamExpression?.(card.id, name, raw);
  };
  content.appendChild(done);
}

// ============== EDITOR HELPERS ==============
function paramMeta(state, card, name) {
  const manifestCard = state.manifest?.byId.get(card.cardId);
  return manifestCard?.params.find((p) => p.name === name) || null;
}

function inferKind(value) {
  if (typeof value === 'boolean') return 'boolean';
  if (typeof value === 'number') return 'integer';
  if (Array.isArray(value)) return 'symbolList';
  return 'expression';
}

function isNumericEditable(current) {
  return typeof current === 'number';
}

function isCompound(value) {
  return value !== null && typeof value === 'object' && (value.type === 'call' || value.type === 'var');
}

function presetValues(min, max, dflt) {
  const mid = Math.round((min + max) / 2);
  const set = new Set([min, mid, max]);
  if (Number.isFinite(dflt)) set.add(dflt);
  return [...set].filter((v) => v >= min && v <= max).sort((a, b) => a - b);
}

function toSymbolList(value) {
  return Array.isArray(value) ? value.join(' ') : String(value ?? '');
}

function unitFor(card, name, state) {
  const meta = paramMeta(state, card, name);
  return meta?.unit && typeof card.params[name].value === 'number' ? meta.unit : '';
}

// ============== EXPRESSION BUILDER HELPERS ==============
function make(tag, cls, text) {
  const el = document.createElement(tag);
  if (cls) el.className = cls;
  if (text !== undefined) el.textContent = text;
  return el;
}

function kindOf(value) {
  if (typeof value === 'number') return 'number';
  if (typeof value === 'boolean') return 'bool';
  if (value && typeof value === 'object') {
    if (value.type === 'var') return 'var';
    if (value.type === 'call') return 'call';
  }
  return 'var';
}

function encodingOf(value) {
  const kind = kindOf(value);
  if (kind === 'number') return 'lit:number';
  if (kind === 'bool') return 'lit:bool';
  if (kind === 'call') return 'call:' + value.form;
  return 'lit:var';
}

// Normalize raw projected values into builder-friendly shapes (bare symbols → var refs).
function normalizeValue(value) {
  if (typeof value === 'string') return { type: 'var', name: value };
  if (value && typeof value === 'object') return JSON.parse(JSON.stringify(value));
  return value;
}

function transformValue(old, encoding, state, vars) {
  if (encoding === 'lit:number') return typeof old === 'number' ? old : 0;
  if (encoding === 'lit:bool') return typeof old === 'boolean' ? old : true;
  if (encoding === 'lit:var') {
    if (old && old.type === 'var') return old;
    return { type: 'var', name: vars[0] || '' };
  }
  if (encoding.startsWith('call:')) {
    const form = encoding.slice(5);
    const mc = expressionCardByForm(state, form);
    const arity = mc ? mc.params.length : 0;
    const variadic = Boolean(mc?.variadic);
    const args = defaultCallArgs(form, state);
    const sameForm = old && old.type === 'call' && old.form === form;
    if ((arity >= 1 || variadic) && !sameForm && usableOperand(old)) {
      args[0] = normalizeValue(old);
    }
    return { type: 'call', form, args };
  }
  return old;
}

function defaultCallArgs(form, state) {
  const mc = expressionCardByForm(state, form);
  if (!mc) return [];
  if (mc.variadic) return [defaultOperand(form), defaultOperand(form)];
  return mc.params.map((p) => literalFromDefault(p.default));
}

function literalFromDefault(d) {
  if (typeof d === 'number' || typeof d === 'boolean') return d;
  return 0;
}

function defaultOperand(form) {
  return form === 'and' || form === 'or' || form === 'not' ? true : 0;
}

function usableOperand(value) {
  if (typeof value === 'number' || typeof value === 'boolean') return true;
  return Boolean(value && typeof value === 'object' && (value.type === 'var' || value.type === 'call'));
}

function expressionCards(state, category) {
  return (state?.manifest?.cards || []).filter((c) => c.kind === 'expression' && c.category === category);
}

function expressionCardByForm(state, form) {
  const list = state?.manifest?.byForm?.get(form) || [];
  return list.find((c) => c.kind === 'expression') || null;
}

function collectVariableNames(cards) {
  const names = new Set();
  const walk = (list) => {
    for (const card of list || []) {
      if (card.cardId === 'binding.define-var' || card.cardId === 'binding.set') {
        const n = card.params?.name?.value;
        if (typeof n === 'string' && n) names.add(n);
      }
      if (card.cardId === 'binding.define-func') {
        const n = card.params?.name?.value;
        if (typeof n === 'string' && n) names.add(n);
        const ps = card.params?.params?.value;
        if (Array.isArray(ps)) ps.forEach((p) => { if (p) names.add(p); });
      }
      if (card.children) walk(card.children);
      if (card.branches) for (const branch of Object.values(card.branches)) walk(branch);
    }
  };
  walk(cards);
  return [...names];
}

function getAt(root, path) {
  let value = root;
  for (const key of path) value = value.args[key];
  return value;
}

function setAt(rootRef, path, value) {
  if (path.length === 0) {
    rootRef.value = value;
    return;
  }
  let node = rootRef.value;
  for (let i = 0; i < path.length - 1; i++) node = node.args[path[i]];
  node.args[path[path.length - 1]] = value;
}

// ============== SIDE PANELS ==============
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

// Exported for unit testing the expression-builder composition logic.
export const __builder = { transformValue, defaultCallArgs, collectVariableNames, normalizeValue, kindOf, encodingOf };
