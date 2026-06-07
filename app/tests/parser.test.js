import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
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
  assert.throws(() => parseCardCode('(drive 40 1000'), /expected '\)'/);
});

test('parses all examples', async () => {
  for (const name of ['square', 'obstacle', 'line', 'patrol', 'approach']) {
    const source = await readFile(new URL(`../../examples/${name}.ccode`, import.meta.url), 'utf8');
    const ast = parseCardCode(source);
    assert.ok(ast.children.length > 0, `${name} should have top-level forms`);
  }
});
