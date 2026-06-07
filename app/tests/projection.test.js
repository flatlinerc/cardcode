import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';
import { loadManifestFromObject } from '../src/manifest.js';
import { projectProgram, projectValue } from '../src/projection.js';

async function defaultManifest() {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return loadManifestFromObject(raw);
}

test('projects commands and controls into specialized cards', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(repeat 2 (drive 40 1000) (turn-right 90))');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards.length, 1);
  assert.equal(cards[0].cardId, 'control.repeat');
  assert.equal(cards[0].params.count.value, 2);
  assert.equal(cards[0].children.length, 2);
  assert.equal(cards[0].children[0].cardId, 'robot.drive');
  assert.equal(cards[0].children[0].params.speed.value, 40);
});

test('projects function definitions and calls', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(define (square n) (* n n)) (drive (square 6) 1000)');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards[0].cardId, 'binding.define-func');
  assert.equal(cards[0].params.name.value, 'square');
  assert.deepEqual(cards[0].params.params.value, ['n']);
  assert.equal(cards[1].cardId, 'robot.drive');
  assert.equal(cards[1].params.speed.value.type, 'call');
  assert.equal(cards[1].params.speed.value.form, 'square');
});

test('falls back to generic cards for unknown forms', async () => {
  const manifest = await defaultManifest();
  const ast = parseCardCode('(custom-thing 1 true)');
  const cards = projectProgram(ast, manifest);

  assert.equal(cards[0].cardId, 'generic.form');
  assert.equal(cards[0].form, 'custom-thing');
  assert.equal(cards[0].args.length, 2);
});

test('preserves unsafe integer literals without precision loss', () => {
  const ast = parseCardCode('9007199254740993');

  assert.equal(projectValue(ast.children[0]), '9007199254740993');
});
