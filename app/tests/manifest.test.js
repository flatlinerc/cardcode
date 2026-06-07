import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadManifestFromObject } from '../src/manifest.js';

test('default manifest validates and indexes current language cards', async () => {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  const manifest = loadManifestFromObject(raw);

  assert.equal(manifest.manifestVersion, 1);
  assert.equal(manifest.language, 'cardcode');
  assert.ok(manifest.byForm.get('drive').some((card) => card.id === 'robot.drive'));
  assert.ok(manifest.byForm.get('define').some((card) => card.kind === 'binding'));
  assert.ok(manifest.byForm.get('+').some((card) => card.kind === 'expression'));
  assert.ok(manifest.byId.has('control.if'));

  const repeatCountParam = manifest.byId.get('control.repeat').params.find((param) => param.name === 'count');
  assert.equal(repeatCountParam.min, 0);
  assert.equal(repeatCountParam.max, 100);
});

test('manifest validation rejects duplicate card ids', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      { id: 'x', form: 'drive', kind: 'command', category: 'motion', label: 'A', template: '(drive {{speed}} {{durationMs}})', params: [] },
      { id: 'x', form: 'wait', kind: 'command', category: 'time', label: 'B', template: '(wait {{durationMs}})', params: [] }
    ]
  }), /duplicate card id 'x'/);
});

test('manifest validation rejects cards without params arrays', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      { id: 'x', form: 'drive', kind: 'command', category: 'motion', label: 'A', template: '(drive)' }
    ]
  }), /card x: params must be an array/);

  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      { id: 'y', form: 'wait', kind: 'command', category: 'time', label: 'Wait', template: '(wait)', params: {} }
    ]
  }), /card y: params must be an array/);
});

test('manifest validation rejects invalid param kinds', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      {
        id: 'x',
        form: 'drive',
        kind: 'command',
        category: 'motion',
        label: 'A',
        template: '(drive {{speed}})',
        params: [{ name: 'speed', kind: 'percentage', label: 'speed', default: 40 }]
      }
    ]
  }), /card x param speed: invalid kind 'percentage'/);
});

test('manifest validation rejects invalid optional card field shapes', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      {
        id: 'x',
        form: 'if',
        kind: 'control',
        category: 'logic',
        label: 'If',
        template: '(if {{condition}} {{then}} {{else}})',
        branches: ['then', ''],
        params: []
      }
    ]
  }), /card x: branches must be an array of non-empty strings/);

  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      {
        id: 'y',
        form: 'drive',
        kind: 'command',
        category: 'motion',
        label: 'Drive',
        template: '(drive)',
        highlight: 'yes',
        params: []
      }
    ]
  }), /card y: highlight must be a boolean/);
});

test('manifest validation rejects params without labels', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      {
        id: 'x',
        form: 'drive',
        kind: 'command',
        category: 'motion',
        label: 'A',
        template: '(drive {{speed}})',
        params: [{ name: 'speed', kind: 'expression', default: 40 }]
      }
    ]
  }), /card x param speed: label must be a non-empty string/);
});

test('manifest validation rejects params without defaults', () => {
  assert.throws(() => loadManifestFromObject({
    manifestVersion: 1,
    language: 'cardcode',
    cards: [
      {
        id: 'x',
        form: 'drive',
        kind: 'command',
        category: 'motion',
        label: 'A',
        template: '(drive {{speed}})',
        params: [{ name: 'speed', kind: 'expression', label: 'speed' }]
      }
    ]
  }), /card x param speed: default is required/);
});
