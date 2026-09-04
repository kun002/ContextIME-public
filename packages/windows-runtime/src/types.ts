export type InputMode = 'en' | 'zh';

export interface RuntimeLogger {
  debug(message: string): void;
  info(message: string): void;
  warn(message: string): void;
  error(message: string): void;
}

export interface WindowsNativePort {
  getForegroundLanguageId(): number;
  enumerateLanguageIds(): number[];
  requestLanguageId(languageId: number): boolean;
  getImeOpenStatus(): boolean | null;
  setImeOpenStatus(open: boolean): boolean;
}

export interface WindowsImeRuntimeConfig {
  pollIntervalMs: number;
  autoSwitchSuppressMs: number;
  englishLanguageIds: readonly number[];
  chineseLanguageIds: readonly number[];
}

export interface RuntimeModeChangedEvent {
  mode: InputMode;
  origin: 'manual' | 'auto' | 'unknown';
}

export interface RuntimeSwitchResult {
  success: boolean;
  method: 'layout' | 'ime-open-status' | 'skip' | 'unavailable' | 'failed';
  elapsedMs: number;
  message?: string;
}

export interface RuntimeDiagnostics {
  ready: boolean;
  currentMode: InputMode;
  englishLanguageId: number;
  chineseLanguageId: number;
  pollIntervalMs: number;
  strategy: 'dual-layout' | 'single-layout' | 'unavailable';
  currentLanguageId: number;
  imeOpen: boolean | null;
}

export interface DisposableLike {
  dispose(): void;
}
