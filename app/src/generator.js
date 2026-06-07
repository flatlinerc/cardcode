export function generateProgram(cards) {
  const ctx = { source: '', cardSpans: new Map() };
  cards.forEach((card, index) => {
    if (index > 0) ctx.source += '\n';
    writeCard(ctx, card, 0);
  });
  return { source: ctx.source, cardSpans: ctx.cardSpans };
}

function writeCard(ctx, card, indent) {
  const start = ctx.source.length;
  switch (card.cardId) {
    case 'control.do':
      writeSequence(ctx, card, indent, 'do');
      break;
    case 'control.repeat':
      writeSequence(ctx, card, indent, 'repeat', emitValue(card.params.count.value));
      break;
    case 'control.when':
      writeSequence(ctx, card, indent, 'when', emitValue(card.params.condition.value));
      break;
    case 'control.while':
      writeSequence(ctx, card, indent, 'while', emitValue(card.params.condition.value));
      break;
    case 'control.if':
      writeIf(ctx, card, indent);
      break;
    case 'binding.define-func':
      writeFunction(ctx, card, indent);
      break;
    case 'binding.define-var':
      ctx.source += `${pad(indent)}(define ${card.params.name.value} ${emitValue(card.params.value.value)})`;
      break;
    case 'binding.set':
      ctx.source += `${pad(indent)}(set! ${card.params.name.value} ${emitValue(card.params.value.value)})`;
      break;
    case 'generic.form':
      ctx.source += `${pad(indent)}(${card.form}${card.args.length ? ' ' + card.args.map(emitValue).join(' ') : ''})`;
      break;
    default:
      writeSimpleForm(ctx, card, indent);
      break;
  }
  ctx.cardSpans.set(card.id, spanFromSource(ctx.source, start, ctx.source.length));
}

function writeSequence(ctx, card, indent, form, firstArg) {
  ctx.source += `${pad(indent)}(${form}${firstArg === undefined ? '' : ` ${firstArg}`}`;
  for (const child of card.children || []) {
    ctx.source += '\n';
    writeCard(ctx, child, indent + 1);
  }
  ctx.source += ')';
}

function writeIf(ctx, card, indent) {
  ctx.source += `${pad(indent)}(if ${emitValue(card.params.condition.value)}`;
  const thenCard = card.branches.then[0];
  const elseCard = card.branches.else[0];
  ctx.source += '\n';
  thenCard ? writeCard(ctx, thenCard, indent + 1) : ctx.source += `${pad(indent + 1)}(do)`;
  ctx.source += '\n';
  elseCard ? writeCard(ctx, elseCard, indent + 1) : ctx.source += `${pad(indent + 1)}(do)`;
  ctx.source += ')';
}

function writeFunction(ctx, card, indent) {
  const params = card.params.params.value.join(' ');
  ctx.source += `${pad(indent)}(define (${card.params.name.value}${params ? ` ${params}` : ''})`;
  for (const child of card.children || []) {
    ctx.source += '\n';
    writeCard(ctx, child, indent + 1);
  }
  ctx.source += ')';
}

function writeSimpleForm(ctx, card, indent) {
  const args = Object.values(card.params || {}).map((param) => emitValue(param.value));
  ctx.source += `${pad(indent)}(${card.form}${args.length ? ` ${args.join(' ')}` : ''})`;
}

export function emitValue(value) {
  if (value === null || value === undefined) return '()';
  if (typeof value === 'number') return String(value);
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'string') return value;
  if (value.type === 'var') return value.name;
  if (value.type === 'call') return `(${value.form}${value.args.length ? ` ${value.args.map(emitValue).join(' ')}` : ''})`;
  throw new Error(`cannot emit value ${JSON.stringify(value)}`);
}

function pad(indent) {
  return '  '.repeat(indent);
}

function spanFromSource(source, startOffset, endOffset) {
  const start = lineColumnAt(source, startOffset);
  const end = lineColumnAt(source, endOffset);
  return {
    startOffset,
    endOffset,
    startLine: start.line,
    startColumn: start.column,
    endLine: end.line,
    endColumn: end.column
  };
}

function lineColumnAt(source, offset) {
  let line = 1;
  let column = 1;
  for (let i = 0; i < offset; i++) {
    if (source[i] === '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  return { line, column };
}
