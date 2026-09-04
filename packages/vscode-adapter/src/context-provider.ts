import * as vscode from 'vscode';
import {
  getLanguageDefinition,
  SyntaxKind,
  SyntaxRuntime,
} from '@context-ime/syntax-runtime';
import {
  EditorContextUpdate,
  EditorSurface,
  EditorSyntax,
  normalizeLanguageId,
} from './editor-context-protocol';

export class VSCodeContextProvider implements vscode.Disposable {
  private readonly syntaxRuntime = new SyntaxRuntime();
  private readonly documentIds = new WeakMap<vscode.TextDocument, string>();
  private nextDocumentId = 1;
  private surface: EditorSurface = 'unknown';

  noteEditorInteraction(): void {
    this.surface = 'editor';
  }

  noteTerminalInteraction(terminal: vscode.Terminal | undefined): void {
    this.surface = terminal && terminal === vscode.window.activeTerminal
      ? 'integratedTerminal'
      : 'unknown';
  }

  noteUnknownSurface(): void {
    this.surface = 'unknown';
  }

  noteWindowFocusChanged(): void {
    // VS Code does not expose which workbench control receives focus when the
    // window itself is reactivated. Wait for a reliable editor/terminal event.
    this.surface = 'unknown';
  }

  currentSurface(): EditorSurface {
    return this.surface;
  }

  async capture(): Promise<EditorContextUpdate> {
    if (!vscode.window.state.focused) return unavailableUpdate(false);

    if (this.surface === 'integratedTerminal') {
      return vscode.window.activeTerminal
        ? {
            windowFocused: true,
            surface: 'integratedTerminal',
            syntax: 'unknown',
            languageId: '',
          }
        : unavailableUpdate(true);
    }
    if (this.surface !== 'editor') return unavailableUpdate(true);

    const editor = vscode.window.activeTextEditor;
    if (!editor) return unavailableUpdate(true);
    const syntax = await this.detectSyntax(editor);
    if (
      !vscode.window.state.focused ||
      this.surface !== 'editor' ||
      vscode.window.activeTextEditor !== editor
    ) {
      return unavailableUpdate(vscode.window.state.focused);
    }
    return {
      windowFocused: true,
      surface: 'editor',
      syntax,
      languageId: normalizeLanguageId(editor.document.languageId),
    };
  }

  diagnostics(): readonly string[] {
    const value = this.syntaxRuntime.getDiagnostics();
    return [
      `surface hint: ${this.surface}`,
      `syntax runtime: ${value.initialized ? 'initialized' : 'not initialized'}`,
      `loaded languages: ${value.loadedLanguages.join(', ') || '-'}`,
      `cached opaque documents: ${value.cachedDocuments}`,
    ];
  }

  dispose(): void {
    this.syntaxRuntime.dispose();
  }

  private async detectSyntax(editor: vscode.TextEditor): Promise<EditorSyntax> {
    const languageId = editor.document.languageId.toLowerCase();
    if (languageId === 'markdown') return detectMarkdownSyntax(editor);
    if (!getLanguageDefinition(languageId)) return 'unknown';

    try {
      const result = await this.syntaxRuntime.analyze({
        documentId: this.opaqueDocumentId(editor.document),
        languageId,
        version: editor.document.version,
        text: editor.document.getText(),
        cursorOffset: editor.document.offsetAt(editor.selection.active),
      });
      if (result !== 'unknown') return syntaxKindToEditorSyntax(result);
    } catch {
      // A local parser failure must degrade to a conservative bounded check.
    }
    return detectConservativeCodeSyntax(editor, languageId);
  }

  private opaqueDocumentId(document: vscode.TextDocument): string {
    const existing = this.documentIds.get(document);
    if (existing) return existing;
    const value = `document-${this.nextDocumentId++}`;
    this.documentIds.set(document, value);
    return value;
  }
}

function unavailableUpdate(windowFocused: boolean): EditorContextUpdate {
  return {
    windowFocused,
    surface: 'unknown',
    syntax: 'unknown',
    languageId: '',
  };
}

function syntaxKindToEditorSyntax(value: SyntaxKind): EditorSyntax {
  switch (value) {
    case 'code': return 'code';
    case 'comment': return 'comment';
    case 'string': return 'string';
    case 'unknown': return 'unknown';
  }
}

function detectMarkdownSyntax(editor: vscode.TextEditor): EditorSyntax {
  const position = editor.selection.active;
  let fenceCount = 0;
  for (let line = 0; line <= position.line; line += 1) {
    const text = editor.document.lineAt(line).text.trimStart();
    if (/^(```|~~~)/.test(text)) fenceCount += 1;
  }
  return fenceCount % 2 === 1 ? 'markdownCode' : 'markdownText';
}

function detectConservativeCodeSyntax(
  editor: vscode.TextEditor,
  languageId: string,
): EditorSyntax {
  const line = editor.document.lineAt(editor.selection.active.line).text;
  const beforeCursor = line.slice(0, editor.selection.active.character);
  if (/^\s*\/\//.test(beforeCursor)) return 'comment';
  if (
    /^\s*#/.test(beforeCursor) &&
    ['python', 'shellscript', 'ruby'].includes(languageId)
  ) {
    return 'comment';
  }
  if (beforeCursor.lastIndexOf('/*') > beforeCursor.lastIndexOf('*/')) {
    return 'comment';
  }
  return 'code';
}
