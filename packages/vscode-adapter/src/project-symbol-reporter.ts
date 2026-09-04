import * as vscode from 'vscode';
import { ProjectIndexerClient, ProjectIndexPublishResult } from './project-indexer-client';
import { ReportScheduler } from './report-scheduler';
import {
  ProjectSymbolCapture,
  VSCodeProjectSymbolSource,
} from './project-symbol-source';
import { MAX_PROJECT_SYMBOLS_PER_DOCUMENT } from './project-symbols';

export class VSCodeProjectSymbolReporter implements vscode.Disposable {
  private readonly disposables: vscode.Disposable[] = [];
  private readonly scheduler: ReportScheduler;
  private readonly publishedVersions = new WeakMap<vscode.TextDocument, number>();
  private heartbeat: NodeJS.Timeout | null = null;
  private lastCapture: ProjectSymbolCapture | null = null;
  private lastResult: ProjectIndexPublishResult | null = null;
  private lastActivationResult: ProjectIndexPublishResult | null = null;
  private activeProjectId: string | null = null;
  private disabledInvalidationSent = false;
  private started = false;
  private disposed = false;
  private forceNext = false;

  constructor(
    private readonly source: VSCodeProjectSymbolSource,
    private readonly client: ProjectIndexerClient,
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
      vscode.window.onDidChangeActiveTextEditor(() => {
        this.scheduler.schedule('active-editor');
      }),
      vscode.window.onDidChangeWindowState(() => {
        this.scheduler.schedule('window-focus');
      }),
      vscode.workspace.onDidSaveTextDocument((document) => {
        if (vscode.window.activeTextEditor?.document === document) {
          this.scheduler.schedule('document-save');
        }
      }),
      vscode.workspace.onDidChangeWorkspaceFolders(() => {
        this.forceNext = true;
        this.scheduler.schedule('workspace-folders');
      }),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (!event.affectsConfiguration('context-ime.projectDictionary')) return;
        this.forceNext = true;
        this.scheduler.schedule('configuration');
      }),
    );
    this.heartbeat = setInterval(
      () => this.scheduler.schedule('heartbeat'),
      1000,
    );
    this.heartbeat.unref();
    this.scheduler.schedule('activate');
  }

  async reportNow(): Promise<void> {
    this.forceNext = true;
    this.scheduler.schedule('manual-report');
    await this.scheduler.flush();
  }

  diagnostics(): readonly string[] {
    const config = this.configuration();
    return [
      `project symbol collection: ${config.enabled ? 'enabled' : 'disabled'}`,
      `project index endpoint: ${this.client.endpoint}`,
      `project symbol cap: ${config.maximumSymbols}`,
      this.lastCapture
        ? `last project symbols: project=${shortId(this.lastCapture.projectId)} language=${this.lastCapture.languageId} count=${this.lastCapture.entries.length}`
        : 'last project symbols: none',
      this.lastResult
        ? `last project transport: ${this.lastResult.status} accepted=${this.lastResult.acceptedSymbols}/${this.lastResult.attemptedSymbols} batches=${this.lastResult.batches} elapsed=${this.lastResult.elapsedMs}ms response=${this.lastResult.responseStatus ?? '-'} code=${this.lastResult.errorCode ?? '-'}`
        : 'last project transport: none',
      this.lastActivationResult
        ? `active project transport: ${this.lastActivationResult.status} project=${this.activeProjectId ? shortId(this.activeProjectId) : '-'} elapsed=${this.lastActivationResult.elapsedMs}ms code=${this.lastActivationResult.errorCode ?? '-'}`
        : 'active project transport: none',
      'project privacy: Language Server names only; no source text, URI, path, range, detail, or container is transmitted',
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
  }

  private async report(trigger: string): Promise<void> {
    const config = this.configuration();
    if (!config.enabled) {
      if (!this.disabledInvalidationSent) {
        this.lastActivationResult = await this.client.deactivate(config.timeoutMs);
        this.disabledInvalidationSent =
          this.lastActivationResult.status === 'ok';
        if (this.disabledInvalidationSent) this.activeProjectId = null;
      }
      return;
    }
    this.disabledInvalidationSent = false;
    const document = vscode.window.activeTextEditor?.document;
    const projectId = vscode.window.state.focused && document
      ? this.source.projectId(document)
      : null;
    if (!document || !projectId) {
      if (this.activeProjectId !== null) {
        this.lastActivationResult = await this.client.deactivate(config.timeoutMs);
        if (this.lastActivationResult.status === 'ok') {
          this.activeProjectId = null;
        }
      }
      return;
    }

    this.lastActivationResult = await this.client.activate(
      projectId, config.timeoutMs,
    );
    if (this.lastActivationResult.status === 'ok') {
      this.activeProjectId = projectId;
    }
    if (this.disposed) return;
    const force = this.forceNext;
    this.forceNext = false;
    if (!force && this.publishedVersions.get(document) === document.version) {
      return;
    }

    const capturedVersion = document.version;
    const capture = await this.source.capture(document, config.maximumSymbols);
    if (this.disposed || !capture) return;
    this.lastCapture = capture;
    if (capture.entries.length === 0) {
      if (document.version === capturedVersion) {
        this.publishedVersions.set(document, capturedVersion);
      }
      this.output.appendLine(
        `[project:${trigger}] project=${shortId(capture.projectId)} language=${capture.languageId} symbols=0 provider=empty`,
      );
      return;
    }

    const result = await this.client.publish(
      capture.projectId, capture.entries, config.timeoutMs,
    );
    if (this.disposed) return;
    this.lastResult = result;
    if (result.status === 'ok' && document.version === capturedVersion) {
      this.publishedVersions.set(document, capturedVersion);
    }
    this.output.appendLine(
      `[project:${trigger}] project=${shortId(capture.projectId)} language=${capture.languageId} symbols=${capture.entries.length} transport=${result.status} accepted=${result.acceptedSymbols} batches=${result.batches} elapsed=${result.elapsedMs}ms response=${result.responseStatus ?? '-'} code=${result.errorCode ?? '-'}`,
    );
  }

  private configuration(): ProjectDictionaryConfiguration {
    const section = vscode.workspace.getConfiguration(
      'context-ime.projectDictionary',
    );
    return {
      enabled: section.get<boolean>('enabled', true),
      debounceMs: clamp(section.get<number>('debounceMs', 500), 100, 5000),
      timeoutMs: clamp(section.get<number>('timeoutMs', 1000), 100, 5000),
      maximumSymbols: clamp(
        section.get<number>(
          'maximumSymbolsPerDocument',
          MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
        ),
        1,
        MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
      ),
    };
  }
}

interface ProjectDictionaryConfiguration {
  enabled: boolean;
  debounceMs: number;
  timeoutMs: number;
  maximumSymbols: number;
}

function clamp(value: number, minimum: number, maximum: number): number {
  if (!Number.isFinite(value)) return minimum;
  return Math.min(maximum, Math.max(minimum, Math.round(value)));
}

function shortId(projectId: string): string {
  return `${projectId.slice(0, 12)}…`;
}
