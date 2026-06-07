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
  receiveRaw(data) {
    this.onmessage?.({ data });
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

test('runtime client throws when sending before connect', () => {
  const client = new RuntimeClient();

  assert.throws(
    () => client.compile('(drive 40 1000)'),
    /runtime socket is not connected/
  );
});

test('runtime client dispatches one error for malformed JSON', () => {
  const socket = new FakeSocket();
  const received = [];
  const client = new RuntimeClient({ socketFactory: () => socket, onMessage: (msg) => received.push(msg) });
  client.connect('ws://robot');

  socket.receiveRaw('{');

  assert.equal(received.length, 1);
  assert.equal(received[0].type, 'error');
  assert.match(received[0].message, /^malformed runtime message: /);
});

test('runtime client ignores stale events after reconnect', () => {
  const sockets = [new FakeSocket(), new FakeSocket()];
  const received = [];
  const statuses = [];
  const client = new RuntimeClient({
    socketFactory: () => sockets.shift(),
    onMessage: (msg) => received.push(msg),
    onStatus: (status) => statuses.push(status)
  });

  const oldSocket = client.connect('ws://robot');
  client.connect('ws://robot');

  oldSocket.onclose?.();
  oldSocket.receive({ type: 'diagnostics', stale: true });
  oldSocket.onerror?.();

  assert.deepEqual(received, []);
  assert.deepEqual(statuses, [
    { connected: false, url: 'ws://robot' }
  ]);
});

test('runtime client does not relabel onMessage exceptions as malformed JSON', () => {
  const socket = new FakeSocket();
  const received = [];
  const client = new RuntimeClient({
    socketFactory: () => socket,
    onMessage: (msg) => {
      received.push(msg);
      throw new Error('consumer failed');
    }
  });
  client.connect('ws://robot');

  assert.throws(
    () => socket.receive({ type: 'diagnostics', ok: true, diagnostics: [] }),
    /consumer failed/
  );
  assert.deepEqual(received, [
    { type: 'diagnostics', ok: true, diagnostics: [] }
  ]);
});
