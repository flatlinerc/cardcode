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
