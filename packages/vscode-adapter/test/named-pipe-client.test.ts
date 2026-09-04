import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createServer, Server, Socket } from 'node:net';
import test from 'node:test';
import {
  EDITOR_CONTEXT_REQUEST_TYPE,
  EDITOR_CONTEXT_RESPONSE_TYPE,
  PROTOCOL_VERSION,
  RESPONSE_ACKNOWLEDGEMENT,
  RESPONSE_FRAME_SIZE,
} from '../src/editor-context-protocol';
import { ContextServiceEditorClient } from '../src/named-pipe-client';

let endpointSequence = 0;

test('client exchanges one bounded editor request, response, and ACK', async () => {
  const endpoint = uniqueEndpoint('success');
  let requestFrame: Buffer | null = null;
  let acknowledgement = -1;
  let resolveAcknowledgement!: () => void;
  const acknowledgementReceived = new Promise<void>((resolve) => {
    resolveAcknowledgement = resolve;
  });
  const server = createServer((socket) => {
    let received = Buffer.alloc(0);
    let responseSent = false;
    socket.on('data', (chunk: Buffer) => {
      received = Buffer.concat([received, chunk]);
      if (!responseSent && received.length >= 48) {
        requestFrame = Buffer.from(received.subarray(0, 48));
        received = received.subarray(48);
        const requestId = requestFrame.readUInt32LE(12);
        socket.write(responseFrame(requestId, 0, 1));
        responseSent = true;
      }
      if (responseSent && received.length >= 1) {
        acknowledgement = received[0];
        resolveAcknowledgement();
        socket.end();
      }
    });
  });

  await listen(server, endpoint);
  const client = new ContextServiceEditorClient(endpoint);
  const result = await client.publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'markdownCode',
    languageId: 'markdown',
  }, 500);
  await acknowledgementReceived;
  await close(server);

  assert.equal(result.status, 'ok');
  assert.equal(result.responseStatus, 'ok');
  assert.ok(requestFrame);
  assert.equal(requestFrame.toString('ascii', 0, 4), 'CIME');
  assert.equal(requestFrame.readUInt16LE(6), EDITOR_CONTEXT_REQUEST_TYPE);
  assert.equal(requestFrame[17], 1);
  assert.equal(requestFrame[18], 5);
  assert.equal(requestFrame.toString('ascii', 20, 28), 'markdown');
  assert.equal(acknowledgement, RESPONSE_ACKNOWLEDGEMENT);
});

test('client returns rejected for a valid service application error', async () => {
  const endpoint = uniqueEndpoint('rejected');
  const sockets = new Set<Socket>();
  const server = createServer((socket) => {
    sockets.add(socket);
    socket.once('data', (frame: Buffer) => {
      socket.write(responseFrame(frame.readUInt32LE(12), 3, 0));
    });
    socket.on('close', () => sockets.delete(socket));
  });

  await listen(server, endpoint);
  const result = await new ContextServiceEditorClient(endpoint).publish({
    windowFocused: false,
    surface: 'unknown',
    syntax: 'unknown',
    languageId: '',
  }, 500);
  for (const socket of sockets) socket.destroy();
  await close(server);

  assert.equal(result.status, 'rejected');
  assert.equal(result.responseStatus, 'internalError');
});

test('client keeps a validated response when native disconnect surfaces as read EPIPE', async () => {
  const socket = new EventEmitter() as EventEmitter & {
    destroy(): void;
    pause(): void;
    write(data: Uint8Array, callback?: (error?: Error | null) => void): boolean;
  };
  let writes = 0;
  let acknowledgement = -1;
  socket.destroy = () => {};
  socket.pause = () => {};
  socket.write = (data, callback) => {
    writes += 1;
    if (writes === 1) {
      const request = Buffer.from(data);
      callback?.();
      setImmediate(() => {
        socket.emit('data', responseFrame(request.readUInt32LE(12), 0, 1));
      });
    } else {
      acknowledgement = data[0];
      setImmediate(() => {
        socket.emit(
          'error',
          Object.assign(new Error('read EPIPE'), { code: 'EPIPE' }),
        );
        callback?.();
        socket.emit('close', true);
      });
    }
    return true;
  };

  const client = new ContextServiceEditorClient(
    'test-native-disconnect',
    () => socket as unknown as Socket,
  );
  const resultPromise = client.publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'comment',
    languageId: 'typescript',
  }, 500);
  setImmediate(() => socket.emit('connect'));
  const result = await resultPromise;

  assert.equal(result.status, 'ok');
  assert.equal(result.responseStatus, 'ok');
  assert.equal(acknowledgement, RESPONSE_ACKNOWLEDGEMENT);
});

test('client retries a transient missing pipe instance within the total deadline', async () => {
  let attempts = 0;
  const connector = (): Socket => {
    attempts += 1;
    const attempt = attempts;
    const socket = new EventEmitter() as EventEmitter & {
      destroy(): void;
      pause(): void;
      write(data: Uint8Array, callback?: (error?: Error | null) => void): boolean;
    };
    socket.destroy = () => {};
    socket.pause = () => {};
    socket.write = (data, callback) => {
      const frame = Buffer.from(data);
      callback?.();
      if (frame.length === 48) {
        setImmediate(() => {
          socket.emit('data', responseFrame(frame.readUInt32LE(12), 0, 1));
        });
      }
      return true;
    };
    setImmediate(() => {
      if (attempt === 1) {
        socket.emit(
          'error',
          Object.assign(new Error('pipe instance gap'), { code: 'ENOENT' }),
        );
      } else {
        socket.emit('connect');
      }
    });
    return socket as unknown as Socket;
  };

  const result = await new ContextServiceEditorClient(
    'test-transient-gap',
    connector,
  ).publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'code',
    languageId: 'typescript',
  }, 100);

  assert.equal(result.status, 'ok');
  assert.equal(result.responseStatus, 'ok');
  assert.equal(attempts, 2);
});

test('client enforces one total deadline when the service stalls', async () => {
  const endpoint = uniqueEndpoint('timeout');
  const sockets = new Set<Socket>();
  const server = createServer((socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
  });

  await listen(server, endpoint);
  const startedAt = Date.now();
  const result = await new ContextServiceEditorClient(endpoint).publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'code',
    languageId: 'typescript',
  }, 30);
  const elapsedMs = Date.now() - startedAt;
  for (const socket of sockets) socket.destroy();
  await close(server);

  assert.equal(result.status, 'timeout');
  assert.ok(elapsedMs < 300, `deadline took ${elapsedMs}ms`);
});

test('client reports a disconnect when the service closes before responding', async () => {
  const endpoint = uniqueEndpoint('disconnected');
  const server = createServer((socket) => {
    socket.once('data', () => socket.end());
  });

  await listen(server, endpoint);
  const result = await new ContextServiceEditorClient(endpoint).publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'comment',
    languageId: 'typescript',
  }, 500);
  await close(server);

  assert.equal(result.status, 'disconnected');
});

test('client reports an unavailable service for a missing endpoint', async () => {
  const endpoint = uniqueEndpoint('missing');
  const result = await new ContextServiceEditorClient(endpoint).publish({
    windowFocused: false,
    surface: 'unknown',
    syntax: 'unknown',
    languageId: '',
  }, 500);

  assert.equal(result.status, 'serviceUnavailable');
  assert.ok(result.errorCode);
});

test('client rejects a response larger than the fixed native frame', async () => {
  const endpoint = uniqueEndpoint('oversized');
  const server = createServer((socket) => {
    socket.once('data', () => socket.end(Buffer.alloc(RESPONSE_FRAME_SIZE + 1)));
  });

  await listen(server, endpoint);
  const result = await new ContextServiceEditorClient(endpoint).publish({
    windowFocused: true,
    surface: 'editor',
    syntax: 'code',
    languageId: 'csharp',
  }, 500);
  await close(server);

  assert.equal(result.status, 'protocolError');
});

function uniqueEndpoint(fixture: string): string {
  endpointSequence += 1;
  return process.platform === 'win32'
    ? `\\\\.\\pipe\\ContextIME.Adapter.Test.${process.pid}.${endpointSequence}.${fixture}`
    : join(tmpdir(), `contextime-adapter-${process.pid}-${endpointSequence}-${fixture}.sock`);
}

function responseFrame(
  requestId: number,
  status: number,
  accepted: number,
): Buffer {
  const frame = Buffer.alloc(RESPONSE_FRAME_SIZE);
  frame.write('CIME', 0, 'ascii');
  frame.writeUInt16LE(PROTOCOL_VERSION, 4);
  frame.writeUInt16LE(EDITOR_CONTEXT_RESPONSE_TYPE, 6);
  frame.writeUInt32LE(16, 8);
  frame.writeUInt32LE(requestId, 12);
  frame[16] = status;
  frame[17] = accepted;
  return frame;
}

function listen(server: Server, endpoint: string): Promise<void> {
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(endpoint, resolve);
  });
}

function close(server: Server): Promise<void> {
  return new Promise((resolve, reject) => {
    server.close((error) => error ? reject(error) : resolve());
  });
}
