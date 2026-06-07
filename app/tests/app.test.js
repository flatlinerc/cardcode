import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadManifestFromObject } from '../src/manifest.js';
import { createInitialState } from '../src/state.js';
import { applySourceToState } from '../src/app.js';

async function defaultManifest() {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return loadManifestFromObject(raw);
}

test('source edits preserve raw editor text while updating cards', async () => {
  const manifest = await defaultManifest();
  const source = '(drive 40 1000)\n';

  const { state } = applySourceToState(createInitialState(), manifest, source);

  assert.equal(state.source, source);
  assert.equal(state.cards.length, 1);
  assert.equal(state.cards[0].cardId, 'robot.drive');
  assert.equal(state.generatedSource, '(drive 40 1000)');
});

test('default source can be canonicalized during initialization', async () => {
  const manifest = await defaultManifest();
  const source = '(repeat 4 (drive 40 1000) (turn-right 90))';

  const { state } = applySourceToState(createInitialState(), manifest, source, { canonicalize: true });

  assert.equal(state.source, '(repeat 4\n  (drive 40 1000)\n  (turn-right 90))');
  assert.equal(state.cards[0].cardId, 'control.repeat');
});
