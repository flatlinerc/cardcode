import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { loadManifestFromObject } from '../src/manifest.js';
import { generateProgram } from '../src/generator.js';
import { __builder } from '../src/render.js';

const { transformValue, defaultCallArgs, collectVariableNames, normalizeValue, kindOf, encodingOf } = __builder;

async function manifestState() {
  const raw = JSON.parse(await readFile(new URL('../cardcode-default.manifest.json', import.meta.url), 'utf8'));
  return { manifest: loadManifestFromObject(raw) };
}

test('kindOf and encodingOf classify projected values', () => {
  assert.equal(kindOf(20), 'number');
  assert.equal(kindOf(true), 'bool');
  assert.equal(kindOf({ type: 'var', name: 'x' }), 'var');
  assert.equal(kindOf({ type: 'call', form: '<', args: [] }), 'call');
  assert.equal(encodingOf(20), 'lit:number');
  assert.equal(encodingOf({ type: 'call', form: 'distance-cm', args: [] }), 'call:distance-cm');
});

test('normalizeValue turns bare symbols into var refs', () => {
  assert.deepEqual(normalizeValue('speed'), { type: 'var', name: 'speed' });
  assert.equal(normalizeValue(7), 7);
});

test('switching a slot to a sensor produces a zero-arg call', async () => {
  const state = await manifestState();
  const value = transformValue(0, 'call:distance-cm', state, []);
  assert.deepEqual(value, { type: 'call', form: 'distance-cm', args: [] });
});

test('switching to an operator wraps the current value as the first operand', async () => {
  const state = await manifestState();
  const sensor = { type: 'call', form: 'distance-cm', args: [] };
  const value = transformValue(sensor, 'call:<', state, []);
  assert.equal(value.form, '<');
  assert.equal(value.args.length, 2);
  assert.deepEqual(value.args[0], sensor); // wrapped
  assert.equal(value.args[1], 1); // manifest default for `<` right operand
  const source = generateProgram([
    { cardId: 'control.when', form: 'when', params: { condition: { value } }, children: [] }
  ]).source;
  assert.equal(source, '(when (< (distance-cm) 1))');
});

test('switching to a variadic operator seeds two operands', async () => {
  const state = await manifestState();
  const value = transformValue(true, 'call:and', state, []);
  assert.equal(value.form, 'and');
  assert.equal(value.args.length, 2);
  assert.deepEqual(value.args[0], true); // wrapped current
});

test('defaultCallArgs follows manifest arity and variadic shape', async () => {
  const state = await manifestState();
  assert.deepEqual(defaultCallArgs('distance-cm', state), []);
  assert.deepEqual(defaultCallArgs('<', state), [0, 1]);
  assert.deepEqual(defaultCallArgs('+', state), [0, 0]);
});

test('switching to a variable prefers a known variable name', async () => {
  const state = await manifestState();
  assert.deepEqual(transformValue(0, 'lit:var', state, ['count']), { type: 'var', name: 'count' });
  assert.deepEqual(transformValue(0, 'lit:var', state, []), { type: 'var', name: '' });
});

test('collectVariableNames gathers defines, sets and function params', () => {
  const cards = [
    { cardId: 'binding.define-var', params: { name: { value: 'speed' } } },
    {
      cardId: 'control.repeat',
      params: { count: { value: 3 } },
      children: [
        { cardId: 'binding.set', params: { name: { value: 'speed' } } },
        { cardId: 'binding.define-func', params: { name: { value: 'helper' }, params: { value: ['a', 'b'] } }, children: [] }
      ]
    }
  ];
  assert.deepEqual(collectVariableNames(cards).sort(), ['a', 'b', 'helper', 'speed']);
});