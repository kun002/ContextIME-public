import assert from 'node:assert/strict';
import test from 'node:test';
import { SyntaxRuntime, SyntaxSnapshot, VSCodeWasmResolver } from '../src/index';

function snapshot(
  text: string,
  cursorOffset: number,
  patch: Partial<SyntaxSnapshot> = {},
): SyntaxSnapshot {
  return {
    documentId: 'file:///sample.ts',
    languageId: 'typescript',
    version: 1,
    text,
    cursorOffset,
    ...patch,
  };
}

test('resolves Microsoft parser and language WASM assets', () => {
  const resolver = new VSCodeWasmResolver();
  assert.match(resolver.resolveCoreWasm(), /tree-sitter\.wasm$/);
  assert.match(resolver.resolveLanguageWasm('typescript') ?? '', /tree-sitter-typescript\.wasm$/);
  assert.match(resolver.resolveLanguageWasm('csharp') ?? '', /tree-sitter-c-sharp\.wasm$/);
});

test('detects TypeScript code, comments and strings', async () => {
  const runtime = new SyntaxRuntime();
  const text = 'const value = "hello"; // 中文说明';

  assert.equal(await runtime.analyze(snapshot(text, text.indexOf('value') + 2)), 'code');
  assert.equal(await runtime.analyze(snapshot(text, text.indexOf('hello') + 2)), 'string');
  assert.equal(await runtime.analyze(snapshot(text, text.indexOf('说明') + 1)), 'comment');
  runtime.dispose();
});

test('detects C# comment and string contexts', async () => {
  const runtime = new SyntaxRuntime();
  const text = 'var name = "ContextIME"; // 中文注释';
  const base = { documentId: 'file:///sample.cs', languageId: 'csharp' };

  assert.equal(
    await runtime.analyze(snapshot(text, text.indexOf('ContextIME') + 3, base)),
    'string',
  );
  assert.equal(
    await runtime.analyze(snapshot(text, text.indexOf('注释') + 1, base)),
    'comment',
  );
  runtime.dispose();
});

test('reuses the cached tree for cursor-only movement', async () => {
  const runtime = new SyntaxRuntime();
  const text = 'const first = 1; const second = 2;';

  await runtime.analyze(snapshot(text, text.indexOf('first')));
  await runtime.analyze(snapshot(text, text.indexOf('second')));
  const diagnostics = runtime.getDiagnostics();
  assert.equal(diagnostics.parseCount, 1);
  assert.equal(diagnostics.cacheHitCount, 1);
  runtime.dispose();
});

test('incrementally updates a changed document', async () => {
  const runtime = new SyntaxRuntime();
  const first = 'const value = 1;';
  const second = 'const value = 1; // changed';

  assert.equal(await runtime.analyze(snapshot(first, first.length)), 'code');
  assert.equal(
    await runtime.analyze(snapshot(second, second.length, { version: 2 })),
    'comment',
  );
  const diagnostics = runtime.getDiagnostics();
  assert.equal(diagnostics.parseCount, 2);
  assert.equal(diagnostics.incrementalParseCount, 1);
  runtime.dispose();
});

test('incrementally updates Unicode text with UTF-16 coordinates', async () => {
  const runtime = new SyntaxRuntime();
  const first = 'const 名称 = 1;';
  const second = 'const 名称 = 1; // 中文说明';

  await runtime.analyze(snapshot(first, first.length));
  assert.equal(
    await runtime.analyze(snapshot(second, second.length, { version: 2 })),
    'comment',
  );
  assert.equal(runtime.getDiagnostics().incrementalParseCount, 1);
  runtime.dispose();
});

test('keeps document trees isolated across files', async () => {
  const runtime = new SyntaxRuntime();
  const code = 'const value = 1;';
  const comment = '// note';

  assert.equal(
    await runtime.analyze(snapshot(code, code.length, { documentId: 'file:///a.ts' })),
    'code',
  );
  assert.equal(
    await runtime.analyze(snapshot(comment, comment.length, { documentId: 'file:///b.ts' })),
    'comment',
  );
  assert.equal(runtime.getDiagnostics().cachedDocuments, 2);
  runtime.dispose();
});

test('uses VS Code UTF-16 columns when Unicode precedes the cursor', async () => {
  const runtime = new SyntaxRuntime();
  const text = 'const 名称 = 1; // 中文说明';
  assert.equal(await runtime.analyze(snapshot(text, text.length)), 'comment');
  runtime.dispose();
});

test('does not split surrogate pairs at cursor boundaries', async () => {
  const runtime = new SyntaxRuntime();
  const text = 'const icon = "😀"; // 注释';
  const insidePair = text.indexOf('😀') + 1;
  assert.equal(await runtime.analyze(snapshot(text, insidePair)), 'string');
  runtime.dispose();
});

test('returns unknown for unsupported languages without parsing', async () => {
  const runtime = new SyntaxRuntime();
  const result = await runtime.analyze(
    snapshot('Shader "Test" {}', 5, { languageId: 'shaderlab' }),
  );
  assert.equal(result, 'unknown');
  assert.deepEqual(runtime.getDiagnostics().unsupportedLanguages, ['shaderlab']);
  runtime.dispose();
});
