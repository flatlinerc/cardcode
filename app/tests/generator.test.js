import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { parseCardCode } from '../src/parser.js';
import { loadManifestFromObject } from '../src/manifest.js';
import { projectProgram } from '../src/projection.js';
import { generateProgram } from '../src/generator.js';

async function cardsFromSource(source) {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return projectProgram(parseCardCode(source), loadManifestFromObject(raw));
}

test('generates canonical source from projected cards', async () => {
  const cards = await cardsFromSource('(repeat 2 (drive 40 1000) (turn-right 90))');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(repeat 2\n  (drive 40 1000)\n  (turn-right 90))');
  assert.equal(generated.cardSpans.get(cards[0].id).startOffset, 0);
  assert.ok(generated.cardSpans.get(cards[0].children[0].id).startOffset > 0);
  assert.doesNotThrow(() => parseCardCode(generated.source));
});

test('generates functions and expression arguments', async () => {
  const cards = await cardsFromSource('(define (square n) (* n n)) (drive (square 6) 1000)');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(define (square n)\n  (* n n))\n(drive (square 6) 1000)');
});

test('generates generic forms without losing arguments', async () => {
  const cards = await cardsFromSource('(custom-thing 1 true)');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(custom-thing 1 true)');
});

test('generates do forms without losing child cards', async () => {
  const cards = await cardsFromSource('(do (drive 40 1000) (stop))');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(do\n  (drive 40 1000)\n  (stop))');
  assert.ok(generated.cardSpans.has(cards[0].children[1].id));
  assert.doesNotThrow(() => parseCardCode(generated.source));
});

test('preserves unsafe integer text when generating values', async () => {
  const cards = await cardsFromSource('(drive 9007199254740993 1000)');
  const generated = generateProgram(cards);

  assert.equal(generated.source, '(drive 9007199254740993 1000)');
});

test('generates all cards in multi-card if branches', async () => {
  const [ifCard] = await cardsFromSource('(if true (stop) (beep))');
  const [stopCard, beepCard, driveCard, waitCard] = await cardsFromSource('(stop) (beep) (drive 10 20) (wait 30)');
  stopCard.id = 'then-stop';
  beepCard.id = 'then-beep';
  driveCard.id = 'else-drive';
  waitCard.id = 'else-wait';
  ifCard.branches = {
    then: [stopCard, beepCard],
    else: [driveCard, waitCard]
  };

  const generated = generateProgram([ifCard]);

  assert.equal(generated.source, '(if true\n  (do\n    (stop)\n    (beep))\n  (do\n    (drive 10 20)\n    (wait 30)))');
  assert.ok(generated.cardSpans.has(stopCard.id));
  assert.ok(generated.cardSpans.has(beepCard.id));
  assert.ok(generated.cardSpans.has(driveCard.id));
  assert.ok(generated.cardSpans.has(waitCard.id));
});
