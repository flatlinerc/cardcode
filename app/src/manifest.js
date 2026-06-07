const VALID_KINDS = new Set(['command', 'control', 'expression', 'binding', 'generic']);
const VALID_PARAM_KINDS = new Set(['integer', 'boolean', 'enum', 'symbol', 'symbolList', 'expression']);

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
  validateOptionalCardFields(card);
  if (!Array.isArray(card.params)) throw new Error(`card ${card.id}: params must be an array`);
  const params = card.params.map((param) => validateParam(param, card.id));
  const normalized = { ...card, params };
  byId.set(normalized.id, normalized);
  return normalized;
}

function validateOptionalCardFields(card) {
  if (Object.hasOwn(card, 'children') && !isNonEmptyString(card.children)) {
    throw new Error(`card ${card.id}: children must be a non-empty string`);
  }
  if (Object.hasOwn(card, 'branches') && !isNonEmptyStringArray(card.branches)) {
    throw new Error(`card ${card.id}: branches must be an array of non-empty strings`);
  }
  if (Object.hasOwn(card, 'variadic') && !isNonEmptyString(card.variadic)) {
    throw new Error(`card ${card.id}: variadic must be a non-empty string`);
  }
  if (Object.hasOwn(card, 'highlight') && typeof card.highlight !== 'boolean') {
    throw new Error(`card ${card.id}: highlight must be a boolean`);
  }
  if (Object.hasOwn(card, 'capability') && (!card.capability || typeof card.capability !== 'object' || Array.isArray(card.capability))) {
    throw new Error(`card ${card.id}: capability must be an object`);
  }
}

function validateParam(param, cardId) {
  if (!param || typeof param !== 'object') throw new Error(`card ${cardId}: param must be an object`);
  const paramName = isNonEmptyString(param.name) ? param.name : '<unknown>';
  if (!isNonEmptyString(param.name)) throw new Error(`card ${cardId} param ${paramName}: name must be a non-empty string`);
  if (!isNonEmptyString(param.kind)) throw new Error(`card ${cardId} param ${paramName}: kind must be a non-empty string`);
  if (!VALID_PARAM_KINDS.has(param.kind)) throw new Error(`card ${cardId} param ${paramName}: invalid kind '${param.kind}'`);
  if (!isNonEmptyString(param.label)) throw new Error(`card ${cardId} param ${paramName}: label must be a non-empty string`);
  if (!Object.hasOwn(param, 'default')) throw new Error(`card ${cardId} param ${paramName}: default is required`);
  return { ...param };
}

function isNonEmptyString(value) {
  return typeof value === 'string' && value.length > 0;
}

function isNonEmptyStringArray(value) {
  return Array.isArray(value) && value.every(isNonEmptyString);
}
