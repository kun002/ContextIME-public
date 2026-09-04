import { readFileSync } from 'node:fs';
import { classifyNodeType, getLanguageDefinition } from './language-registry';
import {
  SyntaxKind,
  SyntaxRuntimeDiagnostics,
  SyntaxRuntimeLogger,
  SyntaxRuntimeOptions,
  SyntaxSnapshot,
  WasmResolver,
} from './types';
import { VSCodeWasmResolver } from './wasm-resolver';

const DEFAULT_OPTIONS: SyntaxRuntimeOptions = { maxCachedDocuments: 8 };

interface LanguageState {
  parser: any;
}

interface DocumentState {
  languageId: string;
  version: number;
  text: string;
  tree: any;
  lastUsedAt: number;
}

const silentLogger: SyntaxRuntimeLogger = {
  debug() {},
  info() {},
  warn() {},
  error() {},
};

/**
 * Multi-document runtime for Microsoft's UTF-16 Tree-sitter build used by VS Code.
 * Node indexes and columns therefore use JavaScript UTF-16 code units, matching
 * TextDocument.offsetAt without conversion to UTF-8 bytes.
 */
export class SyntaxRuntime {
  private static initialization: Promise<any> | null = null;
  private readonly options: SyntaxRuntimeOptions;
  private readonly languages = new Map<string, Promise<LanguageState | null>>();
  private readonly documents = new Map<string, DocumentState>();
  private readonly unsupportedLanguages = new Set<string>();
  private parseCount = 0;
  private cacheHitCount = 0;
  private incrementalParseCount = 0;
  private initialized = false;

  constructor(
    private readonly resolver: WasmResolver = new VSCodeWasmResolver(),
    private readonly logger: SyntaxRuntimeLogger = silentLogger,
    options: Partial<SyntaxRuntimeOptions> = {},
  ) {
    this.options = { ...DEFAULT_OPTIONS, ...options };
  }

  async analyze(input: SyntaxSnapshot): Promise<SyntaxKind> {
    const snapshot = normalizeSnapshot(input);
    if (!getLanguageDefinition(snapshot.languageId)) {
      this.unsupportedLanguages.add(snapshot.languageId);
      return 'unknown';
    }

    const language = await this.getLanguage(snapshot.languageId);
    if (!language) return 'unknown';

    const document = this.getOrParseDocument(snapshot, language);
    if (!document?.tree?.rootNode) return 'unknown';
    document.lastUsedAt = Date.now();
    return classifyAtOffset(document.tree.rootNode, snapshot.text, snapshot.cursorOffset);
  }

  disposeDocument(documentId: string): void {
    const state = this.documents.get(documentId);
    state?.tree?.delete?.();
    this.documents.delete(documentId);
  }

  getDiagnostics(): SyntaxRuntimeDiagnostics {
    return {
      initialized: this.initialized,
      loadedLanguages: [...this.languages.keys()],
      cachedDocuments: this.documents.size,
      parseCount: this.parseCount,
      cacheHitCount: this.cacheHitCount,
      incrementalParseCount: this.incrementalParseCount,
      unsupportedLanguages: [...this.unsupportedLanguages],
    };
  }

  dispose(): void {
    for (const state of this.documents.values()) state.tree?.delete?.();
    this.documents.clear();
    for (const promise of this.languages.values()) {
      void promise.then((state) => state?.parser?.delete?.()).catch(() => undefined);
    }
    this.languages.clear();
  }

  private async getTreeSitter(): Promise<any> {
    if (!SyntaxRuntime.initialization) {
      SyntaxRuntime.initialization = (async () => {
        const treeSitter = require('@vscode/tree-sitter-wasm');
        await treeSitter.Parser.init({ locateFile: () => this.resolver.resolveCoreWasm() });
        return treeSitter;
      })();
    }

    try {
      const value = await SyntaxRuntime.initialization;
      this.initialized = true;
      return value;
    } catch (error) {
      SyntaxRuntime.initialization = null;
      this.logger.error(`[SyntaxRuntime] initialization failed: ${messageOf(error)}`);
      throw error;
    }
  }

  private getLanguage(languageId: string): Promise<LanguageState | null> {
    const cached = this.languages.get(languageId);
    if (cached) return cached;
    const loading = this.loadLanguage(languageId);
    this.languages.set(languageId, loading);
    return loading;
  }

  private async loadLanguage(languageId: string): Promise<LanguageState | null> {
    const wasmPath = this.resolver.resolveLanguageWasm(languageId);
    if (!wasmPath) {
      this.unsupportedLanguages.add(languageId);
      this.logger.warn(`[SyntaxRuntime] no WASM asset for ${languageId}`);
      return null;
    }

    try {
      const treeSitter = await this.getTreeSitter();
      const language = await treeSitter.Language.load(readFileSync(wasmPath));
      const parser = new treeSitter.Parser();
      parser.setLanguage(language);
      this.logger.info(`[SyntaxRuntime] loaded ${languageId} from ${wasmPath}`);
      return { parser };
    } catch (error) {
      this.unsupportedLanguages.add(languageId);
      this.logger.error(`[SyntaxRuntime] failed to load ${languageId}: ${messageOf(error)}`);
      return null;
    }
  }

  private getOrParseDocument(
    snapshot: SyntaxSnapshot,
    language: LanguageState,
  ): DocumentState | null {
    const existing = this.documents.get(snapshot.documentId);
    if (
      existing &&
      existing.languageId === snapshot.languageId &&
      existing.version === snapshot.version &&
      existing.text === snapshot.text
    ) {
      this.cacheHitCount += 1;
      return existing;
    }

    let oldTree: any | undefined;
    let incremental = false;
    if (existing?.languageId === snapshot.languageId) {
      oldTree = existing.tree;
      const edit = computeMinimalUtf16Edit(existing.text, snapshot.text);
      if (edit) {
        oldTree.edit(edit);
        incremental = true;
      }
    } else if (existing) {
      existing.tree?.delete?.();
    }

    let tree: any;
    try {
      tree = language.parser.parse(snapshot.text, oldTree);
    } catch (error) {
      this.logger.warn(
        `[SyntaxRuntime] incremental parse failed for ${snapshot.documentId}: ${messageOf(error)}`,
      );
      tree = language.parser.parse(snapshot.text);
      incremental = false;
    }
    if (!tree) return null;

    if (oldTree && oldTree !== tree) oldTree.delete?.();
    this.parseCount += 1;
    if (incremental) this.incrementalParseCount += 1;

    const state: DocumentState = {
      languageId: snapshot.languageId,
      version: snapshot.version,
      text: snapshot.text,
      tree,
      lastUsedAt: Date.now(),
    };
    this.documents.set(snapshot.documentId, state);
    this.evictDocuments();
    return state;
  }

  private evictDocuments(): void {
    while (this.documents.size > this.options.maxCachedDocuments) {
      let oldest: [string, DocumentState] | undefined;
      for (const entry of this.documents) {
        if (!oldest || entry[1].lastUsedAt < oldest[1].lastUsedAt) oldest = entry;
      }
      if (!oldest) return;
      oldest[1].tree?.delete?.();
      this.documents.delete(oldest[0]);
    }
  }
}

function normalizeSnapshot(snapshot: SyntaxSnapshot): SyntaxSnapshot {
  return {
    ...snapshot,
    languageId: snapshot.languageId.toLowerCase(),
    cursorOffset: clampToCodePointBoundary(
      snapshot.text,
      Math.max(0, Math.min(snapshot.cursorOffset, snapshot.text.length)),
    ),
  };
}

function classifyAtOffset(rootNode: any, text: string, cursorOffset: number): SyntaxKind {
  const exact = classifyNodeAndParents(
    rootNode.descendantForPosition(pointAtUtf16Offset(text, cursorOffset)),
  );
  if (exact) return exact;

  const lineStart = text.lastIndexOf('\n', Math.max(0, cursorOffset - 1)) + 1;
  if (cursorOffset > lineStart) {
    const previousOffset = previousCodePointOffset(text, cursorOffset);
    const previous = classifyNodeAndParents(
      rootNode.descendantForPosition(pointAtUtf16Offset(text, previousOffset)),
    );
    if (previous) return previous;
  }
  return 'code';
}

function classifyNodeAndParents(node: any): 'comment' | 'string' | null {
  for (let current = node; current; current = current.parent) {
    const result = classifyNodeType(String(current.type ?? ''));
    if (result) return result;
  }
  return null;
}

function computeMinimalUtf16Edit(oldText: string, newText: string): any | null {
  if (oldText === newText) return null;

  let start = 0;
  const maxStart = Math.min(oldText.length, newText.length);
  while (start < maxStart && oldText.charCodeAt(start) === newText.charCodeAt(start)) {
    start += 1;
  }
  start = boundaryBeforePair(oldText, start);
  start = boundaryBeforePair(newText, start);

  let oldEnd = oldText.length;
  let newEnd = newText.length;
  while (
    oldEnd > start &&
    newEnd > start &&
    oldText.charCodeAt(oldEnd - 1) === newText.charCodeAt(newEnd - 1)
  ) {
    oldEnd -= 1;
    newEnd -= 1;
  }
  oldEnd = boundaryAfterPair(oldText, oldEnd);
  newEnd = boundaryAfterPair(newText, newEnd);

  return {
    startIndex: start,
    oldEndIndex: oldEnd,
    newEndIndex: newEnd,
    startPosition: pointAtUtf16Offset(oldText, start),
    oldEndPosition: pointAtUtf16Offset(oldText, oldEnd),
    newEndPosition: pointAtUtf16Offset(newText, newEnd),
  };
}

function pointAtUtf16Offset(text: string, offset: number): { row: number; column: number } {
  let row = 0;
  let lineStart = 0;
  for (let index = 0; index < offset; index += 1) {
    if (text.charCodeAt(index) === 10) {
      row += 1;
      lineStart = index + 1;
    }
  }
  return { row, column: offset - lineStart };
}

function previousCodePointOffset(text: string, offset: number): number {
  if (offset <= 0) return 0;
  let result = offset - 1;
  if (
    result > 0 &&
    isLowSurrogate(text.charCodeAt(result)) &&
    isHighSurrogate(text.charCodeAt(result - 1))
  ) {
    result -= 1;
  }
  return result;
}

function clampToCodePointBoundary(text: string, offset: number): number {
  return boundaryBeforePair(text, offset);
}

function boundaryBeforePair(text: string, offset: number): number {
  if (
    offset > 0 &&
    offset < text.length &&
    isHighSurrogate(text.charCodeAt(offset - 1)) &&
    isLowSurrogate(text.charCodeAt(offset))
  ) {
    return offset - 1;
  }
  return offset;
}

function boundaryAfterPair(text: string, offset: number): number {
  if (
    offset > 0 &&
    offset < text.length &&
    isHighSurrogate(text.charCodeAt(offset - 1)) &&
    isLowSurrogate(text.charCodeAt(offset))
  ) {
    return offset + 1;
  }
  return offset;
}

function isHighSurrogate(value: number): boolean {
  return value >= 0xd800 && value <= 0xdbff;
}

function isLowSurrogate(value: number): boolean {
  return value >= 0xdc00 && value <= 0xdfff;
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
