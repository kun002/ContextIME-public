import { createConnection, Socket } from 'node:net';
import { performance } from 'node:perf_hooks';
import {
  decodeProjectDictionaryResponse,
  encodeProjectActivationRequest,
  encodeProjectDictionaryRequest,
  PROJECT_INDEXER_PIPE_NAME,
  PROJECT_RESPONSE_ACKNOWLEDGEMENT,
  PROJECT_RESPONSE_FRAME_SIZE,
  ProjectDictionaryProtocolError,
  ProjectResponseStatus,
  splitProjectSymbolEntries,
} from './project-dictionary-protocol';
import { ProjectSymbolEntry } from './project-symbols';

export type ProjectIndexPublishStatus =
  | 'ok'
  | 'rejected'
  | 'serviceUnavailable'
  | 'timeout'
  | 'disconnected'
  | 'protocolError'
  | 'ioError';

export interface ProjectIndexPublishResult {
  requestId: number;
  status: ProjectIndexPublishStatus;
  attemptedSymbols: number;
  acceptedSymbols: number;
  batches: number;
  elapsedMs: number;
  responseStatus?: ProjectResponseStatus;
  errorCode?: string;
}

type SocketConnector = (endpoint: string) => Socket;
type BatchResult = {
  requestId: number;
  status: ProjectIndexPublishStatus;
  acceptedSymbols: number;
  responseStatus?: ProjectResponseStatus;
  errorCode?: string;
};

export class ProjectIndexerClient {
  private nextRequestId = 1;

  constructor(
    readonly endpoint = PROJECT_INDEXER_PIPE_NAME,
    private readonly connect: SocketConnector = (value) => createConnection(value),
  ) {}

  async publish(
    projectId: string,
    entries: readonly ProjectSymbolEntry[],
    timeoutMs: number,
  ): Promise<ProjectIndexPublishResult> {
    const startedAt = performance.now();
    let batches: ProjectSymbolEntry[][];
    try {
      batches = splitProjectSymbolEntries(entries);
    } catch (error) {
      return this.result(0, 'protocolError', entries.length, 0, 0, startedAt, {
        errorCode: protocolCode(error),
      });
    }
    if (batches.length === 0) {
      return this.result(0, 'ok', 0, 0, 0, startedAt);
    }

    const deadlineAt = startedAt + Math.max(1, timeoutMs);
    let acceptedSymbols = 0;
    let completedBatches = 0;
    let lastRequestId = 0;
    for (const batch of batches) {
      lastRequestId = this.allocateRequestId();
      let requestFrame: Buffer;
      try {
        requestFrame = encodeProjectDictionaryRequest(
          lastRequestId, projectId, batch,
        );
      } catch (error) {
        return this.result(
          lastRequestId, 'protocolError', entries.length, acceptedSymbols,
          completedBatches, startedAt, { errorCode: protocolCode(error) },
        );
      }

      let batchResult: BatchResult;
      while (true) {
        const remainingMs = Math.ceil(deadlineAt - performance.now());
        if (remainingMs <= 0) {
          return this.result(
            lastRequestId, 'timeout', entries.length, acceptedSymbols,
            completedBatches, startedAt,
          );
        }
        batchResult = await this.exchange(
          lastRequestId, requestFrame, batch.length, remainingMs,
        );
        if (batchResult.status !== 'serviceUnavailable' ||
            performance.now() >= deadlineAt) {
          break;
        }
        await delay(10);
      }
      if (batchResult.status !== 'ok') {
        return this.result(
          batchResult.requestId, batchResult.status, entries.length,
          acceptedSymbols, completedBatches, startedAt, {
            responseStatus: batchResult.responseStatus,
            errorCode: batchResult.errorCode,
          },
        );
      }
      acceptedSymbols += batchResult.acceptedSymbols;
      completedBatches += 1;
    }
    return this.result(
      lastRequestId, 'ok', entries.length, acceptedSymbols,
      completedBatches, startedAt,
    );
  }

  async activate(
    projectId: string,
    timeoutMs: number,
  ): Promise<ProjectIndexPublishResult> {
    return this.updateActivation(projectId, timeoutMs);
  }

  async deactivate(timeoutMs: number): Promise<ProjectIndexPublishResult> {
    return this.updateActivation(null, timeoutMs);
  }

  private async updateActivation(
    projectId: string | null,
    timeoutMs: number,
  ): Promise<ProjectIndexPublishResult> {
    const startedAt = performance.now();
    const requestId = this.allocateRequestId();
    let frame: Buffer;
    try {
      frame = encodeProjectActivationRequest(requestId, projectId);
    } catch (error) {
      return this.result(requestId, 'protocolError', 0, 0, 0, startedAt, {
        errorCode: protocolCode(error),
      });
    }
    const result = await this.exchange(
      requestId, frame, 0, Math.max(1, timeoutMs),
    );
    return this.result(
      requestId, result.status, 0, result.acceptedSymbols,
      result.status === 'ok' ? 1 : 0, startedAt, {
        responseStatus: result.responseStatus,
        errorCode: result.errorCode,
      },
    );
  }

  private exchange(
    requestId: number,
    requestFrame: Buffer,
    expectedCount: number,
    timeoutMs: number,
  ): Promise<BatchResult> {
    return new Promise((resolve) => {
      let socket: Socket | null = null;
      let response = Buffer.alloc(0);
      let settled = false;
      let acknowledgementPending = false;
      const finish = (result: Omit<BatchResult, 'requestId'>): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        socket?.destroy();
        resolve({ requestId, ...result });
      };
      const timer = setTimeout(
        () => finish({ status: 'timeout', acceptedSymbols: 0 }),
        Math.max(1, timeoutMs),
      );

      try {
        socket = this.connect(this.endpoint);
      } catch (error) {
        finish({
          status: statusForError(error),
          acceptedSymbols: 0,
          errorCode: errorCode(error),
        });
        return;
      }
      socket.once('connect', () => {
        socket?.write(requestFrame, (error) => {
          if (error) {
            finish({
              status: statusForError(error),
              acceptedSymbols: 0,
              errorCode: errorCode(error),
            });
          }
        });
      });
      socket.on('data', (chunk: Buffer) => {
        if (settled) return;
        response = Buffer.concat([response, chunk]);
        if (response.length > PROJECT_RESPONSE_FRAME_SIZE) {
          finish({
            status: 'protocolError', acceptedSymbols: 0,
            errorCode: 'OVERSIZED_RESPONSE',
          });
          return;
        }
        if (response.length !== PROJECT_RESPONSE_FRAME_SIZE) return;

        let decoded;
        try {
          decoded = decodeProjectDictionaryResponse(
            response, requestId, expectedCount,
          );
        } catch (error) {
          finish({
            status: 'protocolError', acceptedSymbols: 0,
            errorCode: protocolCode(error),
          });
          return;
        }
        acknowledgementPending = true;
        socket?.pause();
        socket?.write(Buffer.from([PROJECT_RESPONSE_ACKNOWLEDGEMENT]), (error) => {
          acknowledgementPending = false;
          if (error) {
            finish({
              status: statusForError(error), acceptedSymbols: 0,
              errorCode: errorCode(error),
            });
            return;
          }
          finish({
            status: decoded.status === 'ok' && decoded.accepted
              ? 'ok' : 'rejected',
            acceptedSymbols: decoded.acceptedCount,
            responseStatus: decoded.status,
          });
        });
      });
      socket.once('end', () => {
        if (!settled && !acknowledgementPending &&
            response.length !== PROJECT_RESPONSE_FRAME_SIZE) {
          finish({ status: 'disconnected', acceptedSymbols: 0 });
        }
      });
      socket.once('close', () => {
        if (!settled && !acknowledgementPending) {
          finish({ status: 'disconnected', acceptedSymbols: 0 });
        }
      });
      socket.once('error', (error) => {
        if (!acknowledgementPending) {
          finish({
            status: statusForError(error), acceptedSymbols: 0,
            errorCode: errorCode(error),
          });
        }
      });
    });
  }

  private result(
    requestId: number,
    status: ProjectIndexPublishStatus,
    attemptedSymbols: number,
    acceptedSymbols: number,
    batches: number,
    startedAt: number,
    details: { responseStatus?: ProjectResponseStatus; errorCode?: string } = {},
  ): ProjectIndexPublishResult {
    return {
      requestId,
      status,
      attemptedSymbols,
      acceptedSymbols,
      batches,
      elapsedMs: Math.max(0, Math.round(performance.now() - startedAt)),
      ...details,
    };
  }

  private allocateRequestId(): number {
    const value = this.nextRequestId;
    this.nextRequestId = value === 0xffff_ffff ? 1 : value + 1;
    return value;
  }
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function statusForError(error: unknown): ProjectIndexPublishStatus {
  const code = errorCode(error);
  return code === 'ENOENT' || code === 'ECONNREFUSED' || code === 'EPIPE'
    ? 'serviceUnavailable'
    : 'ioError';
}

function errorCode(error: unknown): string | undefined {
  if (typeof error !== 'object' || error === null || !('code' in error)) {
    return undefined;
  }
  const value = (error as { code?: unknown }).code;
  return typeof value === 'string' ? value : undefined;
}

function protocolCode(error: unknown): string {
  return error instanceof ProjectDictionaryProtocolError
    ? 'INVALID_PROTOCOL_FRAME'
    : 'ENCODE_FAILURE';
}
