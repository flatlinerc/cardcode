import test from 'node:test';
import assert from 'node:assert/strict';
import {
  contentTypeForPath,
  decodeClientFrames,
  encodeServerFrame,
  resolveStaticPath,
  webSocketAccept
} from '../../tools/dev-server.js';

test('dev server computes websocket accept header', () => {
  assert.equal(
    webSocketAccept('dGhlIHNhbXBsZSBub25jZQ=='),
    's3pPLMBiTxaQ9kYGzzhZRbK+xOo='
  );
});

test('dev server decodes masked browser text frames', () => {
  const payload = Buffer.from('{"type":"compile","source":"(drive 40 1000)"}');
  const mask = Buffer.from([1, 2, 3, 4]);
  const frame = Buffer.alloc(2 + mask.length + payload.length);
  frame[0] = 0x81;
  frame[1] = 0x80 | payload.length;
  mask.copy(frame, 2);
  for (let i = 0; i < payload.length; i++) frame[6 + i] = payload[i] ^ mask[i % 4];

  const { frames, rest } = decodeClientFrames(frame);

  assert.equal(rest.length, 0);
  assert.equal(frames.length, 1);
  assert.equal(frames[0].opcode, 1);
  assert.equal(frames[0].payload.toString('utf8'), payload.toString('utf8'));
});

test('dev server preserves partial websocket frames', () => {
  const partial = Buffer.from([0x81, 0x85, 1, 2]);

  const { frames, rest } = decodeClientFrames(partial);

  assert.deepEqual(frames, []);
  assert.deepEqual(rest, partial);
});

test('dev server encodes unmasked server text frames', () => {
  const frame = encodeServerFrame('ok');

  assert.equal(frame[0], 0x81);
  assert.equal(frame[1], 2);
  assert.equal(frame.subarray(2).toString('utf8'), 'ok');
});

test('dev server resolves only files inside app root', () => {
  const root = new URL('../', import.meta.url);

  assert.match(resolveStaticPath(root, '/index.html'), /app\/index\.html$/);
  assert.equal(resolveStaticPath(root, '/..%2FREADME.md'), null);
  assert.equal(contentTypeForPath('/app/src/app.js'), 'text/javascript');
});
