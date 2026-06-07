const RESERVED_LITERAL_SYMBOLS = new Set(['true', 'false', 'off', 'red', 'green', 'blue', 'yellow', 'white']);
const ZERO_PARAM_FORMS = new Set(['stop', 'beep', 'distance-cm', 'button', 'line-left', 'line-right']);

export function projectProgram(programAst, manifest) {
  return programAst.children.map((expr, index) => projectStatement(expr, manifest, `card-${index + 1}`));
}

function projectStatement(expr, manifest, id) {
  if (expr.type !== 'list' || expr.children.length === 0 || expr.children[0].type !== 'symbol') {
    return genericExpressionCard(expr, id);
  }

  const form = expr.children[0].value;
  if (form === 'define') return projectDefine(expr, manifest, id);
  if (form === 'do') return projectSequenceCard(expr, manifest, id, 'control.do', [], 1);
  if (form === 'repeat') return projectSequenceCard(expr, manifest, id, 'control.repeat', ['count'], 2);
  if (form === 'when') return projectSequenceCard(expr, manifest, id, 'control.when', ['condition'], 2);
  if (form === 'while') return projectSequenceCard(expr, manifest, id, 'control.while', ['condition'], 2);
  if (form === 'if') return projectIf(expr, manifest, id);

  const card = chooseCard(manifest, form, ['command', 'binding', 'control']);
  if (!card) return genericExpressionCard(expr, id);
  if (!hasExactArgCount(expr, card.params.length)) return genericExpressionCard(expr, id);

  const args = expr.children.slice(1);
  const params = {};
  for (let i = 0; i < card.params.length; i++) {
    const param = card.params[i];
    params[param.name] = { value: projectValue(args[i]), span: args[i]?.span || expr.span };
  }
  return baseCard(id, card, expr.span, params);
}

function projectDefine(expr, manifest, id) {
  const head = expr.children[1];
  if (head?.type === 'list') {
    if (expr.children.length < 3 || head.children[0]?.type !== 'symbol' || !head.children.slice(1).every((p) => p.type === 'symbol')) {
      return genericExpressionCard(expr, id);
    }
    const card = manifest.byId.get('binding.define-func');
    const name = head.children[0]?.value || 'function';
    const params = head.children.slice(1).map((p) => p.value);
    return {
      ...baseCard(id, card, expr.span, {
        name: { value: name, span: head.children[0]?.span || head.span },
        params: { value: params, span: head.span }
      }),
      children: expr.children.slice(2).map((child, index) => projectStatement(child, manifest, `${id}-${index + 1}`))
    };
  }

  if (expr.children.length !== 3 || head?.type !== 'symbol') return genericExpressionCard(expr, id);
  const card = manifest.byId.get('binding.define-var');
  return baseCard(id, card, expr.span, {
    name: { value: head?.value || 'x', span: head?.span || expr.span },
    value: { value: projectValue(expr.children[2]), span: expr.children[2]?.span || expr.span }
  });
}

function projectSequenceCard(expr, manifest, id, cardId, paramNames, childStart) {
  if (expr.children.length < childStart) return genericExpressionCard(expr, id);
  const card = manifest.byId.get(cardId);
  const params = {};
  for (let i = 0; i < paramNames.length; i++) {
    const valueExpr = expr.children[i + 1];
    params[paramNames[i]] = { value: projectValue(valueExpr), span: valueExpr?.span || expr.span };
  }
  return {
    ...baseCard(id, card, expr.span, params),
    children: expr.children.slice(childStart).map((child, index) => projectStatement(child, manifest, `${id}-${index + 1}`))
  };
}

function projectIf(expr, manifest, id) {
  if (expr.children.length !== 4) return genericExpressionCard(expr, id);
  const card = manifest.byId.get('control.if');
  const condition = expr.children[1];
  return {
    ...baseCard(id, card, expr.span, {
      condition: { value: projectValue(condition), span: condition?.span || expr.span }
    }),
    branches: {
      then: expr.children[2] ? [projectStatement(expr.children[2], manifest, `${id}-then-1`)] : [],
      else: expr.children[3] ? [projectStatement(expr.children[3], manifest, `${id}-else-1`)] : []
    }
  };
}

function chooseCard(manifest, form, kinds) {
  return (manifest.byForm.get(form) || []).find((card) => kinds.includes(card.kind));
}

function hasExactArgCount(expr, count) {
  return expr.children.length === count + 1;
}

function baseCard(id, manifestCard, span, params) {
  return {
    id,
    cardId: manifestCard.id,
    form: manifestCard.form,
    kind: manifestCard.kind,
    category: manifestCard.category,
    label: manifestCard.label,
    params,
    span
  };
}

export function projectValue(expr) {
  if (!expr) return null;
  if (expr.type === 'integer') return projectIntegerValue(expr.value);
  if (expr.type === 'symbol') {
    if (expr.value === 'true') return true;
    if (expr.value === 'false') return false;
    if (RESERVED_LITERAL_SYMBOLS.has(expr.value)) return expr.value;
    return { type: 'var', name: expr.value, span: expr.span };
  }
  if (expr.type === 'list') {
    const form = expr.children[0]?.value || '';
    if (ZERO_PARAM_FORMS.has(form) && expr.children.length === 1) return { type: 'call', form, args: [], span: expr.span };
    return {
      type: 'call',
      form,
      args: expr.children.slice(1).map(projectValue),
      span: expr.span
    };
  }
  return null;
}

function projectIntegerValue(rawValue) {
  const value = Number(rawValue);
  return Number.isSafeInteger(value) ? value : rawValue;
}

function genericExpressionCard(expr, id) {
  const form = expr.type === 'list' && expr.children[0]?.type === 'symbol' ? expr.children[0].value : 'expr';
  return {
    id,
    cardId: 'generic.form',
    form,
    kind: 'generic',
    category: 'generic',
    label: form,
    args: expr.type === 'list' ? expr.children.slice(1).map(projectValue) : [projectValue(expr)],
    span: expr.span
  };
}
