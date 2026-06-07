import test from 'node:test';
import assert from 'node:assert/strict';
import { createInitialState, reduceState, cardsForSpan } from '../src/state.js';

test('state tracks diagnostics and run state', () => {
  let state = createInitialState();
  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'diagnostics', ok: true, diagnostics: [] } });
  assert.deepEqual(state.diagnostics, []);
  assert.equal(state.status, 'Compiled');

  state = reduceState(state, {
    type: 'runtimeMessage',
    message: { type: 'diagnostics', ok: false, diagnostics: [{ severity: 'error', message: 'bad' }] }
  });
  assert.deepEqual(state.diagnostics, [{ severity: 'error', message: 'bad' }]);
  assert.equal(state.status, 'Compile error');

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'programStart' } });
  assert.equal(state.running, true);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'programError', message: 'execution cancelled' } });
  assert.equal(state.running, false);
  assert.equal(state.status, 'execution cancelled');
});

test('cardsForSpan maps overlapping spans', () => {
  const cards = [
    { id: 'a', span: { startOffset: 0, endOffset: 20 } },
    { id: 'b', span: { startOffset: 8, endOffset: 18 } },
    { id: 'c', span: { startOffset: 30, endOffset: 40 } }
  ];
  const hits = cardsForSpan(cards, { startOffset: 10, endOffset: 12 });

  assert.deepEqual(hits.map((card) => card.id), ['a', 'b']);
});
