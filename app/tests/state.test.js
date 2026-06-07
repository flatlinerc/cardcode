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

test('node spans map to the most specific nested card', () => {
  const parentSpan = { startOffset: 0, endOffset: 100 };
  const childSpan = { startOffset: 10, endOffset: 20 };
  const innerChildSpan = { startOffset: 12, endOffset: 18 };
  const branchChildSpan = { startOffset: 30, endOffset: 40 };
  let state = reduceState(createInitialState(), {
    type: 'edit',
    source: '',
    cards: [
      {
        id: 'parent-repeat',
        span: parentSpan,
        children: [
          { id: 'child-drive', span: childSpan }
        ],
        branches: {
          else: [
            { id: 'branch-child', span: branchChildSpan }
          ]
        }
      }
    ]
  });

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'start', span: parentSpan } });
  assert.deepEqual([...state.activeCardIds], ['parent-repeat']);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'start', span: childSpan } });
  assert.deepEqual([...state.activeCardIds].sort(), ['child-drive', 'parent-repeat']);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'done', span: childSpan } });
  assert.deepEqual([...state.activeCardIds], ['parent-repeat']);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'start', span: innerChildSpan } });
  assert.deepEqual([...state.activeCardIds].sort(), ['child-drive', 'parent-repeat']);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'done', span: innerChildSpan } });
  assert.deepEqual([...state.activeCardIds], ['parent-repeat']);

  state = reduceState(state, { type: 'runtimeMessage', message: { type: 'node', event: 'start', span: branchChildSpan } });
  assert.deepEqual([...state.activeCardIds].sort(), ['branch-child', 'parent-repeat']);
});
