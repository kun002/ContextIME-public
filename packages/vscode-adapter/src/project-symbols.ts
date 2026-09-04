import { createHash } from 'node:crypto';
import { posix, win32 } from 'node:path';

export const OPAQUE_PROJECT_ID_LENGTH = 32;
export const MAX_PROJECT_SYMBOL_BYTES = 128;
export const MAX_PROJECT_SYMBOLS_PER_DOCUMENT = 256;

const PROJECT_ID_DOMAIN = 'ContextIME.Project.v1\0';

export type ProjectSymbolType =
  | 'class'
  | 'method'
  | 'property'
  | 'enum'
  | 'namespace'
  | 'file'
  | 'directory'
  | 'asset'
  | 'shader'
  | 'term';

export type ProjectSymbolSource =
  | 'language_server'
  | 'compilation_database'
  | 'project_file'
  | 'file_system'
  | 'manual';

export interface ProjectSymbolEntry {
  symbol: string;
  symbolType: ProjectSymbolType;
  frequency: number;
  source: ProjectSymbolSource;
}

export interface ProjectSymbolNode {
  name: string;
  kind: number;
  children?: readonly ProjectSymbolNode[];
}

// SymbolKind numeric values are part of VS Code's stable extension API. Keep
// this pure module independent of `vscode` so its privacy and normalization
// boundary can run in ordinary Node tests.
export function projectSymbolTypeForKind(kind: number): ProjectSymbolType | null {
  switch (kind) {
    case 0: return 'file';
    case 1:
    case 2:
    case 3: return 'namespace';
    case 4:
    case 10:
    case 18:
    case 22: return 'class';
    case 5:
    case 8:
    case 11: return 'method';
    case 6:
    case 7:
    case 19: return 'property';
    case 9:
    case 21: return 'enum';
    default: return null;
  }
}

export function createOpaqueProjectId(
  workspacePath: string,
  platform: NodeJS.Platform = process.platform,
): string {
  if (!workspacePath || workspacePath.includes('\0')) {
    throw new Error('workspace identity is empty or malformed');
  }
  const normalized = platform === 'win32'
    ? normalizeWindowsWorkspacePath(workspacePath)
    : posix.normalize(workspacePath);
  return createHash('sha256')
    .update(PROJECT_ID_DOMAIN, 'utf8')
    .update(platform === 'win32' ? 'win32\0' : 'posix\0', 'utf8')
    .update(normalized, 'utf8')
    .digest('hex')
    .slice(0, OPAQUE_PROJECT_ID_LENGTH);
}

export function isOpaqueProjectId(value: string): boolean {
  return /^[0-9a-f]{32}$/.test(value);
}

export function isValidProjectSymbol(value: string): boolean {
  if (!value || Buffer.byteLength(value, 'utf8') > MAX_PROJECT_SYMBOL_BYTES ||
      hasUnpairedSurrogate(value)) {
    return false;
  }
  for (const character of value) {
    const codePoint = character.codePointAt(0) ?? 0;
    if (codePoint < 0x20 || codePoint === 0x7f ||
        character === '\\' || character === '/' || character === ':') {
      return false;
    }
  }
  return true;
}

export function collectProjectSymbols(
  nodes: readonly ProjectSymbolNode[],
  maximum = MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
): ProjectSymbolEntry[] {
  const limit = Math.max(0, Math.min(
    MAX_PROJECT_SYMBOLS_PER_DOCUMENT,
    Math.floor(maximum),
  ));
  const collected = new Map<string, ProjectSymbolEntry>();
  const stack = [...nodes].reverse();
  while (stack.length > 0 && collected.size < limit) {
    const node = stack.pop();
    if (!node) break;
    if (node.children) {
      for (let index = node.children.length - 1; index >= 0; index -= 1) {
        stack.push(node.children[index]);
      }
    }
    const symbolType = projectSymbolTypeForKind(node.kind);
    if (!symbolType || !isValidProjectSymbol(node.name)) continue;
    const key = `${symbolType}\0${node.name}`;
    const existing = collected.get(key);
    if (existing) {
      existing.frequency += 1;
    } else {
      collected.set(key, {
        symbol: node.name,
        symbolType,
        frequency: 1,
        source: 'language_server',
      });
    }
  }
  return [...collected.values()];
}

function normalizeWindowsWorkspacePath(value: string): string {
  let normalized = win32.normalize(value).replaceAll('/', '\\').toLowerCase();
  const root = win32.parse(normalized).root;
  while (normalized.length > root.length && normalized.endsWith('\\')) {
    normalized = normalized.slice(0, -1);
  }
  return normalized;
}

function hasUnpairedSurrogate(value: string): boolean {
  for (let index = 0; index < value.length; index += 1) {
    const unit = value.charCodeAt(index);
    if (unit >= 0xd800 && unit <= 0xdbff) {
      if (index + 1 >= value.length) return true;
      const next = value.charCodeAt(index + 1);
      if (next < 0xdc00 || next > 0xdfff) return true;
      index += 1;
    } else if (unit >= 0xdc00 && unit <= 0xdfff) {
      return true;
    }
  }
  return false;
}
