export class RuntimeClient {
  constructor({ socketFactory = (url) => new WebSocket(url), onMessage = () => {}, onStatus = () => {} } = {}) {
    this.socketFactory = socketFactory;
    this.onMessage = onMessage;
    this.onStatus = onStatus;
    this.socket = null;
  }

  connect(url) {
    this.disconnect();
    this.socket = this.socketFactory(url);
    this.socket.onopen = () => this.onStatus({ connected: true, url });
    this.socket.onclose = () => this.onStatus({ connected: false, url });
    this.socket.onerror = () => this.onStatus({ connected: false, url, error: 'websocket error' });
    this.socket.onmessage = (event) => {
      try {
        this.onMessage(JSON.parse(event.data));
      } catch (err) {
        this.onMessage({ type: 'error', message: `malformed runtime message: ${err.message}` });
      }
    };
    return this.socket;
  }

  disconnect() {
    if (this.socket && this.socket.readyState !== 3) this.socket.close();
    this.socket = null;
  }

  compile(source) {
    this.send({ type: 'compile', source });
  }

  run(source, limits = undefined) {
    this.send(limits ? { type: 'run', source, limits } : { type: 'run', source });
  }

  cancel() {
    this.send({ type: 'cancel' });
  }

  reset() {
    this.send({ type: 'reset' });
  }

  setSensor(sensor, value) {
    this.send({ type: 'setSensor', sensor, value });
  }

  send(message) {
    if (!this.socket || this.socket.readyState !== 1) {
      throw new Error('runtime socket is not connected');
    }
    this.socket.send(JSON.stringify(message));
  }
}
