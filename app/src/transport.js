export class RuntimeClient {
  constructor({ socketFactory = (url) => new WebSocket(url), onMessage = () => {}, onStatus = () => {} } = {}) {
    this.socketFactory = socketFactory;
    this.onMessage = onMessage;
    this.onStatus = onStatus;
    this.socket = null;
  }

  connect(url) {
    this.disconnect();
    const socket = this.socketFactory(url);
    this.socket = socket;
    socket.onopen = () => {
      if (this.socket === socket) this.onStatus({ connected: true, url });
    };
    socket.onclose = () => {
      if (this.socket === socket) this.onStatus({ connected: false, url });
    };
    socket.onerror = () => {
      if (this.socket === socket) this.onStatus({ connected: false, url, error: 'websocket error' });
    };
    socket.onmessage = (event) => {
      if (this.socket !== socket) return;
      let message;
      try {
        message = JSON.parse(event.data);
      } catch (err) {
        this.onMessage({ type: 'error', message: `malformed runtime message: ${err.message}` });
        return;
      }
      this.onMessage(message);
    };
    return socket;
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
