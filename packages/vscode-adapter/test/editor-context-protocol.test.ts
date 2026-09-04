import assert from 'node:assert/strict';
import test from 'node:test';
import {
  decodeEditorContextResponse,
  EDITOR_CONTEXT_REQUEST_TYPE,
  EDITOR_CONTEXT_RESPONSE_TYPE,
  EditorContextProtocolError,
  encodeEditorContextRequest,
  LANGUAGE_ID_CAPACITY,
  normalizeLanguageId,
  PROTOCOL_VERSION,
  REQUEST_FRAME_SIZE,
  RESPONSE_FRAME_SIZE,
} from '../src/editor-context-protocol';

test('editor request retains the native 48-byte v1 wire layout', () => {
  const frame = encodeEditorContextRequest(0x4030_2010, {
    windowFocused: true,
    surface: 'editor',
    syntax: 'comment',
    languageId: 'typescript',
  });

  assert.equal(frame.length, REQUEST_FRAME_SIZE);
  assert.equal(frame.toString('ascii', 0, 4), 'CIME');
  assert.equal(frame.readUInt16LE(4), PROTOCOL_VERSION);
  assert.equal(frame.readUInt16LE(6), EDITOR_CONTEXT_REQUEST_TYPE);
  assert.equal(frame.readUInt32LE(8), 32);
  assert.equal(frame.readUInt32LE(12), 0x4030_2010);
  assert.equal(frame[16], 1);
  assert.equal(frame[17], 1);
  assert.equal(frame[18], 2);
  assert.equal(frame[19], 10);
  assert.equal(frame.toString('ascii', 20, 30), 'typescript');
  assert.ok(frame.subarray(30).every((value) => value === 0));
});

test('language IDs are bounded normalized ASCII and never truncated', () => {
  assert.equal(normalizeLanguageId('TypeScriptReact'), 'typescriptreact');
  assert.equal(normalizeLanguageId('objective-cpp'), 'objective-cpp');
  assert.equal(normalizeLanguageId('c++'), 'c++');
  assert.equal(normalizeLanguageId('中文'), '');
  assert.equal(normalizeLanguageId('a'.repeat(LANGUAGE_ID_CAPACITY + 1)), '');

  assert.throws(
    () => encodeEditorContextRequest(1, {
      windowFocused: true,
      surface: 'editor',
      syntax: 'code',
      languageId: 'TypeScript',
    }),
    EditorContextProtocolError,
  );
});

test('editor response round-trips accepted and application error states', () => {
  const accepted = responseFrame(7, 0, 1);
  assert.deepEqual(decodeEditorContextResponse(accepted, 7), {
    requestId: 7,
    status: 'ok',
    accepted: true,
  });

  const rejected = responseFrame(8, 3, 0);
  assert.deepEqual(decodeEditorContextResponse(rejected, 8), {
    requestId: 8,
    status: 'internalError',
    accepted: false,
  });
});

test('editor response decoder rejects corrupted framing and fields', () => {
  const valid = responseFrame(9, 0, 1);
  const fixtures: Array<[string, (frame: Buffer) => void]> = [
    ['magic', (frame) => { frame[0] = 0; }],
    ['version', (frame) => { frame.writeUInt16LE(2, 4); }],
    ['type', (frame) => { frame.writeUInt16LE(2, 6); }],
    ['payload', (frame) => { frame.writeUInt32LE(15, 8); }],
    ['request ID', (frame) => { frame.writeUInt32LE(10, 12); }],
    ['status', (frame) => { frame[16] = 9; }],
    ['accepted', (frame) => { frame[17] = 2; }],
    ['reserved', (frame) => { frame[18] = 1; }],
  ];
  for (const [fixture, mutate] of fixtures) {
    const frame = Buffer.from(valid);
    mutate(frame);
    assert.throws(
      () => decodeEditorContextResponse(frame, 9),
      EditorContextProtocolError,
      fixture,
    );
  }
  assert.throws(
    () => decodeEditorContextResponse(valid.subarray(0, RESPONSE_FRAME_SIZE - 1), 9),
    EditorContextProtocolError,
  );
});

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
