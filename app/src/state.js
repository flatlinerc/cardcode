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
  const hits = message.span ? cardsForNodeSpan(state.cards, message.span) : [];
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

export function cardsForNodeSpan(cards, span) {
  const hits = cardsForSpan(flattenCards(cards), span);
  const exactHits = hits.filter((card) => sameSpan(card.span, span));
  if (exactHits.length > 0) return exactHits;

  const containingHits = hits.filter((card) => containsSpan(card.span, span));
  if (containingHits.length === 0) return [];

  const smallestSize = Math.min(...containingHits.map((card) => spanSize(card.span)));
  return containingHits.filter((card) => spanSize(card.span) === smallestSize);
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

function sameSpan(a, b) {
  return a.startOffset === b.startOffset && a.endOffset === b.endOffset;
}

function containsSpan(a, b) {
  return a.startOffset <= b.startOffset && a.endOffset >= b.endOffset;
}

function spanSize(span) {
  return span.endOffset - span.startOffset;
}
