export const CONTEXT_SERVICE_PIPE_NAME =
  '\\\\.\\pipe\\ContextIME.ContextService.v1';
export const PROTOCOL_VERSION = 1;
export const EDITOR_CONTEXT_REQUEST_TYPE = 3;
export const EDITOR_CONTEXT_RESPONSE_TYPE = 4;
export const REQUEST_FRAME_SIZE = 48;
export const RESPONSE_FRAME_SIZE = 32;
export const RESPONSE_ACKNOWLEDGEMENT = 0x06;
export const LANGUAGE_ID_CAPACITY = 20;

const HEADER_SIZE = 16;
const REQUEST_PAYLOAD_SIZE = 32;
const RESPONSE_PAYLOAD_SIZE = 16;
const MAGIC = 'CIME';

export type EditorSurface = 'unknown' | 'editor' | 'integratedTerminal';
export type EditorSyntax =
  | 'unknown'
  | 'code'
  | 'comment'
  | 'string'
  | 'markdownText'
  | 'markdownCode';

export interface EditorContextUpdate {
  windowFocused: boolean;
  surface: EditorSurface;
  syntax: EditorSyntax;
  languageId: string;
}

export type EditorResponseStatus =
  | 'ok'
  | 'unsupportedVersion'
  | 'malformedRequest'
  | 'internalError';

export interface EditorContextResponse {
  requestId: number;
  status: EditorResponseStatus;
  accepted: boolean;
}

export class EditorContextProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'EditorContextProtocolError';
  }
}

export function normalizeLanguageId(value: string): string {
  const normalized = value.toLowerCase();
  if (
    normalized.length > LANGUAGE_ID_CAPACITY ||
    !/^[a-z0-9_.+\-]*$/.test(normalized)
  ) {
    return '';
  }
  return normalized;
}

export function encodeEditorContextRequest(
  requestId: number,
  update: EditorContextUpdate,
): Buffer {
  assertRequestId(requestId);
  if (normalizeLanguageId(update.languageId) !== update.languageId) {
    throw new EditorContextProtocolError('language ID is not normalized');
  }

  const frame = Buffer.alloc(REQUEST_FRAME_SIZE);
  frame.write(MAGIC, 0, 'ascii');
  frame.writeUInt16LE(PROTOCOL_VERSION, 4);
  frame.writeUInt16LE(EDITOR_CONTEXT_REQUEST_TYPE, 6);
  frame.writeUInt32LE(REQUEST_PAYLOAD_SIZE, 8);
  frame.writeUInt32LE(requestId, 12);

  const payload = HEADER_SIZE;
  frame[payload] = update.windowFocused ? 0x01 : 0;
  frame[payload + 1] = surfaceToWire(update.surface);
  frame[payload + 2] = syntaxToWire(update.syntax);
  frame[payload + 3] = update.languageId.length;
  frame.write(update.languageId, payload + 4, 'ascii');
  return frame;
}

export function decodeEditorContextResponse(
  frame: Buffer,
  expectedRequestId: number,
): EditorContextResponse {
  assertRequestId(expectedRequestId);
  if (frame.length !== RESPONSE_FRAME_SIZE) {
    throw new EditorContextProtocolError('unexpected response frame size');
  }
  if (frame.toString('ascii', 0, 4) !== MAGIC) {
    throw new EditorContextProtocolError('bad response magic');
  }
  if (frame.readUInt16LE(4) !== PROTOCOL_VERSION) {
    throw new EditorContextProtocolError('unsupported response version');
  }
  if (frame.readUInt16LE(6) !== EDITOR_CONTEXT_RESPONSE_TYPE) {
    throw new EditorContextProtocolError('unexpected response message type');
  }
  if (frame.readUInt32LE(8) !== RESPONSE_PAYLOAD_SIZE) {
    throw new EditorContextProtocolError('unexpected response payload size');
  }

  const requestId = frame.readUInt32LE(12);
  if (requestId !== expectedRequestId) {
    throw new EditorContextProtocolError('response request ID mismatch');
  }
  const status = responseStatusFromWire(frame[HEADER_SIZE]);
  const accepted = frame[HEADER_SIZE + 1];
  if (accepted !== 0 && accepted !== 1) {
    throw new EditorContextProtocolError('invalid accepted flag');
  }
  for (let index = HEADER_SIZE + 2; index < frame.length; index += 1) {
    if (frame[index] !== 0) {
      throw new EditorContextProtocolError('non-zero response reserved byte');
    }
  }
  return { requestId, status, accepted: accepted === 1 };
}

function assertRequestId(requestId: number): void {
  if (
    !Number.isInteger(requestId) ||
    requestId < 0 ||
    requestId > 0xffff_ffff
  ) {
    throw new EditorContextProtocolError('request ID is outside uint32');
  }
}

function surfaceToWire(surface: EditorSurface): number {
  switch (surface) {
    case 'unknown': return 0;
    case 'editor': return 1;
    case 'integratedTerminal': return 2;
  }
}

function syntaxToWire(syntax: EditorSyntax): number {
  switch (syntax) {
    case 'unknown': return 0;
    case 'code': return 1;
    case 'comment': return 2;
    case 'string': return 3;
    case 'markdownText': return 4;
    case 'markdownCode': return 5;
  }
}

function responseStatusFromWire(value: number): EditorResponseStatus {
  switch (value) {
    case 0: return 'ok';
    case 1: return 'unsupportedVersion';
    case 2: return 'malformedRequest';
    case 3: return 'internalError';
    default:
      throw new EditorContextProtocolError('invalid response status');
  }
}
