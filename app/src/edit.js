// Structural edits over the projected card tree.
//
// Cards are a projection of the CardCode source. To edit blocks we mutate a
// detached clone of the tree, regenerate source from it, and re-apply that
// source (which re-projects fresh cards). These helpers are the mutation layer:
// they operate on a tree in place and report whether the target was found.

export function cloneCards(cards) {
  return JSON.parse(JSON.stringify(cards ?? []));
}

export function findCard(cards, id) {
  for (const card of cards) {
    if (card.id === id) return card;
    if (card.children) {
      const hit = findCard(card.children, id);
      if (hit) return hit;
    }
    if (card.branches) {
      for (const branch of Object.values(card.branches)) {
        const hit = findCard(branch, id);
        if (hit) return hit;
      }
    }
  }
  return null;
}

export function deleteCardById(cards, id) {
  const idx = cards.findIndex((card) => card.id === id);
  if (idx !== -1) {
    cards.splice(idx, 1);
    return true;
  }
  for (const card of cards) {
    if (card.children && deleteCardById(card.children, id)) return true;
    if (card.branches) {
      for (const branch of Object.values(card.branches)) {
        if (deleteCardById(branch, id)) return true;
      }
    }
  }
  return false;
}

// target: { parentId, branch } — parentId null/undefined means top level.
export function insertCard(cards, target, newCard) {
  const { parentId = null, branch = null } = target || {};
  if (parentId == null) {
    cards.push(newCard);
    return true;
  }
  const parent = findCard(cards, parentId);
  if (!parent) return false;
  if (branch) {
    if (!parent.branches) parent.branches = {};
    if (!parent.branches[branch]) parent.branches[branch] = [];
    parent.branches[branch].push(newCard);
  } else {
    if (!parent.children) parent.children = [];
    parent.children.push(newCard);
  }
  return true;
}

export function updateParamValue(cards, id, name, value) {
  const card = findCard(cards, id);
  if (!card || !card.params || !card.params[name]) return false;
  card.params[name].value = value;
  return true;
}

// Build a fresh card matching the projection shape the generator consumes,
// seeding params from the manifest defaults.
export function buildCardFromManifest(manifestCard) {
  const card = {
    id: 'new',
    cardId: manifestCard.id,
    form: manifestCard.form,
    kind: manifestCard.kind,
    category: manifestCard.category,
    label: manifestCard.label,
    params: {}
  };
  for (const param of manifestCard.params || []) {
    card.params[param.name] = { value: defaultParamValue(param) };
  }
  if (manifestCard.children) card.children = [];
  if (Array.isArray(manifestCard.branches)) {
    card.branches = {};
    for (const branch of manifestCard.branches) card.branches[branch] = [];
  }
  return card;
}

function defaultParamValue(param) {
  return Array.isArray(param.default) ? [...param.default] : param.default;
}
