[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot,

  [string]$SchemaPath = (Join-Path $PSScriptRoot '..\data\contextime_developer-0.2.1.schema.yaml')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$schemaSource = (Resolve-Path $SchemaPath).Path
$dataRoot = Join-Path $root 'output\data'
$defaultPath = Join-Path $dataRoot 'default.yaml'
$developerSchemaPath = Join-Path $dataRoot 'contextime_developer.schema.yaml'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($required in @($defaultPath, $developerSchemaPath, $schemaSource)) {
  if (-not (Test-Path $required -PathType Leaf)) {
    throw "Required 0.2.1 data input was not found: $required"
  }
}

$previousSchema = [System.IO.File]::ReadAllText($developerSchemaPath).Replace("`r`n", "`n").Replace("`r", "`n")
$previousPattern = "([A-Z][-_+.'0-9A-Za-z]*|[a-z][0-9a-z]*[A-Z][0-9A-Za-z]*|[a-z][0-9A-Za-z]*_[0-9A-Za-z_-]*)`$"
if (-not $previousSchema.Contains($previousPattern) -or -not $previousSchema.Contains('version: "0.2.0"')) {
  throw 'The immutable ContextIME 0.2.0 developer schema layer was not applied first.'
}

$schema = [System.IO.File]::ReadAllText($schemaSource).Replace("`r`n", "`n").Replace("`r", "`n")
$developerPattern = "([A-Z][-_+.'0-9A-Za-z]*|[a-z][0-9a-z]*[A-Z][0-9A-Za-z]*|[a-z][0-9A-Za-z]*_[0-9A-Za-z_-]*|^[A-Za-z]:(?:[\\/][0-9A-Za-z_.+\\/-]*)?|^(?:[0-9A-Za-z_+-][0-9A-Za-z_.+-]*[\\/])+[0-9A-Za-z_.+\\/-]*)`$"
$pathPattern = "(^[A-Za-z]:(?:[\\/][0-9A-Za-z_.+\\/-]*)?|^(?:[0-9A-Za-z_+-][0-9A-Za-z_.+-]*[\\/])+[0-9A-Za-z_.+\\/-]*)`$"
foreach ($requiredLiteral in @(
  '__include: luna_pinyin_simp.schema:/'
  'schema_id: contextime_developer'
  'prism: contextime_developer'
  'version: "0.2.1"'
  '- contextime_developer.custom:/patch?'
  $developerPattern
)) {
  if (-not $schema.Contains($requiredLiteral)) {
    throw "ContextIME 0.2.1 developer schema audit failed: $requiredLiteral"
  }
}

$recognizedTechnicalIdentifiers = @('GameObject', 'Vector3.Lerp', 'playerController', 'player_controller')
$recognizedPathFragments = @('Assets/Textures/UI', 'C:\Projects\ContextIME', 'src/components/App.tsx')
$excludedPathFixtures = @('.\src\main.cpp', '..\Assets\Textures', './src/main.cpp', '../src/main.cpp', '\\server\share', '/usr/local/bin', 'https://github.com', 'git status')
foreach ($fixture in $recognizedTechnicalIdentifiers) {
  if (-not [regex]::IsMatch($fixture, $developerPattern)) {
    throw "ContextIME 0.2.1 recognizer does not match required fixture: $fixture"
  }
}
foreach ($fixture in $recognizedPathFragments) {
  if (-not [regex]::IsMatch($fixture, $pathPattern)) {
    throw "ContextIME 0.2.1 path branch does not match required fixture: $fixture"
  }
}
foreach ($fixture in $excludedPathFixtures) {
  if ([regex]::IsMatch($fixture, $pathPattern)) {
    throw "ContextIME 0.2.1 path branch exceeds its documented boundary: $fixture"
  }
}
[System.IO.File]::WriteAllText($developerSchemaPath, $schema, $utf8NoBom)

$default = [System.IO.File]::ReadAllText($defaultPath)
if (-not [regex]::IsMatch($default, '(?ms)^schema_list:\s*\r?\n\s*- schema: contextime_developer(?:\r?\n|$)')) {
  throw 'ContextIME developer schema is no longer the first generated schema.'
}

[PSCustomObject]@{
  version = '0.2.1-preview'
  defaultSchema = 'contextime_developer'
  inheritedSchema = 'luna_pinyin_simp'
  developerSchemaBytes = (Get-Item $developerSchemaPath).Length
  recognizedTechnicalIdentifiers = $recognizedTechnicalIdentifiers
  recognizedPathFragments = $recognizedPathFragments
  excludedPathFixtures = $excludedPathFixtures
  userOverride = '%APPDATA%\ContextIME\contextime_developer.custom.yaml'
  migration = 'Installer redeploys shared data while preserving the independent ContextIME user directory.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.1-data-report.json' -Encoding utf8

Get-Content 'contextime-0.2.1-data-report.json'
