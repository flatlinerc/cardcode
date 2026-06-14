import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadManifestFromObject } from '../src/manifest.js';
import { parseCardCode } from '../src/parser.js';
import { projectProgram } from '../src/projection.js';
import { generateProgram } from '../src/generator.js';
import { cloneCards, findCard, deleteCardById, insertCard, updateParamValue, buildCardFromManifest } from '../src/edit.js';

async function defaultManifest() {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return loadManifestFromObject(raw);
}

function project(source, manifest) {
  return projectProgram(parseCardCode(source), manifest);
}

// Mutate a clone of the projected tree, regenerate, and re-project — mirrors app.mutateTree.
function applyEdit(source, manifest, mutate) {
  const tree = cloneCards(project(source, manifest));
  mutate(tree);
  return generateProgram(tree).source;
}

test('buildCardFromManifest seeds defaults and container shape', async () => {
  const manifest = await defaultManifest();

  const drive = buildCardFromManifest(manifest.byId.get('robot.drive'));
  assert.equal(drive.cardId, 'robot.drive');
  assert.equal(drive.params.speed.value, 40);
  assert.equal(drive.params.durationMs.value, 1000);
  assert.equal(drive.children, undefined);

  const repeat = buildCardFromManifest(manifest.byId.get('control.repeat'));
  assert.deepEqual(repeat.children, []);
  assert.equal(repeat.params.count.value, 4);

  const branch = buildCardFromManifest(manifest.byId.get('control.if'));
  assert.deepEqual(branch.branches, { then: [], else: [] });
});

test('insertCard appends a generated block at top level', async () => {
  const manifest = await defaultManifest();
  const out = applyEdit('(drive 40 1000)', manifest, (tree) => {
    insertCard(tree, { parentId: null }, buildCardFromManifest(manifest.byId.get('robot.beep')));
  });
  assert.equal(out, '(drive 40 1000)\n(beep)');
});

test('insertCard appends into a container by id', async () => {
  const manifest = await defaultManifest();
  const tree = cloneCards(project('(repeat 4 (drive 40 1000))', manifest));
  const repeatId = tree[0].id;
  insertCard(tree, { parentId: repeatId }, buildCardFromManifest(manifest.byId.get('robot.stop')));
  const out = generateProgram(tree).source;
  assert.equal(out, '(repeat 4\n  (drive 40 1000)\n  (stop))');
});

test('insertCard appends into an if branch', async () => {
  const manifest = await defaultManifest();
  const tree = cloneCards(project('(if true (beep) (stop))', manifest));
  const ifId = tree[0].id;
  insertCard(tree, { parentId: ifId, branch: 'then' }, buildCardFromManifest(manifest.byId.get('robot.stop')));
  const out = generateProgram(tree).source;
  // then-branch now has two statements and folds into a (do ...)
  assert.match(out, /\(if true\n {2}\(do\n {4}\(beep\)\n {4}\(stop\)\)/);
});

test('deleteCardById removes a nested block', async () => {
  const manifest = await defaultManifest();
  const out = applyEdit('(repeat 4 (drive 40 1000) (stop))', manifest, (tree) => {
    const stop = tree[0].children[1];
    assert.equal(deleteCardById(tree, stop.id), true);
  });
  assert.equal(out, '(repeat 4\n  (drive 40 1000))');
});

test('updateParamValue changes a literal and round-trips', async () => {
  const manifest = await defaultManifest();
  const out = applyEdit('(drive 40 1000)', manifest, (tree) => {
    assert.equal(updateParamValue(tree, tree[0].id, 'speed', 75), true);
  });
  assert.equal(out, '(drive 75 1000)');
});

test('updateParamValue can store a compound expression', async () => {
  const manifest = await defaultManifest();
  const tree = cloneCards(project('(when true (beep))', manifest));
  const condition = { type: 'call', form: '<', args: [{ type: 'call', form: 'distance-cm', args: [] }, 20] };
  assert.equal(updateParamValue(tree, tree[0].id, 'condition', condition), true);
  const out = generateProgram(tree).source;
  assert.equal(out, '(when (< (distance-cm) 20)\n  (beep))');
});

test('findCard locates cards inside branches', async () => {
  const manifest = await defaultManifest();
  const tree = project('(if true (beep) (stop))', manifest);
  const stop = tree[0].branches.else[0];
  assert.equal(findCard(tree, stop.id), stop);
  assert.equal(findCard(tree, 'missing'), null);
});
