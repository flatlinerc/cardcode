import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';

test('parses nested forms with spans', () => {
  const source = '(repeat 2 (drive 40 1000) (turn-right 90))';
  const ast = parseCardCode(source);

  assert.equal(ast.type, 'program');
  assert.equal(ast.children.length, 1);
  assert.equal(ast.children[0].type, 'list');
  assert.equal(ast.children[0].children[0].value, 'repeat');
  assert.deepEqual(ast.children[0].span, {
    startOffset: 0,
    endOffset: source.length,
    startLine: 1,
    startColumn: 1,
    endLine: 1,
    endColumn: source.length + 1
  });
});

test('skips semicolon comments and tracks following lines', () => {
  const source = '; lead comment\n(drive 40 1000)';
  const ast = parseCardCode(source);
  const drive = ast.children[0];

  assert.equal(drive.children[0].value, 'drive');
  assert.equal(drive.span.startLine, 2);
  assert.equal(drive.span.startColumn, 1);
});

test('reports unmatched paren with source span', () => {
  const source = '(drive 40 1000';
  let err;
  try {
    parseCardCode(source);
  } catch (error) {
    err = error;
  }

  assert.match(err.message, /expected '\)'/);
  assert.deepEqual(err.span, {
    startOffset: source.length,
    endOffset: source.length,
    startLine: 1,
    startColumn: source.length + 1,
    endLine: 1,
    endColumn: source.length + 1
  });
});

test('preserves integer literal values as source text', () => {
  const ast = parseCardCode('(drive 9007199254740993 1000)');
  const speed = ast.children[0].children[1];
  const duration = ast.children[0].children[2];

  assert.equal(speed.type, 'integer');
  assert.equal(speed.raw, '9007199254740993');
  assert.equal(speed.value, '9007199254740993');
  assert.equal(duration.type, 'integer');
  assert.equal(duration.raw, '1000');
  assert.equal(duration.value, '1000');
});

test('parses all examples', async () => {
  const examplesUrl = new URL('../../examples/', import.meta.url);
  const exampleNames = (await readdir(examplesUrl)).filter((name) => name.endsWith('.ccode'));

  for (const name of exampleNames) {
    const source = await readFile(new URL(name, examplesUrl), 'utf8');
    const ast = parseCardCode(source);
    assert.ok(ast.children.length > 0, `${name} should have top-level forms`);
  }
});
