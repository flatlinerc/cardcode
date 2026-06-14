#!/usr/bin/env node
import { createHash } from 'node:crypto';
import { createReadStream, existsSync } from 'node:fs';
import { stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { extname, resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { spawn } from 'node:child_process';

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

export function webSocketAccept(key) {
  return createHash('sha1').update(`${key}${WS_GUID}`).digest('base64');
}

export function encodeServerFrame(payload, opcode = 1) {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload));
  let header;
  if (body.length < 126) {
    header = Buffer.from([0x80 | opcode, body.length]);
  } else if (body.length <= 0xffff) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | opcode;
    header[1] = 126;
    header.writeUInt16BE(body.length, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | opcode;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(body.length), 2);
  }
  return Buffer.concat([header, body]);
}

export function decodeClientFrames(buffer) {
  let offset = 0;
  const frames = [];

  while (offset + 2 <= buffer.length) {
    const start = offset;
    const first = buffer[offset++];
    const second = buffer[offset++];
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    let length = second & 0x7f;

    if (length === 126) {
      if (offset + 2 > buffer.length) return { frames, rest: buffer.subarray(start) };
      length = buffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (offset + 8 > buffer.length) return { frames, rest: buffer.subarray(start) };
      const bigLength = buffer.readBigUInt64BE(offset);
      if (bigLength > BigInt(Number.MAX_SAFE_INTEGER)) {
        throw new Error('websocket frame too large');
      }
      length = Number(bigLength);
      offset += 8;
    }

    const maskOffset = offset;
    if (masked) offset += 4;
    if (offset + length > buffer.length) return { frames, rest: buffer.subarray(start) };

    const payload = Buffer.from(buffer.subarray(offset, offset + length));
    offset += length;

    if (masked) {
      const mask = buffer.subarray(maskOffset, maskOffset + 4);
      for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
    }

    frames.push({ opcode, payload });
  }

  return { frames, rest: buffer.subarray(offset) };
}

export function resolveStaticPath(rootUrl, requestPath) {
  const root = resolve(fileURLToPath(rootUrl));
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(requestPath, 'http://localhost').pathname);
  } catch {
    return null;
  }
  const relativePath = pathname === '/' ? 'index.html' : pathname.slice(1);
  const resolved = resolve(root, relativePath);
  if (resolved !== root && !resolved.startsWith(`${root}${sep}`)) return null;
  return resolved;
}

export function contentTypeForPath(pathname) {
  switch (extname(pathname)) {
    case '.html': return 'text/html';
    case '.css': return 'text/css';
    case '.js': return 'text/javascript';
    case '.json': return 'application/json';
    case '.svg': return 'image/svg+xml';
    default: return 'application/octet-stream';
  }
}

export function createDevServer({
  appRoot = new URL('../app/', import.meta.url),
  harness = './build/cardcode-harness',
  harnessArgs = [],
  runtimePath = '/runtime'
} = {}) {
  const server = createServer(async (req, res) => {
    // A client that navigates away mid-response resets the socket; swallow it
    // so it doesn't surface as an unhandled 'error' event and crash the server.
    req.on('error', () => {});
    res.on('error', () => {});

    const filePath = resolveStaticPath(appRoot, req.url || '/');
    if (!filePath) {
      res.writeHead(403);
      res.end('Forbidden\n');
      return;
    }

    try {
      const info = await stat(filePath);
      if (!info.isFile()) throw new Error('not a file');
      res.writeHead(200, { 'Content-Type': contentTypeForPath(filePath) });
      const stream = createReadStream(filePath);
      stream.on('error', () => res.destroy());
      stream.pipe(res);
    } catch {
      res.writeHead(404);
      res.end('Not found\n');
    }
  });

  server.on('upgrade', (req, socket) => {
    // Without an error listener, a client reset (ECONNRESET) on the upgraded
    // socket throws and takes the whole process down.
    socket.on('error', () => socket.destroy());
    const pathname = new URL(req.url || '/', 'http://localhost').pathname;
    if (pathname !== runtimePath) {
      socket.write('HTTP/1.1 404 Not Found\r\n\r\n');
      socket.destroy();
      return;
    }
    attachRuntimeSocket(req, socket, harness, harnessArgs);
  });

  return server;
}

function attachRuntimeSocket(req, socket, harness, harnessArgs) {
  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
    socket.destroy();
    return;
  }

  const child = spawn(harness, harnessArgs, { stdio: ['pipe', 'pipe', 'pipe'] });
  // Writing to the harness after it exits emits EPIPE on stdin; ignore it.
  child.stdin.on('error', () => {});
  let socketBuffer = Buffer.alloc(0);
  let lineBuffer = '';

  socket.write([
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${webSocketAccept(key)}`,
    '',
    ''
  ].join('\r\n'));

  socket.on('data', (chunk) => {
    socketBuffer = Buffer.concat([socketBuffer, chunk]);
    let decoded;
    try {
      decoded = decodeClientFrames(socketBuffer);
    } catch (err) {
      sendText(socket, JSON.stringify({ type: 'error', message: err.message }));
      socket.destroy();
      return;
    }
    socketBuffer = decoded.rest;
    for (const frame of decoded.frames) {
      if (frame.opcode === 1) child.stdin.write(`${frame.payload.toString('utf8')}\n`);
      if (frame.opcode === 8) socket.end(encodeServerFrame(Buffer.alloc(0), 8));
      if (frame.opcode === 9) socket.write(encodeServerFrame(frame.payload, 10));
    }
  });

  child.stdout.on('data', (chunk) => {
    lineBuffer += chunk.toString('utf8');
    const lines = lineBuffer.split(/\r?\n/);
    lineBuffer = lines.pop() || '';
    for (const line of lines) {
      if (line.length > 0) sendText(socket, line);
    }
  });

  child.stderr.on('data', (chunk) => {
    process.stderr.write(chunk);
  });

  child.on('error', (err) => {
    sendText(socket, JSON.stringify({ type: 'error', message: `mock harness failed: ${err.message}` }));
    socket.end(encodeServerFrame(Buffer.alloc(0), 8));
  });

  child.on('exit', () => {
    socket.end(encodeServerFrame(Buffer.alloc(0), 8));
  });

  socket.on('close', () => {
    child.kill();
  });
}

function sendText(socket, text) {
  if (!socket.destroyed) socket.write(encodeServerFrame(text));
}

function parseArgs(argv) {
  const options = {
    host: '0.0.0.0',
    port: 19000,
    harness: './build/cardcode-harness',
    harnessArgs: []
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--host') options.host = argv[++i];
    else if (arg === '--port') options.port = Number(argv[++i]);
    else if (arg === '--harness') options.harness = argv[++i];
    else if (arg === '--realtime') options.harnessArgs.push('--realtime');
    else if (arg === '--help') options.help = true;
    else throw new Error(`unknown argument: ${arg}`);
  }

  return options;
}

function printHelp() {
  console.log(`Usage: npm run dev -- [--port 19000] [--host 0.0.0.0] [--harness ./build/cardcode-harness] [--realtime]

Serves app/ over HTTP and proxies ws://HOST:PORT/runtime to cardcode-harness.
Build the harness first with:

  cmake -S . -B build
  cmake --build build
`);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    printHelp();
    return;
  }
  if (!existsSync(options.harness)) {
    throw new Error(`missing harness at ${options.harness}; run cmake --build build first`);
  }

  const server = createDevServer(options);
  server.on('error', (err) => {
    console.error(`dev server failed: ${err.message}`);
    process.exit(1);
  });
  server.listen(options.port, options.host, () => {
    console.log(`CardCode UI: http://${options.host}:${options.port}/`);
    console.log(`Mock runtime: ws://${options.host}:${options.port}/runtime`);
  });
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((err) => {
    console.error(err.message);
    process.exit(1);
  });
}
