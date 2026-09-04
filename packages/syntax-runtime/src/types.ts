export type SyntaxKind = 'code' | 'comment' | 'string' | 'unknown';

export interface SyntaxSnapshot {
  documentId: string;
  languageId: string;
  version: number;
  text: string;
  /** UTF-16 offset, matching VS Code TextDocument.offsetAt. */
  cursorOffset: number;
}

export interface SyntaxRuntimeLogger {
  debug(message: string): void;
  info(message: string): void;
  warn(message: string): void;
  error(message: string): void;
}

export interface WasmResolver {
  resolveCoreWasm(): string;
  resolveLanguageWasm(languageId: string): string | null;
}

export interface SyntaxRuntimeOptions {
  maxCachedDocuments: number;
}

export interface SyntaxRuntimeDiagnostics {
  initialized: boolean;
  loadedLanguages: readonly string[];
  cachedDocuments: number;
  parseCount: number;
  cacheHitCount: number;
  incrementalParseCount: number;
  unsupportedLanguages: readonly string[];
}
