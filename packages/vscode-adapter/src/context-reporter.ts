import * as vscode from 'vscode';
import { EditorContextUpdate } from './editor-context-protocol';
import {
  ContextServiceEditorClient,
  EditorContextPublishResult,
} from './named-pipe-client';
import { ReportScheduler } from './report-scheduler';
import { VSCodeContextProvider } from './context-provider';

export class VSCodeContextReporter implements vscode.Disposable {
  private readonly disposables: vscode.Disposable[] = [];
  private readonly scheduler: ReportScheduler;
  private heartbeat: NodeJS.Timeout | null = null;
  private lastUpdate: EditorContextUpdate | null = null;
  private lastResult: EditorContextPublishResult | null = null;
  private lastLoggedState: string | null = null;
  private disabledInvalidationSent = false;
  private started = false;
  private disposed = false;

  constructor(
    private readonly provider: VSCodeContextProvider,
    private readonly client: ContextServiceEditorClient,
    private readonly output: vscode.OutputChannel,
  ) {
    this.scheduler = new ReportScheduler(
      (trigger) => this.report(trigger),
      () => this.configuration().debounceMs,
    );
  }

  start(): void {
    if (this.started || this.disposed) return;
    this.started = true;
    this.disposables.push(
      vscode.window.onDidChangeTextEditorSelection(() => {
        this.provider.noteEditorInteraction();
        this.scheduler.schedule('editor-selection');
      }),
      vscode.window.onDidChangeActiveTextEditor((editor) => {
        if (editor) this.provider.noteEditorInteraction();
        else this.provider.noteUnknownSurface();
        this.scheduler.schedule('active-editor');
      }),
      vscode.workspace.onDidChangeTextDocument((event) => {
        if (
          this.provider.currentSurface() === 'editor' &&
          vscode.window.activeTextEditor?.document === event.document
        ) {
          this.scheduler.schedule('editor-document');
        }
      }),
      vscode.window.onDidChangeActiveTerminal((terminal) => {
        this.provider.noteTerminalInteraction(terminal);
        this.scheduler.schedule('active-terminal');
      }),
      vscode.window.onDidChangeTerminalState((terminal) => {
        if (terminal.state.isInteractedWith) {
          this.provider.noteTerminalInteraction(terminal);
          this.scheduler.schedule('terminal-interaction');
        }
      }),
      vscode.window.onDidChangeWindowState(() => {
        this.provider.noteWindowFocusChanged();
        this.scheduler.schedule('window-focus');
      }),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (!event.affectsConfiguration('context-ime.adapter')) return;
        this.disabledInvalidationSent = false;
        this.resetHeartbeat();
        this.scheduler.schedule('configuration');
      }),
    );
    this.resetHeartbeat();
    this.scheduler.schedule('activate');
  }

  async reportNow(): Promise<void> {
    this.scheduler.schedule('manual-report');
    await this.scheduler.flush();
  }

  diagnostics(): readonly string[] {
    const config = this.configuration();
    return [
      'mode: report-only (never switches the system IME)',
      `enabled: ${config.enabled}`,
      `endpoint: ${this.client.endpoint}`,
      `heartbeat: ${config.heartbeatMs}ms`,
      `deadline: ${config.timeoutMs}ms`,
      this.lastUpdate
        ? `last context: focused=${this.lastUpdate.windowFocused} surface=${this.lastUpdate.surface} syntax=${this.lastUpdate.syntax} language=${this.lastUpdate.languageId || '-'}`
        : 'last context: none',
      this.lastResult
        ? `last transport: ${this.lastResult.status} request=${this.lastResult.requestId} elapsed=${this.lastResult.elapsedMs}ms response=${this.lastResult.responseStatus ?? '-'} code=${this.lastResult.errorCode ?? '-'}`
        : 'last transport: none',
      ...this.provider.diagnostics(),
      'privacy: no source text, URI, project path, token, or environment value is transmitted',
    ];
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.scheduler.dispose();
    if (this.heartbeat) clearInterval(this.heartbeat);
    this.heartbeat = null;
    for (const item of this.disposables) item.dispose();
    this.disposables.length = 0;
    this.provider.dispose();
  }

  private async report(trigger: string): Promise<void> {
    const config = this.configuration();
    let update: EditorContextUpdate;
    if (!config.enabled) {
      if (this.disabledInvalidationSent) return;
      update = {
        windowFocused: false,
        surface: 'unknown',
        syntax: 'unknown',
        languageId: '',
      };
      this.disabledInvalidationSent = true;
    } else {
      update = await this.provider.capture();
      this.disabledInvalidationSent = false;
    }

    const result = await this.client.publish(update, config.timeoutMs);
    if (this.disposed) return;
    this.lastUpdate = update;
    this.lastResult = result;
    const stateKey = [
      update.windowFocused,
      update.surface,
      update.syntax,
      update.languageId,
      result.status,
      result.responseStatus ?? '',
      result.errorCode ?? '',
    ].join(':');
    if (trigger !== 'heartbeat' || stateKey !== this.lastLoggedState) {
      this.output.appendLine(
        `[report:${trigger}] request=${result.requestId} focused=${update.windowFocused} surface=${update.surface} syntax=${update.syntax} language=${update.languageId || '-'} transport=${result.status} response=${result.responseStatus ?? '-'} elapsed=${result.elapsedMs}ms code=${result.errorCode ?? '-'}`,
      );
      this.lastLoggedState = stateKey;
    }
  }

  private resetHeartbeat(): void {
    if (this.heartbeat) clearInterval(this.heartbeat);
    this.heartbeat = null;
    const config = this.configuration();
    if (!config.enabled) return;
    this.heartbeat = setInterval(
      () => this.scheduler.schedule('heartbeat'),
      config.heartbeatMs,
    );
    this.heartbeat.unref();
  }

  private configuration(): AdapterConfiguration {
    const section = vscode.workspace.getConfiguration('context-ime.adapter');
    return {
      enabled: section.get<boolean>('enabled', true),
      debounceMs: clamp(section.get<number>('debounceMs', 35), 0, 500),
      heartbeatMs: clamp(section.get<number>('heartbeatMs', 1000), 250, 1500),
      timeoutMs: clamp(section.get<number>('timeoutMs', 250), 25, 1000),
    };
  }
}

interface AdapterConfiguration {
  enabled: boolean;
  debounceMs: number;
  heartbeatMs: number;
  timeoutMs: number;
}

function clamp(value: number, minimum: number, maximum: number): number {
  if (!Number.isFinite(value)) return minimum;
  return Math.min(maximum, Math.max(minimum, Math.round(value)));
}
