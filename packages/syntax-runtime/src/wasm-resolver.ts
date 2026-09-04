import { getLanguageDefinition } from './language-registry';
import { WasmResolver } from './types';

/** Resolves prebuilt WASM assets shipped by Microsoft's VS Code package. */
export class VSCodeWasmResolver implements WasmResolver {
  resolveCoreWasm(): string {
    return require.resolve('@vscode/tree-sitter-wasm/wasm/tree-sitter.wasm');
  }

  resolveLanguageWasm(languageId: string): string | null {
    const definition = getLanguageDefinition(languageId);
    if (!definition) return null;

    const candidates = [definition.wasmName, ...(definition.aliases ?? [])];
    for (const name of candidates) {
      try {
        return require.resolve(`@vscode/tree-sitter-wasm/wasm/tree-sitter-${name}.wasm`);
      } catch {
        // Try the next explicitly registered filename.
      }
    }
    return null;
  }
}
