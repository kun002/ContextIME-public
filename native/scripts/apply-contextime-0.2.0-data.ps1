[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot,

  [string]$SchemaPath = (Join-Path $PSScriptRoot '..\data\contextime_developer-0.2.0.schema.yaml')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$schemaSource = (Resolve-Path $SchemaPath).Path
$dataRoot = Join-Path $root 'output\data'
$defaultPath = Join-Path $dataRoot 'default.yaml'
$simplifiedSchemaPath = Join-Path $dataRoot 'luna_pinyin_simp.schema.yaml'
$developerSchemaPath = Join-Path $dataRoot 'contextime_developer.schema.yaml'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($required in @($defaultPath, $simplifiedSchemaPath, $schemaSource)) {
  if (-not (Test-Path $required -PathType Leaf)) {
    throw "Required generated Rime data was not found: $required"
  }
}

$schema = [System.IO.File]::ReadAllText($schemaSource).Replace("`r`n", "`n").Replace("`r", "`n")
foreach ($requiredLiteral in @(
  '__include: luna_pinyin_simp.schema:/'
  'schema_id: contextime_developer'
  'prism: contextime_developer'
  '- contextime_developer.custom:/patch?'
  "([A-Z][-_+.'0-9A-Za-z]*|[a-z][0-9a-z]*[A-Z][0-9A-Za-z]*|[a-z][0-9A-Za-z]*_[0-9A-Za-z_-]*)`$"
)) {
  if (-not $schema.Contains($requiredLiteral)) {
    throw "Developer schema audit failed: $requiredLiteral"
  }
}
[System.IO.File]::WriteAllText($developerSchemaPath, $schema, $utf8NoBom)

$default = [System.IO.File]::ReadAllText($defaultPath).Replace("`r`n", "`n").Replace("`r", "`n")
$simplifiedEntry = '  - schema: luna_pinyin_simp'
$developerEntry = '  - schema: contextime_developer'
if ([regex]::Matches($default, "(?m)^$([regex]::Escape($simplifiedEntry))$").Count -ne 1) {
  throw 'Expected exactly one luna_pinyin_simp entry after applying the 0.1.3 data layer.'
}
if ([regex]::IsMatch($default, "(?m)^$([regex]::Escape($developerEntry))$")) {
  throw 'contextime_developer is unexpectedly already present in the generated schema list.'
}
$default = $default.Replace($simplifiedEntry, $developerEntry)
[System.IO.File]::WriteAllText($defaultPath, $default, $utf8NoBom)

$verifiedDefault = [System.IO.File]::ReadAllText($defaultPath)
if (-not [regex]::IsMatch($verifiedDefault, "(?ms)^schema_list:\s*\r?\n\s*- schema: contextime_developer(?:\r?\n|$)")) {
  throw 'contextime_developer is not the first generated schema.'
}

[PSCustomObject]@{
  version = '0.2.0-preview'
  defaultSchema = 'contextime_developer'
  inheritedSchema = 'luna_pinyin_simp'
  developerSchemaBytes = (Get-Item $developerSchemaPath).Length
  recognizedTechnicalIdentifiers = @('GameObject', 'Vector3.Lerp', 'playerController', 'player_controller')
  userOverride = '%APPDATA%\ContextIME\contextime_developer.custom.yaml'
  migration = 'Installer redeploys shared data while preserving the independent ContextIME user directory.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.0-data-report.json' -Encoding utf8

Get-Content 'contextime-0.2.0-data-report.json'
