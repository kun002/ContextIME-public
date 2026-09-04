import assert from 'node:assert/strict';
import test from 'node:test';
import {
  decodeProjectDictionaryResponse,
  encodeProjectActivationRequest,
  encodeProjectDictionaryRequest,
  MAX_PROJECT_RECORDS_PER_REQUEST,
  PROJECT_PROTOCOL_VERSION,
  PROJECT_REQUEST_FRAME_SIZE,
  PROJECT_REQUEST_TYPE,
  PROJECT_RESPONSE_FRAME_SIZE,
  PROJECT_RESPONSE_TYPE,
  ProjectDictionaryProtocolError,
  splitProjectSymbolEntries,
} from '../src/project-dictionary-protocol';
import { ProjectSymbolEntry } from '../src/project-symbols';

const projectId = '00112233445566778899aabbccddeeff';

test('project request encodes bounded symbol records without paths or source text', () => {
  const frame = encodeProjectDictionaryRequest(0x1020_3040, projectId, [
    entry('PlayerController', 'class', 2),
    entry('SpawnPlayer', 'method', 1),
  ]);

  assert.equal(frame.length, PROJECT_REQUEST_FRAME_SIZE);
  assert.equal(frame.toString('ascii', 0, 4), 'CIPD');
  assert.equal(frame.readUInt16LE(4), PROJECT_PROTOCOL_VERSION);
  assert.equal(frame.readUInt16LE(6), PROJECT_REQUEST_TYPE);
  assert.equal(frame.readUInt32LE(8), PROJECT_REQUEST_FRAME_SIZE - 16);
  assert.equal(frame.readUInt32LE(12), 0x1020_3040);
  assert.equal(frame[16], 1);
  assert.equal(frame[17], 2);
  assert.equal(frame.subarray(20, 36).toString('hex'), projectId);
  assert.equal(frame[48], 0);
  assert.equal(frame[49], 0);
  assert.equal(frame.readUInt16LE(50), 16);
  assert.equal(frame.readUInt32LE(52), 2);
  assert.equal(frame.toString('utf8', 56, 72), 'PlayerController');
  assert.ok(!frame.toString('utf8').includes('C:\\'));
});

test('symbol entries split by count and frame capacity', () => {
  const many = Array.from(
    { length: MAX_PROJECT_RECORDS_PER_REQUEST + 1 },
    (_, index) => entry(`Symbol${index}`, 'class', 1),
  );
  const countBatches = splitProjectSymbolEntries(many);
  assert.deepEqual(countBatches.map((batch) => batch.length), [64, 1]);

  const long = Array.from(
    { length: 40 },
    (_, index) => entry(`${index}`.padEnd(128, 'x'), 'method', 1),
  );
  const sizeBatches = splitProjectSymbolEntries(long);
  assert.ok(sizeBatches.length > 1);
  assert.equal(sizeBatches.flat().length, long.length);
  for (const batch of sizeBatches) {
    assert.doesNotThrow(() => encodeProjectDictionaryRequest(1, projectId, batch));
  }
});

test('active project lease operations remain fixed and carry no symbols', () => {
  const activate = encodeProjectActivationRequest(21, projectId);
  assert.equal(activate.length, PROJECT_REQUEST_FRAME_SIZE);
  assert.equal(activate[16], 2);
  assert.equal(activate[17], 0);
  assert.equal(activate.subarray(20, 36).toString('hex'), projectId);
  assert.ok(activate.subarray(48).every((value) => value === 0));

  const deactivate = encodeProjectActivationRequest(22, null);
  assert.equal(deactivate[16], 3);
  assert.equal(deactivate[17], 0);
  assert.ok(deactivate.subarray(20, 36).every((value) => value === 0));
  assert.throws(
    () => encodeProjectActivationRequest(23, 'not-an-opaque-id'),
    ProjectDictionaryProtocolError,
  );
});

test('request encoder rejects invalid ids, symbols, counts, and frequencies', () => {
  assert.throws(
    () => encodeProjectDictionaryRequest(1, '../project', [entry('Safe', 'class', 1)]),
    ProjectDictionaryProtocolError,
  );
  assert.throws(
    () => encodeProjectDictionaryRequest(1, projectId, []),
    ProjectDictionaryProtocolError,
  );
  assert.throws(
    () => encodeProjectDictionaryRequest(1, projectId, [entry('A/B', 'file', 1)]),
    ProjectDictionaryProtocolError,
  );
  assert.throws(
    () => encodeProjectDictionaryRequest(1, projectId, [entry('Safe', 'class', 0)]),
    ProjectDictionaryProtocolError,
  );
});

test('project response validates accepted count and reserved bytes', () => {
  assert.deepEqual(decodeProjectDictionaryResponse(
    responseFrame(7, 0, 1, 3), 7, 3,
  ), {
    requestId: 7,
    status: 'ok',
    accepted: true,
    acceptedCount: 3,
  });
  assert.deepEqual(decodeProjectDictionaryResponse(
    responseFrame(10, 0, 1, 0), 10, 0,
  ), {
    requestId: 10,
    status: 'ok',
    accepted: true,
    acceptedCount: 0,
  });
  assert.deepEqual(decodeProjectDictionaryResponse(
    responseFrame(8, 2, 0, 0), 8, 3,
  ), {
    requestId: 8,
    status: 'storeError',
    accepted: false,
    acceptedCount: 0,
  });

  const corrupt = responseFrame(9, 0, 1, 2);
  corrupt[20] = 1;
  assert.throws(
    () => decodeProjectDictionaryResponse(corrupt, 9, 2),
    ProjectDictionaryProtocolError,
  );
  assert.throws(
    () => decodeProjectDictionaryResponse(responseFrame(9, 0, 1, 1), 9, 2),
    ProjectDictionaryProtocolError,
  );
  assert.throws(
    () => decodeProjectDictionaryResponse(
      Buffer.alloc(PROJECT_RESPONSE_FRAME_SIZE - 1), 9, 2,
    ),
    ProjectDictionaryProtocolError,
  );
});

function entry(
  symbol: string,
  symbolType: ProjectSymbolEntry['symbolType'],
  frequency: number,
): ProjectSymbolEntry {
  return { symbol, symbolType, frequency, source: 'language_server' };
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
