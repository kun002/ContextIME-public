import * as vscode from 'vscode';
import {
  collectProjectSymbols,
  createOpaqueProjectId,
  MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
  ProjectSymbolEntry,
  ProjectSymbolNode,
} from './project-symbols';

const SUPPORTED_LANGUAGES = new Set([
  'c',
  'cpp',
  'csharp',
  'javascript',
  'javascriptreact',
  'python',
  'typescript',
  'typescriptreact',
]);

export interface ProjectSymbolCapture {
  projectId: string;
  languageId: string;
  entries: ProjectSymbolEntry[];
}

export class VSCodeProjectSymbolSource {
  supports(document: vscode.TextDocument): boolean {
    return document.uri.scheme === 'file' &&
      SUPPORTED_LANGUAGES.has(document.languageId.toLowerCase()) &&
      vscode.workspace.getWorkspaceFolder(document.uri) !== undefined;
  }

  projectId(document: vscode.TextDocument): string | null {
    if (!this.supports(document)) return null;
    const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
    return workspaceFolder
      ? createOpaqueProjectId(workspaceFolder.uri.fsPath, 'win32')
      : null;
  }

  async capture(
    document: vscode.TextDocument,
    maximum = MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
  ): Promise<ProjectSymbolCapture | null> {
    const projectId = this.projectId(document);
    if (!projectId) return null;

    let symbols: Array<vscode.DocumentSymbol | vscode.SymbolInformation> | undefined;
    try {
      symbols = await vscode.commands.executeCommand<
        Array<vscode.DocumentSymbol | vscode.SymbolInformation> | undefined
      >('vscode.executeDocumentSymbolProvider', document.uri);
    } catch {
      return null;
    }
    if (!symbols) return null;

    // Only the stable name/kind/children fields cross this boundary. Location,
    // container, detail, URI, ranges, and source text are never retained.
    const nodes: readonly ProjectSymbolNode[] = symbols;
    return {
      projectId,
      languageId: document.languageId.toLowerCase(),
      entries: collectProjectSymbols(nodes, maximum),
    };
  }
}
