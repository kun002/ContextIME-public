import {
  DisposableLike,
  InputMode,
  RuntimeDiagnostics,
  RuntimeLogger,
  RuntimeModeChangedEvent,
  RuntimeSwitchResult,
  WindowsImeRuntimeConfig,
  WindowsNativePort,
} from './types';

export const DEFAULT_WINDOWS_RUNTIME_CONFIG: WindowsImeRuntimeConfig = {
  pollIntervalMs: 250,
  autoSwitchSuppressMs: 750,
  englishLanguageIds: [1033],
  chineseLanguageIds: [2052, 1028, 3076, 5124, 4100],
};

interface SystemState {
  languageId: number;
  imeOpen: boolean | null;
  mode: InputMode | null;
}

export class WindowsImeRuntime implements DisposableLike {
  private readonly listeners = new Set<(event: RuntimeModeChangedEvent) => void>();
  private readonly config: WindowsImeRuntimeConfig;
  private englishLanguageId = 0;
  private chineseLanguageId = 0;
  private currentMode: InputMode = 'en';
  private expectedAutoMode: InputMode | null = null;
  private suppressExternalUntil = Number.NEGATIVE_INFINITY;
  private timer: NodeJS.Timeout | null = null;

  constructor(
    private readonly port: WindowsNativePort,
    private readonly logger: RuntimeLogger,
    config: Partial<WindowsImeRuntimeConfig> = {},
  ) {
    this.config = { ...DEFAULT_WINDOWS_RUNTIME_CONFIG, ...config };
    this.initializeLayouts();
  }

  isReady(): boolean {
    return this.chineseLanguageId !== 0 &&
      (this.englishLanguageId !== 0 || this.port.getImeOpenStatus() !== null);
  }

  getCurrentMode(): InputMode {
    return this.currentMode;
  }

  start(): void {
    if (this.timer || !this.isReady()) return;
    this.timer = setInterval(() => this.syncFromSystem(), this.config.pollIntervalMs);
    this.timer.unref?.();
  }

  onDidChangeMode(listener: (event: RuntimeModeChangedEvent) => void): DisposableLike {
    this.listeners.add(listener);
    return { dispose: () => this.listeners.delete(listener) };
  }

  switchTo(mode: InputMode, now = Date.now()): RuntimeSwitchResult {
    const startedAt = Date.now();
    if (!this.isReady()) {
      return {
        success: false,
        method: 'unavailable',
        elapsedMs: Date.now() - startedAt,
        message: 'A Chinese keyboard layout and a controllable English mode are required.',
      };
    }

    const state = this.readSystemState();
    if (state.mode === mode) {
      this.currentMode = mode;
      return { success: true, method: 'skip', elapsedMs: Date.now() - startedAt };
    }

    this.expectedAutoMode = mode;
    this.suppressExternalUntil = now + this.config.autoSwitchSuppressMs;

    let accepted = false;
    let method: RuntimeSwitchResult['method'] = 'failed';
    if (mode === 'en' && this.englishLanguageId !== 0) {
      accepted = this.port.requestLanguageId(this.englishLanguageId);
      method = 'layout';
    } else if (mode === 'en') {
      accepted = this.port.setImeOpenStatus(false);
      method = 'ime-open-status';
    } else if (this.isChineseLanguageId(state.languageId)) {
      accepted = this.port.setImeOpenStatus(true);
      method = 'ime-open-status';
    } else {
      accepted = this.port.requestLanguageId(this.chineseLanguageId);
      method = 'layout';
    }

    if (!accepted) {
      this.expectedAutoMode = null;
      return {
        success: false,
        method: 'failed',
        elapsedMs: Date.now() - startedAt,
        message: 'The foreground input context rejected the mode change.',
      };
    }

    this.currentMode = mode;
    this.emit({ mode, origin: 'auto' });
    this.logger.debug(`[WindowsRuntime] auto switch -> ${mode} (${method})`);
    return { success: true, method, elapsedMs: Date.now() - startedAt };
  }

  syncFromSystem(now = Date.now()): InputMode {
    let observed = this.readSystemState();

    if (this.expectedAutoMode && now <= this.suppressExternalUntil) {
      if (
        this.expectedAutoMode === 'zh' &&
        this.isChineseLanguageId(observed.languageId) &&
        observed.mode !== 'zh'
      ) {
        this.port.setImeOpenStatus(true);
        observed = this.readSystemState();
      }

      if (observed.mode === this.expectedAutoMode) {
        this.currentMode = this.expectedAutoMode;
        this.expectedAutoMode = null;
      }
      return this.currentMode;
    }

    const timedOutAutoMode = this.expectedAutoMode;
    this.expectedAutoMode = null;
    if (!observed.mode || observed.mode === this.currentMode) return this.currentMode;

    this.currentMode = observed.mode;
    const origin = timedOutAutoMode ? 'unknown' : 'manual';
    this.logger.info(`[WindowsRuntime] ${origin} switch -> ${observed.mode}`);
    this.emit({ mode: observed.mode, origin });
    return observed.mode;
  }

  getDiagnostics(): RuntimeDiagnostics {
    const state = this.readSystemState();
    return {
      ready: this.isReady(),
      currentMode: this.currentMode,
      englishLanguageId: this.englishLanguageId,
      chineseLanguageId: this.chineseLanguageId,
      pollIntervalMs: this.config.pollIntervalMs,
      strategy:
        this.chineseLanguageId === 0
          ? 'unavailable'
          : this.englishLanguageId !== 0
            ? 'dual-layout'
            : this.port.getImeOpenStatus() !== null
              ? 'single-layout'
              : 'unavailable',
      currentLanguageId: state.languageId,
      imeOpen: state.imeOpen,
    };
  }

  dispose(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    this.listeners.clear();
  }

  private initializeLayouts(): void {
    const languageIds = this.port.enumerateLanguageIds();
    this.englishLanguageId =
      languageIds.find((id) => this.config.englishLanguageIds.includes(id)) ?? 0;
    this.chineseLanguageId =
      languageIds.find((id) => this.config.chineseLanguageIds.includes(id)) ?? 0;
    this.currentMode = this.readSystemState().mode ?? 'en';

    if (this.isReady()) {
      this.logger.info(
        `[WindowsRuntime] layouts en=${this.englishLanguageId}, zh=${this.chineseLanguageId}`,
      );
    } else {
      this.logger.warn(
        `[WindowsRuntime] unavailable: en=${this.englishLanguageId}, zh=${this.chineseLanguageId}`,
      );
    }
  }

  private readSystemState(): SystemState {
    const languageId = this.port.getForegroundLanguageId();
    if (this.config.englishLanguageIds.includes(languageId)) {
      return { languageId, imeOpen: null, mode: 'en' };
    }
    if (this.isChineseLanguageId(languageId)) {
      const imeOpen = this.port.getImeOpenStatus();
      return { languageId, imeOpen, mode: imeOpen === false ? 'en' : 'zh' };
    }
    return { languageId, imeOpen: null, mode: null };
  }

  private isChineseLanguageId(languageId: number): boolean {
    return this.config.chineseLanguageIds.includes(languageId);
  }

  private emit(event: RuntimeModeChangedEvent): void {
    for (const listener of this.listeners) listener(event);
  }
}
