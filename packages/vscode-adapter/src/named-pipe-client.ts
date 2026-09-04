import { createConnection, Socket } from 'node:net';
import { performance } from 'node:perf_hooks';
import {
  CONTEXT_SERVICE_PIPE_NAME,
  decodeEditorContextResponse,
  EditorContextProtocolError,
  EditorContextUpdate,
  EditorResponseStatus,
  encodeEditorContextRequest,
  RESPONSE_ACKNOWLEDGEMENT,
  RESPONSE_FRAME_SIZE,
} from './editor-context-protocol';

export type EditorContextPublishStatus =
  | 'ok'
  | 'rejected'
  | 'serviceUnavailable'
  | 'timeout'
  | 'disconnected'
  | 'protocolError'
  | 'ioError';

export interface EditorContextPublishResult {
  requestId: number;
  status: EditorContextPublishStatus;
  elapsedMs: number;
  responseStatus?: EditorResponseStatus;
  errorCode?: string;
}

type SocketConnector = (endpoint: string) => Socket;

export class ContextServiceEditorClient {
  private nextRequestId = 1;

  constructor(
    readonly endpoint = CONTEXT_SERVICE_PIPE_NAME,
    private readonly connect: SocketConnector = (value) => createConnection(value),
  ) {}

  async publish(
    update: EditorContextUpdate,
    timeoutMs: number,
  ): Promise<EditorContextPublishResult> {
    const requestId = this.allocateRequestId();
    const startedAt = performance.now();
    let requestFrame: Buffer;
    try {
      requestFrame = encodeEditorContextRequest(requestId, update);
    } catch (error) {
      return {
        requestId,
        status: 'protocolError',
        elapsedMs: elapsed(startedAt),
        errorCode: protocolCode(error),
      };
    }

    const deadlineAt = startedAt + Math.max(1, timeoutMs);
    // The production service intentionally serializes requests. Retry only
    // the brief endpoint gap between pipe instances; a genuinely absent
    // service still degrades within the configured deadline and never enters
    // the input key path.
    const retryUntil = deadlineAt;
    while (true) {
      const remainingMs = Math.max(1, Math.ceil(deadlineAt - performance.now()));
      const result = await this.exchange(
        requestId,
        requestFrame,
        remainingMs,
        startedAt,
      );
      if (result.status !== 'serviceUnavailable' ||
          performance.now() >= retryUntil) {
        return result;
      }
      await delay(1);
    }
  }

  private exchange(
    requestId: number,
    requestFrame: Buffer,
    timeoutMs: number,
    startedAt: number,
  ): Promise<EditorContextPublishResult> {
    return new Promise((resolve) => {
      let socket: Socket | null = null;
      let response = Buffer.alloc(0);
      let settled = false;
      let acknowledgementPending = false;
      const finish = (
        result: Omit<EditorContextPublishResult, 'requestId' | 'elapsedMs'>,
      ): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        socket?.destroy();
        resolve({ requestId, elapsedMs: elapsed(startedAt), ...result });
      };
      const timer = setTimeout(
        () => finish({ status: 'timeout' }),
        Math.max(1, timeoutMs),
      );

      try {
        socket = this.connect(this.endpoint);
      } catch (error) {
        finish({ status: statusForError(error), errorCode: errorCode(error) });
        return;
      }

      socket.once('connect', () => {
        socket?.write(requestFrame, (error) => {
          if (error) {
            finish({ status: statusForError(error), errorCode: errorCode(error) });
          }
        });
      });
      socket.on('data', (chunk: Buffer) => {
        if (settled) return;
        response = Buffer.concat([response, chunk]);
        if (response.length > RESPONSE_FRAME_SIZE) {
          finish({ status: 'protocolError', errorCode: 'OVERSIZED_RESPONSE' });
          return;
        }
        if (response.length !== RESPONSE_FRAME_SIZE) return;

        let decoded;
        try {
          decoded = decodeEditorContextResponse(response, requestId);
        } catch (error) {
          finish({
            status: 'protocolError',
            errorCode: protocolCode(error),
          });
          return;
        }
        // The native service disconnects its byte-mode pipe immediately after
        // consuming the acknowledgement. Node can surface that expected
        // post-response disconnect as a read-side EPIPE before the successful
        // acknowledgement write callback runs. At this point the complete,
        // correlated response has already been validated, so let the ACK
        // callback settle the exchange instead of overwriting it with a false
        // serviceUnavailable result.
        acknowledgementPending = true;
        socket?.pause();
        socket?.write(Buffer.from([RESPONSE_ACKNOWLEDGEMENT]), (error) => {
          acknowledgementPending = false;
          if (error) {
            finish({ status: statusForError(error), errorCode: errorCode(error) });
            return;
          }
          finish({
            status:
              decoded.status === 'ok' && decoded.accepted ? 'ok' : 'rejected',
            responseStatus: decoded.status,
          });
        });
      });
      socket.once('end', () => {
        if (!settled && !acknowledgementPending &&
            response.length !== RESPONSE_FRAME_SIZE) {
          finish({ status: 'disconnected' });
        }
      });
      socket.once('close', () => {
        if (!settled && !acknowledgementPending) {
          finish({ status: 'disconnected' });
        }
      });
      socket.once('error', (error) => {
        if (!acknowledgementPending) {
          finish({ status: statusForError(error), errorCode: errorCode(error) });
        }
      });
    });
  }

  private allocateRequestId(): number {
    const value = this.nextRequestId;
    this.nextRequestId = value === 0xffff_ffff ? 1 : value + 1;
    return value;
  }
}

function elapsed(startedAt: number): number {
  return Math.max(0, Math.round(performance.now() - startedAt));
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function statusForError(error: unknown): EditorContextPublishStatus {
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
  return error instanceof EditorContextProtocolError
    ? 'INVALID_PROTOCOL_FRAME'
    : 'ENCODE_FAILURE';
}
