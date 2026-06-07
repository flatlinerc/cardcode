export function parseCardCode(source) {
  const parser = new Parser(source);
  const children = [];
  while (!parser.atEnd()) {
    parser.skipIgnored();
    if (parser.atEnd()) break;
    children.push(parser.parseExpr());
  }
  return {
    type: 'program',
    children,
    span: spanFromOffsets(source, 0, source.length)
  };
}

class Parser {
  constructor(source) {
    this.source = source;
    this.offset = 0;
  }

  atEnd() {
    return this.offset >= this.source.length;
  }

  skipIgnored() {
    for (;;) {
      while (!this.atEnd() && /\s/.test(this.source[this.offset])) this.offset++;
      if (this.source[this.offset] !== ';') return;
      while (!this.atEnd() && this.source[this.offset] !== '\n') this.offset++;
    }
  }

  parseExpr() {
    this.skipIgnored();
    if (this.source[this.offset] === '(') return this.parseList();
    if (this.source[this.offset] === ')') throw this.error("unexpected ')'");
    return this.parseAtom();
  }

  parseList() {
    const start = this.offset++;
    const children = [];
    for (;;) {
      this.skipIgnored();
      if (this.atEnd()) throw this.error("expected ')'");
      if (this.source[this.offset] === ')') {
        this.offset++;
        return { type: 'list', children, span: spanFromOffsets(this.source, start, this.offset) };
      }
      children.push(this.parseExpr());
    }
  }

  parseAtom() {
    const start = this.offset;
    while (!this.atEnd() && !/\s|\(|\)|;/.test(this.source[this.offset])) this.offset++;
    const raw = this.source.slice(start, this.offset);
    const number = /^-?\d+$/.test(raw) ? Number(raw) : null;
    const value = number === null ? raw : number;
    const type = number === null ? 'symbol' : 'integer';
    return { type, value, raw, span: spanFromOffsets(this.source, start, this.offset) };
  }

  error(message) {
    const span = spanFromOffsets(this.source, this.offset, Math.min(this.source.length, this.offset + 1));
    const err = new Error(`${message} at ${span.startLine}:${span.startColumn}`);
    err.span = span;
    return err;
  }
}

export function spanFromOffsets(source, startOffset, endOffset) {
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
