import assert from 'node:assert/strict';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createServer, Server, Socket } from 'node:net';
import test from 'node:test';
import {
  PROJECT_PROTOCOL_VERSION,
  PROJECT_REQUEST_FRAME_SIZE,
  PROJECT_RESPONSE_ACKNOWLEDGEMENT,
  PROJECT_RESPONSE_FRAME_SIZE,
  PROJECT_RESPONSE_TYPE,
} from '../src/project-dictionary-protocol';
import { ProjectIndexerClient } from '../src/project-indexer-client';
import { ProjectSymbolEntry } from '../src/project-symbols';

const projectId = '00112233445566778899aabbccddeeff';
let endpointSequence = 0;

test('project client sends bounded batches and acknowledgements', async () => {
  const endpoint = uniqueEndpoint('success');
  let frames = 0;
  let accepted = 0;
  let acknowledgements = 0;
  const sockets = new Set<Socket>();
  const server = createServer((socket) => {
    sockets.add(socket);
    let received = Buffer.alloc(0);
    let responseSent = false;
    socket.on('data', (chunk: Buffer) => {
      received = Buffer.concat([received, chunk]);
      if (!responseSent && received.length >= PROJECT_REQUEST_FRAME_SIZE) {
        const frame = received.subarray(0, PROJECT_REQUEST_FRAME_SIZE);
        received = received.subarray(PROJECT_REQUEST_FRAME_SIZE);
        frames += 1;
        const count = frame[17];
        accepted += count;
        socket.write(responseFrame(frame.readUInt32LE(12), 0, 1, count));
        responseSent = true;
      }
      if (responseSent && received.length >= 1) {
        if (received[0] === PROJECT_RESPONSE_ACKNOWLEDGEMENT) {
          acknowledgements += 1;
        }
        socket.end();
      }
    });
    socket.on('close', () => sockets.delete(socket));
  });

  await listen(server, endpoint);
  const entries = Array.from(
    { length: 70 },
    (_, index) => entry(`ProjectSymbol${index}`),
  );
  const result = await new ProjectIndexerClient(endpoint).publish(
    projectId, entries, 1000,
  );
  await waitFor(() => acknowledgements === 2);
  for (const socket of sockets) socket.destroy();
  await close(server);

  assert.equal(result.status, 'ok');
  assert.equal(result.attemptedSymbols, 70);
  assert.equal(result.acceptedSymbols, 70);
  assert.equal(result.batches, 2);
  assert.equal(frames, 2);
  assert.equal(accepted, 70);
  assert.equal(acknowledgements, 2);
});

test('project client refreshes and clears the active-project lease', async () => {
  const endpoint = uniqueEndpoint('activation');
  const operations: number[] = [];
  let acknowledgements = 0;
  const server = createServer((socket) => {
    let received = Buffer.alloc(0);
    socket.on('data', (chunk: Buffer) => {
      received = Buffer.concat([received, chunk]);
      if (received.length >= PROJECT_REQUEST_FRAME_SIZE && operations.length < 2) {
        const frame = received.subarray(0, PROJECT_REQUEST_FRAME_SIZE);
        received = received.subarray(PROJECT_REQUEST_FRAME_SIZE);
        operations.push(frame[16]);
        socket.write(responseFrame(frame.readUInt32LE(12), 0, 1, 0));
      }
      if (received.length >= 1) {
        if (received[0] === PROJECT_RESPONSE_ACKNOWLEDGEMENT) {
          acknowledgements += 1;
        }
        socket.end();
      }
    });
  });

  await listen(server, endpoint);
  const client = new ProjectIndexerClient(endpoint);
  const activated = await client.activate(projectId, 1000);
  const deactivated = await client.deactivate(1000);
  await waitFor(() => acknowledgements === 2);
  await close(server);

  assert.equal(activated.status, 'ok');
  assert.equal(deactivated.status, 'ok');
  assert.deepEqual(operations, [2, 3]);
});

test('project client reports native store rejection without retrying', async () => {
  const endpoint = uniqueEndpoint('rejected');
  let connections = 0;
  const server = createServer((socket) => {
    connections += 1;
    socket.once('data', (frame: Buffer) => {
      socket.write(responseFrame(frame.readUInt32LE(12), 2, 0, 0));
    });
  });

  await listen(server, endpoint);
  const result = await new ProjectIndexerClient(endpoint).publish(
    projectId, [entry('PlayerController')], 500,
  );
  await close(server);

  assert.equal(result.status, 'rejected');
  assert.equal(result.responseStatus, 'storeError');
  assert.equal(result.acceptedSymbols, 0);
  assert.equal(connections, 1);
});

test('project client enforces one deadline for an absent endpoint', async () => {
  const endpoint = uniqueEndpoint('missing');
  const started = Date.now();
  const result = await new ProjectIndexerClient(endpoint).publish(
    projectId, [entry('PlayerController')], 40,
  );
  const elapsed = Date.now() - started;

  assert.ok(
    result.status === 'serviceUnavailable' || result.status === 'timeout',
    result.status,
  );
  assert.equal(result.acceptedSymbols, 0);
  assert.ok(elapsed < 300, `deadline took ${elapsed}ms`);
});

test('project client rejects invalid local records before connecting', async () => {
  const result = await new ProjectIndexerClient('unused').publish(
    projectId,
    [{ ...entry('PlayerController'), frequency: 0 }],
    100,
  );
  assert.equal(result.status, 'protocolError');
  assert.equal(result.requestId, 0);
  assert.equal(result.batches, 0);
});

function entry(symbol: string): ProjectSymbolEntry {
  return {
    symbol,
    symbolType: 'class',
    frequency: 1,
    source: 'language_server',
  };
}

function responseFrame(
  requestId: number,
  status: number,
  accepted: number,
  acceptedCount: number,
): Buffer {
  const frame = Buffer.alloc(PROJECT_RESPONSE_FRAME_SIZE);
  frame.write('CIPD', 0, 'ascii');
  frame.writeUInt16LE(PROJECT_PROTOCOL_VERSION, 4);
  frame.writeUInt16LE(PROJECT_RESPONSE_TYPE, 6);
  frame.writeUInt32LE(PROJECT_RESPONSE_FRAME_SIZE - 16, 8);
  frame.writeUInt32LE(requestId, 12);
  frame[16] = status;
  frame[17] = accepted;
  frame.writeUInt16LE(acceptedCount, 18);
  return frame;
}

function uniqueEndpoint(fixture: string): string {
  endpointSequence += 1;
  return process.platform === 'win32'
    ? `\\\\.\\pipe\\ContextIME.Project.Test.${process.pid}.${endpointSequence}.${fixture}`
    : join(tmpdir(), `contextime-project-${process.pid}-${endpointSequence}-${fixture}.sock`);
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

async function waitFor(predicate: () => boolean): Promise<void> {
  const deadline = Date.now() + 1000;
  while (!predicate()) {
    if (Date.now() >= deadline) throw new Error('condition timed out');
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}
