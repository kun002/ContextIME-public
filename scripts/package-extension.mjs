import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const sourceManifestPath = join(root, 'packages', 'vscode-adapter', 'package.json');
const sourceManifest = JSON.parse(readFileSync(sourceManifestPath, 'utf8'));
const stage = join(root, 'build', 'vsix');
const artifacts = join(root, 'artifacts');
const version = sourceManifest.version;
const packageName = `context-ime-${version}-win32-x64.vsix`;
const output = join(artifacts, packageName);

rmSync(stage, { recursive: true, force: true });
mkdirSync(join(stage, 'dist'), { recursive: true });
mkdirSync(artifacts, { recursive: true });

execFileSync(process.execPath, [join(root, 'node_modules', 'typescript', 'bin', 'tsc'), '-b'], {
  cwd: root,
  stdio: 'inherit',
});

await build({
  entryPoints: [join(root, 'packages', 'vscode-adapter', 'dist', 'extension.js')],
  outfile: join(stage, 'dist', 'extension.js'),
  bundle: true,
  platform: 'node',
  format: 'cjs',
  target: 'node20',
  external: ['vscode', '@vscode/tree-sitter-wasm'],
  sourcemap: false,
  minify: false,
  logLevel: 'info',
});

const bundlePath = join(stage, 'dist', 'extension.js');
const bundleSource = readFileSync(bundlePath, 'utf8');
const forbiddenBundleMarkers = JSON.parse(readFileSync(
  join(root, 'scripts', 'vsix-forbidden-markers.json'),
  'utf8',
));
for (const marker of forbiddenBundleMarkers) {
  if (bundleSource.includes(marker)) {
    throw new Error(`VSIX bundle contains forbidden controller capability: ${marker}`);
  }
}
const allowedExternalModules = new Set(['vscode', '@vscode/tree-sitter-wasm']);
for (const match of bundleSource.matchAll(/require\(["']([^"']+)["']\)/g)) {
  const moduleName = match[1];
  if (!moduleName.startsWith('node:') && !allowedExternalModules.has(moduleName)) {
    throw new Error(`VSIX bundle contains unexpected external module: ${moduleName}`);
  }
}

const manifest = {
  ...sourceManifest,
  name: 'context-ime',
  publisher: 'kun002',
  version,
  private: false,
  main: './dist/extension.js',
  categories: ['Other', 'Programming Languages'],
  keywords: ['ime', 'chinese', 'input-method', 'tree-sitter', 'context'],
  repository: {
    type: 'git',
    url: 'https://github.com/kun002/ContextIME-public.git',
  },
  bugs: {
    url: 'https://github.com/kun002/ContextIME-public/issues',
  },
  extensionKind: ['ui'],
  capabilities: {
    untrustedWorkspaces: { supported: true },
  },
  files: [
    'dist/**',
    'docs/**',
    'node_modules/@vscode/tree-sitter-wasm/**',
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
  ],
  scripts: undefined,
  devDependencies: undefined,
  dependencies: {
    '@vscode/tree-sitter-wasm': '0.3.1',
  },
};
writeFileSync(join(stage, 'package.json'), `${JSON.stringify(manifest, null, 2)}\n`);

for (const file of ['README.md', 'LICENSE']) {
  copyFileSync(join(root, file), join(stage, file));
}
copyFileSync(
  join(root, 'packages', 'vscode-adapter', 'THIRD_PARTY_NOTICES.md'),
  join(stage, 'THIRD_PARTY_NOTICES.md'),
);
mkdirSync(join(stage, 'docs'), { recursive: true });
for (const file of [
  'syntax-runtime.md',
  'editor-context-protocol.md',
  'vscode-adapter.md',
  'project-dictionary.md',
  'project-dictionary-protocol.md',
  'project-dictionary-candidate-bridge.md',
]) {
  copyFileSync(join(root, 'docs', file), join(stage, 'docs', file));
}

execFileSync('npm', ['install', '--omit=dev', '--ignore-scripts', '--no-package-lock'], {
  cwd: stage,
  stdio: 'inherit',
  shell: process.platform === 'win32',
});

const required = [
  'dist/extension.js',
  'node_modules/@vscode/tree-sitter-wasm/wasm/tree-sitter.wasm',
  'node_modules/@vscode/tree-sitter-wasm/wasm/tree-sitter-typescript.wasm',
  'node_modules/@vscode/tree-sitter-wasm/wasm/tree-sitter-c-sharp.wasm',
  'README.md',
  'LICENSE',
  'THIRD_PARTY_NOTICES.md',
  'docs/project-dictionary.md',
  'docs/project-dictionary-protocol.md',
  'docs/project-dictionary-candidate-bridge.md',
];
for (const relative of required) {
  if (!existsSync(join(stage, relative))) {
    throw new Error(`VSIX staging is missing required file: ${relative}`);
  }
}

execFileSync(
  process.execPath,
  [
    join(root, 'node_modules', '@vscode', 'vsce', 'vsce'),
    'package',
    '--target',
    'win32-x64',
    '--out',
    output,
  ],
  {
    cwd: stage,
    stdio: 'inherit',
    env: { ...process.env, VSCE_STORE: 'file' },
  },
);

if (!existsSync(output)) throw new Error(`VSIX was not created: ${output}`);
console.log(`Created ${output}`);
