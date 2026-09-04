import {
  isOpaqueProjectId,
  isValidProjectSymbol,
  ProjectSymbolEntry,
  ProjectSymbolSource,
  ProjectSymbolType,
} from './project-symbols';

export const PROJECT_INDEXER_PIPE_NAME =
  '\\\\.\\pipe\\ContextIME.ProjectIndexer.v1';
export const PROJECT_PROTOCOL_VERSION = 1;
export const PROJECT_REQUEST_TYPE = 1;
export const PROJECT_RESPONSE_TYPE = 2;
export const PROJECT_REQUEST_FRAME_SIZE = 4096;
export const PROJECT_RESPONSE_FRAME_SIZE = 32;
export const PROJECT_RESPONSE_ACKNOWLEDGEMENT = 0x06;
export const MAX_PROJECT_RECORDS_PER_REQUEST = 64;

export type ProjectDictionaryOperation =
  | 'upsert'
  | 'activateProject'
  | 'deactivateProject';

const MAGIC = 'CIPD';
const HEADER_SIZE = 16;
const REQUEST_PAYLOAD_SIZE = PROJECT_REQUEST_FRAME_SIZE - HEADER_SIZE;
const RESPONSE_PAYLOAD_SIZE = PROJECT_RESPONSE_FRAME_SIZE - HEADER_SIZE;
const RECORDS_OFFSET = HEADER_SIZE + 32;
const RECORD_HEADER_SIZE = 8;

export type ProjectResponseStatus =
  | 'ok'
  | 'malformedRequest'
  | 'storeError'
  | 'internalError';

export interface ProjectDictionaryResponse {
  requestId: number;
  status: ProjectResponseStatus;
  accepted: boolean;
  acceptedCount: number;
}

export class ProjectDictionaryProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ProjectDictionaryProtocolError';
  }
}

export function splitProjectSymbolEntries(
  entries: readonly ProjectSymbolEntry[],
): ProjectSymbolEntry[][] {
  const batches: ProjectSymbolEntry[][] = [];
  let current: ProjectSymbolEntry[] = [];
  let currentBytes = RECORDS_OFFSET;
  for (const entry of entries) {
    validateEntry(entry);
    const recordBytes = RECORD_HEADER_SIZE + Buffer.byteLength(entry.symbol, 'utf8');
    if (current.length >= MAX_PROJECT_RECORDS_PER_REQUEST ||
        currentBytes + recordBytes > PROJECT_REQUEST_FRAME_SIZE) {
      if (current.length === 0) {
        throw new ProjectDictionaryProtocolError('project symbol cannot fit request');
      }
      batches.push(current);
      current = [];
      currentBytes = RECORDS_OFFSET;
    }
    current.push(entry);
    currentBytes += recordBytes;
  }
  if (current.length > 0) batches.push(current);
  return batches;
}

export function encodeProjectDictionaryRequest(
  requestId: number,
  projectId: string,
  entries: readonly ProjectSymbolEntry[],
): Buffer {
  return encodeRequest(requestId, 'upsert', projectId, entries);
}

export function encodeProjectActivationRequest(
  requestId: number,
  projectId: string | null,
): Buffer {
  return encodeRequest(
    requestId,
    projectId === null ? 'deactivateProject' : 'activateProject',
    projectId ?? '00000000000000000000000000000000',
    [],
  );
}

function encodeRequest(
  requestId: number,
  operation: ProjectDictionaryOperation,
  projectId: string,
  entries: readonly ProjectSymbolEntry[],
): Buffer {
  assertRequestId(requestId);
  if (!isOpaqueProjectId(projectId)) {
    throw new ProjectDictionaryProtocolError('project ID is not canonical');
  }
  if ((operation === 'upsert' &&
       (entries.length === 0 || entries.length > MAX_PROJECT_RECORDS_PER_REQUEST)) ||
      (operation !== 'upsert' && entries.length !== 0)) {
    throw new ProjectDictionaryProtocolError('project record count is invalid');
  }

  const frame = Buffer.alloc(PROJECT_REQUEST_FRAME_SIZE);
  writeHeader(frame, PROJECT_REQUEST_TYPE, REQUEST_PAYLOAD_SIZE, requestId);
  const payload = HEADER_SIZE;
  frame[payload] = operation === 'upsert'
    ? 1
    : operation === 'activateProject' ? 2 : 3;
  frame[payload + 1] = entries.length;
  Buffer.from(projectId, 'hex').copy(frame, payload + 4);

  let offset = RECORDS_OFFSET;
  for (const entry of entries) {
    validateEntry(entry);
    const symbol = Buffer.from(entry.symbol, 'utf8');
    if (offset + RECORD_HEADER_SIZE + symbol.length > frame.length) {
      throw new ProjectDictionaryProtocolError('project request exceeds frame');
    }
    frame[offset] = symbolTypeToWire(entry.symbolType);
    frame[offset + 1] = symbolSourceToWire(entry.source);
    frame.writeUInt16LE(symbol.length, offset + 2);
    frame.writeUInt32LE(entry.frequency, offset + 4);
    symbol.copy(frame, offset + RECORD_HEADER_SIZE);
    offset += RECORD_HEADER_SIZE + symbol.length;
  }
  return frame;
}

export function decodeProjectDictionaryResponse(
  frame: Buffer,
  expectedRequestId: number,
  expectedCount: number,
): ProjectDictionaryResponse {
  assertRequestId(expectedRequestId);
  if (frame.length !== PROJECT_RESPONSE_FRAME_SIZE) {
    throw new ProjectDictionaryProtocolError('unexpected response frame size');
  }
  if (frame.toString('ascii', 0, 4) !== MAGIC ||
      frame.readUInt16LE(4) !== PROJECT_PROTOCOL_VERSION ||
      frame.readUInt16LE(6) !== PROJECT_RESPONSE_TYPE ||
      frame.readUInt32LE(8) !== RESPONSE_PAYLOAD_SIZE ||
      frame.readUInt32LE(12) !== expectedRequestId) {
    throw new ProjectDictionaryProtocolError('invalid response header');
  }
  const status = responseStatusFromWire(frame[HEADER_SIZE]);
  const acceptedByte = frame[HEADER_SIZE + 1];
  const acceptedCount = frame.readUInt16LE(HEADER_SIZE + 2);
  if ((acceptedByte !== 0 && acceptedByte !== 1) ||
      acceptedCount > expectedCount ||
      ((status === 'ok') !== (acceptedByte === 1)) ||
      (acceptedByte === 1 &&
       (status !== 'ok' || acceptedCount !== expectedCount)) ||
      (acceptedByte === 0 && acceptedCount !== 0) ||
      !frame.subarray(HEADER_SIZE + 4).every((value) => value === 0)) {
    throw new ProjectDictionaryProtocolError('invalid response payload');
  }
  return {
    requestId: expectedRequestId,
    status,
    accepted: acceptedByte === 1,
    acceptedCount,
  };
}

function writeHeader(
  frame: Buffer,
  type: number,
  payloadSize: number,
  requestId: number,
): void {
  frame.write(MAGIC, 0, 'ascii');
  frame.writeUInt16LE(PROJECT_PROTOCOL_VERSION, 4);
  frame.writeUInt16LE(type, 6);
  frame.writeUInt32LE(payloadSize, 8);
  frame.writeUInt32LE(requestId, 12);
}

function validateEntry(entry: ProjectSymbolEntry): void {
  if (!isValidProjectSymbol(entry.symbol) ||
      !Number.isInteger(entry.frequency) || entry.frequency <= 0 ||
      entry.frequency > 0xffff_ffff) {
    throw new ProjectDictionaryProtocolError('project symbol entry is invalid');
  }
  symbolTypeToWire(entry.symbolType);
  symbolSourceToWire(entry.source);
}

function symbolTypeToWire(type: ProjectSymbolType): number {
  const values: readonly ProjectSymbolType[] = [
    'class', 'method', 'property', 'enum', 'namespace',
    'file', 'directory', 'asset', 'shader', 'term',
  ];
  const value = values.indexOf(type);
  if (value < 0) throw new ProjectDictionaryProtocolError('unknown symbol type');
  return value;
}

function symbolSourceToWire(source: ProjectSymbolSource): number {
  const values: readonly ProjectSymbolSource[] = [
    'language_server', 'compilation_database', 'project_file',
    'file_system', 'manual',
  ];
  const value = values.indexOf(source);
  if (value < 0) throw new ProjectDictionaryProtocolError('unknown symbol source');
  return value;
}

function responseStatusFromWire(value: number): ProjectResponseStatus {
  switch (value) {
    case 0: return 'ok';
    case 1: return 'malformedRequest';
    case 2: return 'storeError';
    case 3: return 'internalError';
    default:
      throw new ProjectDictionaryProtocolError('invalid response status');
  }
}

function assertRequestId(requestId: number): void {
  if (!Number.isInteger(requestId) || requestId < 0 || requestId > 0xffff_ffff) {
    throw new ProjectDictionaryProtocolError('request ID is outside uint32');
  }
}
