import * as vscode from 'vscode';
import { VSCodeContextProvider } from './context-provider';
import { VSCodeContextReporter } from './context-reporter';
import { ContextServiceEditorClient } from './named-pipe-client';
import { ProjectIndexerClient } from './project-indexer-client';
import { VSCodeProjectSymbolReporter } from './project-symbol-reporter';
import { VSCodeProjectSymbolSource } from './project-symbol-source';

let reporter: VSCodeContextReporter | null = null;
let projectReporter: VSCodeProjectSymbolReporter | null = null;

export function activate(context: vscode.ExtensionContext): void {
  const output = vscode.window.createOutputChannel('ContextIME Adapter');
  const provider = new VSCodeContextProvider();
  const client = new ContextServiceEditorClient();
  reporter = new VSCodeContextReporter(provider, client, output);
  projectReporter = new VSCodeProjectSymbolReporter(
    new VSCodeProjectSymbolSource(),
    new ProjectIndexerClient(),
    output,
  );
  context.subscriptions.push(output, reporter, projectReporter);
  reporter.start();
  projectReporter.start();

  context.subscriptions.push(
    vscode.commands.registerCommand('context-ime.reportNow', async () => {
      await reporter?.reportNow();
    }),
    vscode.commands.registerCommand('context-ime.adapterStatus', () => {
      const lines = [
        ...(reporter?.diagnostics() ?? ['adapter is not active']),
        ...(projectReporter?.diagnostics() ?? []),
      ];
      void vscode.window.showInformationMessage(lines.join('\n'), { modal: true });
    }),
    vscode.commands.registerCommand(
      'context-ime.refreshProjectDictionary',
      async () => {
        await projectReporter?.reportNow();
      },
    ),
  );
  output.appendLine(
    'ContextIME report-only adapter activated; Context Engine remains the only decision owner.',
  );
}

export function deactivate(): void {
  reporter?.dispose();
  reporter = null;
  projectReporter?.dispose();
  projectReporter = null;
}
