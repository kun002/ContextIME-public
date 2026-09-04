export interface LanguageDefinition {
  wasmName: string;
  aliases?: readonly string[];
}

const DEFINITIONS: Readonly<Record<string, LanguageDefinition>> = {
  javascript: { wasmName: 'javascript' },
  javascriptreact: { wasmName: 'javascript' },
  typescript: { wasmName: 'typescript' },
  typescriptreact: { wasmName: 'tsx' },
  csharp: { wasmName: 'c-sharp', aliases: ['c_sharp'] },
  cpp: { wasmName: 'cpp' },
  c: { wasmName: 'c' },
  python: { wasmName: 'python' },
  go: { wasmName: 'go' },
  rust: { wasmName: 'rust' },
  java: { wasmName: 'java' },
  ruby: { wasmName: 'ruby' },
  php: { wasmName: 'php' },
  css: { wasmName: 'css' },
  shellscript: { wasmName: 'bash' },
  powershell: { wasmName: 'powershell' },
};

export function getLanguageDefinition(languageId: string): LanguageDefinition | null {
  return DEFINITIONS[languageId.toLowerCase()] ?? null;
}

export function getSupportedLanguageIds(): readonly string[] {
  return Object.keys(DEFINITIONS);
}

export function classifyNodeType(nodeType: string): 'comment' | 'string' | null {
  const normalized = nodeType.toLowerCase();
  if (normalized.includes('comment')) return 'comment';

  if (
    normalized.includes('string') ||
    normalized === 'character_literal' ||
    normalized === 'char_literal' ||
    normalized === 'heredoc_body' ||
    normalized === 'regex'
  ) {
    return 'string';
  }
  return null;
}
