[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot,

  [string]$SchemaPath = (Join-Path $PSScriptRoot '..\data\contextime_developer-0.2.2.schema.yaml'),

  [string]$LuaPath = (Join-Path $PSScriptRoot '..\data\rime.lua')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$schemaSource = (Resolve-Path $SchemaPath).Path
$luaSource = (Resolve-Path $LuaPath).Path
$dataRoot = Join-Path $root 'output\data'
$defaultPath = Join-Path $dataRoot 'default.yaml'
$developerSchemaPath = Join-Path $dataRoot 'contextime_developer.schema.yaml'
$sharedLuaPath = Join-Path $dataRoot 'rime.lua'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($required in @($defaultPath, $developerSchemaPath, $schemaSource, $luaSource)) {
  if (-not (Test-Path $required -PathType Leaf)) {
    throw "Required 0.2.2 data input was not found: $required"
  }
}

$previousSchema = [System.IO.File]::ReadAllText($developerSchemaPath).Replace("`r`n", "`n").Replace("`r", "`n")
if (-not $previousSchema.Contains('version: "0.2.1"') -or
    -not $previousSchema.Contains('recognizer/patterns/uppercase')) {
  throw 'The immutable ContextIME 0.2.1 developer schema layer was not applied first.'
}

$schema = [System.IO.File]::ReadAllText($schemaSource).Replace("`r`n", "`n").Replace("`r", "`n")
$commandPattern = '^(git|npm|dotnet|cargo|cd)( .*)?$'
foreach ($requiredLiteral in @(
  '__include: luna_pinyin_simp.schema:/'
  'schema_id: contextime_developer'
  'prism: contextime_developer'
  'version: "0.2.2"'
  '- contextime_developer.custom:/patch?'
  'lua_processor@contextime_command_processor'
  'lua_translator@contextime_command_translator'
  'contextime_command:'
  'command_fragments:'
  'enabled: true'
  $commandPattern
)) {
  if (-not $schema.Contains($requiredLiteral)) {
    throw "ContextIME 0.2.2 developer schema audit failed: $requiredLiteral"
  }
}

$commandRoots = @('git', 'npm', 'dotnet', 'cargo', 'cd')
foreach ($rootName in $commandRoots) {
  if (-not [regex]::IsMatch("$rootName status", $commandPattern)) {
    throw "ContextIME 0.2.2 command pattern does not match configured root: $rootName"
  }
  if (-not $schema.Contains("      - $rootName")) {
    throw "ContextIME 0.2.2 command root is not packaged in schema config: $rootName"
  }
}

$recognizedCommandFragments = @(
  'git status'
  'npm run build'
  'dotnet test --filter ContextIME.Tests'
  'cargo build --release'
  'cd Assets/Textures'
  'git status && npm test'
)
$excludedCommandFragments = @('run', 'shi', 'status', 'make build', 'gitstatus')
foreach ($fixture in $recognizedCommandFragments) {
  if (-not [regex]::IsMatch($fixture, $commandPattern)) {
    throw "ContextIME 0.2.2 command recognizer does not match required fixture: $fixture"
  }
}
foreach ($fixture in $excludedCommandFragments) {
  if ([regex]::IsMatch($fixture, $commandPattern)) {
    throw "ContextIME 0.2.2 command recognizer exceeds its finite-root boundary: $fixture"
  }
}

$lua = [System.IO.File]::ReadAllText($luaSource).Replace("`r`n", "`n").Replace("`r", "`n")
foreach ($requiredLiteral in @(
  'contextime_load_command_config'
  'config:get_bool("contextime/command_fragments/enabled")'
  'config:get_list("contextime/command_fragments/roots")'
  'contextime_is_command_input'
  'function contextime_command_processor(key, env)'
  'function contextime_command_translator(input, segment, env)'
  'env.engine:commit_text(command)'
  'context:clear()'
  'context:pop_input(1)'
)) {
  if (-not $lua.Contains($requiredLiteral)) {
    throw "ContextIME 0.2.2 Lua audit failed: $requiredLiteral"
  }
}
if ($lua.Contains('contextime_command_mode')) {
  throw 'ContextIME command mode must not persist independently from the current composition.'
}

[System.IO.File]::WriteAllText($developerSchemaPath, $schema, $utf8NoBom)
[System.IO.File]::WriteAllText($sharedLuaPath, $lua, $utf8NoBom)

$default = [System.IO.File]::ReadAllText($defaultPath)
if (-not [regex]::IsMatch($default, '(?ms)^schema_list:\s*\r?\n\s*- schema: contextime_developer(?:\r?\n|$)')) {
  throw 'ContextIME developer schema is no longer the first generated schema.'
}

[PSCustomObject]@{
  version = '0.2.2-preview'
  defaultSchema = 'contextime_developer'
  inheritedSchema = 'luna_pinyin_simp'
  developerSchemaBytes = (Get-Item $developerSchemaPath).Length
  sharedLuaBytes = (Get-Item $sharedLuaPath).Length
  commandRoots = $commandRoots
  recognizedCommandFragments = $recognizedCommandFragments
  excludedCommandFragments = $excludedCommandFragments
  commitBehavior = 'Enter commits the command composition but is not forwarded to the target application.'
  cancellationBehavior = 'Escape clears the composition; command mode has no state outside the current composition.'
  userOverride = '%APPDATA%\ContextIME\contextime_developer.custom.yaml'
  migration = 'Installer redeploys shared data while preserving the independent ContextIME user directory.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.2-data-report.json' -Encoding utf8

Get-Content 'contextime-0.2.2-data-report.json'
