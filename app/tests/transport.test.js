import test from 'node:test';
import assert from 'node:assert/strict';
import { RuntimeClient } from '../src/transport.js';

class FakeSocket {
  constructor() {
    this.sent = [];
    this.readyState = FakeSocket.OPEN;
  }
  send(data) {
    this.sent.push(JSON.parse(data));
  }
  close() {
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.();
  }
  receive(obj) {
    this.onmessage?.({ data: JSON.stringify(obj) });
  }
}
FakeSocket.OPEN = 1;
FakeSocket.CLOSED = 3;

test('runtime client sends protocol messages', () => {
  const socket = new FakeSocket();
  const client = new RuntimeClient({ socketFactory: () => socket });
  client.connect('ws://robot');
  client.compile('(drive 40 1000)');
  client.run('(drive 40 1000)', { maxTotalSteps: 20 });
  client.cancel();
  client.reset();
  client.setSensor('distance-cm', 12);

  assert.deepEqual(socket.sent, [
    { type: 'compile', source: '(drive 40 1000)' },
    { type: 'run', source: '(drive 40 1000)', limits: { maxTotalSteps: 20 } },
    { type: 'cancel' },
    { type: 'reset' },
    { type: 'setSensor', sensor: 'distance-cm', value: 12 }
  ]);
});

test('runtime client dispatches parsed messages', () => {
  const socket = new FakeSocket();
  const received = [];
  const client = new RuntimeClient({ socketFactory: () => socket, onMessage: (msg) => received.push(msg) });
  client.connect('ws://robot');

  socket.receive({ type: 'diagnostics', ok: true, diagnostics: [] });
  socket.receive({ type: 'programStart' });

  assert.deepEqual(received, [
    { type: 'diagnostics', ok: true, diagnostics: [] },
    { type: 'programStart' }
  ]);
});
