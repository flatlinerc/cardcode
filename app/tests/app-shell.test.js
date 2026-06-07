import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

test('app shell declares required assets and element ids', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');

  assert.match(html, /<link rel="stylesheet" href="\.\/styles\.css">/);
  assert.match(html, /<script type="module" src="\.\/src\/app\.js"><\/script>/);
  assert.match(html, /value="ws:\/\/localhost:9000\/runtime"/);

  for (const id of [
    'status',
    'target',
    'url',
    'connect',
    'run',
    'stop',
    'reset',
    'blocks-tab',
    'code-tab',
    'blocks-view',
    'code-view',
    'diagnostics',
    'sensor-distance',
    'sensor-button',
    'sensor-line-left',
    'sensor-line-right',
    'telemetry'
  ]) {
    assert.match(html, new RegExp(`id="${id}"`));
  }
});

test('styles define responsive layout and app view states', async () => {
  const css = await readFile(new URL('../styles.css', import.meta.url), 'utf8');

  assert.match(css, /\.show-code\s+#blocks-view/);
  assert.match(css, /\.show-code\s+#code-view/);
  assert.match(css, /\.card\.active/);
  assert.match(css, /\.card\.error/);
  assert.match(css, /@media\s*\(max-width:/);
});
