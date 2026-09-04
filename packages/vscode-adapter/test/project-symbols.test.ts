import assert from 'node:assert/strict';
import test from 'node:test';
import {
  collectProjectSymbols,
  createOpaqueProjectId,
  isOpaqueProjectId,
  isValidProjectSymbol,
  MAX_PROJECT_SYMBOL_BYTES,
  projectSymbolTypeForKind,
} from '../src/project-symbols';

test('workspace paths become stable opaque project ids without exposing paths', () => {
  const first = createOpaqueProjectId('C:\\Users\\ExampleUser\\Game', 'win32');
  const equivalent = createOpaqueProjectId('c:/users/EXAMPLEUSER/Game\\', 'win32');
  const other = createOpaqueProjectId('C:\\Users\\ExampleUser\\Other', 'win32');

  assert.ok(isOpaqueProjectId(first));
  assert.equal(first, equivalent);
  assert.notEqual(first, other);
  assert.ok(!first.includes('users'));
  assert.throws(() => createOpaqueProjectId('', 'win32'));
  assert.throws(() => createOpaqueProjectId('bad\0path', 'win32'));
});

test('language-server kinds map only to approved project symbol fields', () => {
  assert.equal(projectSymbolTypeForKind(4), 'class');
  assert.equal(projectSymbolTypeForKind(11), 'method');
  assert.equal(projectSymbolTypeForKind(7), 'property');
  assert.equal(projectSymbolTypeForKind(9), 'enum');
  assert.equal(projectSymbolTypeForKind(2), 'namespace');
  assert.equal(projectSymbolTypeForKind(13), null);
  assert.equal(projectSymbolTypeForKind(14), null);
});

test('project symbol validation mirrors native privacy and size boundaries', () => {
  assert.equal(isValidProjectSymbol('PlayerController'), true);
  assert.equal(isValidProjectSymbol('玩家控制器'), true);
  assert.equal(isValidProjectSymbol('Assets/Secret.txt'), false);
  assert.equal(isValidProjectSymbol('C:\\secret'), false);
  assert.equal(isValidProjectSymbol('source\nline'), false);
  assert.equal(isValidProjectSymbol('\ud800'), false);
  assert.equal(isValidProjectSymbol('a'.repeat(MAX_PROJECT_SYMBOL_BYTES)), true);
  assert.equal(isValidProjectSymbol('a'.repeat(MAX_PROJECT_SYMBOL_BYTES + 1)), false);
});

test('document symbol trees are flattened, filtered, bounded, and deduplicated', () => {
  const entries = collectProjectSymbols([
    {
      name: 'Game.Player',
      kind: 2,
      children: [
        {
          name: 'PlayerController',
          kind: 4,
          children: [
            { name: 'SpawnPlayer', kind: 5 },
            { name: 'SpawnPlayer', kind: 5 },
            { name: 'ignoredVariable', kind: 13 },
            { name: 'Assets/Secret', kind: 6 },
          ],
        },
      ],
    },
  ]);

  assert.deepEqual(entries, [
    {
      symbol: 'Game.Player',
      symbolType: 'namespace',
      frequency: 1,
      source: 'language_server',
    },
    {
      symbol: 'PlayerController',
      symbolType: 'class',
      frequency: 1,
      source: 'language_server',
    },
    {
      symbol: 'SpawnPlayer',
      symbolType: 'method',
      frequency: 2,
      source: 'language_server',
    },
  ]);
  assert.equal(collectProjectSymbols([
    { name: 'One', kind: 4 },
    { name: 'Two', kind: 4 },
  ], 1).length, 1);
  assert.deepEqual(collectProjectSymbols([], 10), []);
});
