[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot,

  [string]$SchemaPath = (Join-Path $PSScriptRoot '..\data\contextime_developer.schema.yaml'),

  [string]$LuaPath = (Join-Path $PSScriptRoot '..\data\rime.lua')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$schemaSource = (Resolve-Path $SchemaPath).Path
$luaSource = (Resolve-Path $LuaPath).Path
$dataRoot = Join-Path $root 'output\data'
$developerSchemaPath = Join-Path $dataRoot 'contextime_developer.schema.yaml'
$sharedLuaPath = Join-Path $dataRoot 'rime.lua'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($required in @(
  $developerSchemaPath,
  $sharedLuaPath,
  $schemaSource,
  $luaSource
)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required 0.5.0 data input was not found: $required"
  }
}

$previousSchema = [System.IO.File]::ReadAllText($developerSchemaPath)
if (-not $previousSchema.Contains('version: "0.2.3"') -or
    $previousSchema.Contains('lua_translator@contextime_project_translator')) {
  throw 'The immutable ContextIME 0.2.3 data layer was not applied first.'
}

$schema = [System.IO.File]::ReadAllText($schemaSource).Replace("`r`n", "`n").Replace("`r", "`n")
foreach ($requiredLiteral in @(
  '__include: luna_pinyin_simp.schema:/'
  'schema_id: contextime_developer'
  'version: "0.5.0"'
  'lua_processor@contextime_command_processor'
  'lua_translator@contextime_command_translator'
  'lua_translator@contextime_project_translator'
  '- contextime_developer.custom:/patch?'
)) {
  if (-not $schema.Contains($requiredLiteral)) {
    throw "ContextIME 0.5.0 developer schema audit failed: $requiredLiteral"
  }
}

$lua = [System.IO.File]::ReadAllText($luaSource).Replace("`r`n", "`n").Replace("`r", "`n")
foreach ($requiredLiteral in @(
  'function contextime_command_processor(key, env)'
  'function contextime_command_translator(input, segment, env)'
  'function contextime_project_translator(input, segment, env)'
  'env.engine.context:get_property("contextime_project_candidates")'
  'candidate.quality = 10000'
  '〔项目·'
)) {
  if (-not $lua.Contains($requiredLiteral)) {
    throw "ContextIME 0.5.0 Lua candidate audit failed: $requiredLiteral"
  }
}

[System.IO.File]::WriteAllText($developerSchemaPath, $schema, $utf8NoBom)
[System.IO.File]::WriteAllText($sharedLuaPath, $lua, $utf8NoBom)

[PSCustomObject]@{
  version = '0.5.0-preview'
  defaultSchema = 'contextime_developer'
  inheritedSchema = 'luna_pinyin_simp'
  projectTranslator = 'lua_translator@contextime_project_translator'
  projectProperty = 'contextime_project_candidates'
  candidateSourceLabel = '〔项目·<symbol type>〕'
  inputPathIo = $false
  fallback = 'Empty or malformed property yields no project candidates and leaves ordinary pinyin translators active.'
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.5.0-data-report.json' -Encoding utf8

Get-Content 'contextime-0.5.0-data-report.json'
