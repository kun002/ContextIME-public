[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$dataRoot = Join-Path $root 'output\data'
$defaultPath = Join-Path $dataRoot 'default.yaml'
$simplifiedSchemaPath = Join-Path $dataRoot 'luna_pinyin_simp.schema.yaml'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

foreach ($required in @($defaultPath, $simplifiedSchemaPath)) {
  if (-not (Test-Path $required -PathType Leaf)) {
    throw "Required generated Rime data was not found: $required"
  }
}

$default = [System.IO.File]::ReadAllText($defaultPath).Replace("`r`n", "`n").Replace("`r", "`n")
$traditionalEntry = '  - schema: luna_pinyin'
$simplifiedEntry = '  - schema: luna_pinyin_simp'
$quickEntry = '  - schema: quick5'

if ([regex]::Matches($default, "(?m)^$([regex]::Escape($traditionalEntry))$").Count -ne 1) {
  throw 'Expected exactly one luna_pinyin entry in the generated default schema list.'
}
if ([regex]::Matches($default, "(?m)^$([regex]::Escape($quickEntry))$").Count -ne 1) {
  throw 'Expected exactly one quick5 entry in the generated default schema list.'
}
if ([regex]::IsMatch($default, "(?m)^$([regex]::Escape($simplifiedEntry))$")) {
  throw 'luna_pinyin_simp is unexpectedly already present in the generated default schema list.'
}

$default = $default.Replace($traditionalEntry, $simplifiedEntry)
$default = $default.Replace("$quickEntry`n", '')
[System.IO.File]::WriteAllText($defaultPath, $default, $utf8NoBom)

$verified = [System.IO.File]::ReadAllText($defaultPath)
if (-not [regex]::IsMatch($verified, "(?ms)^schema_list:\s*\r?\n\s*- schema: luna_pinyin_simp(?:\r?\n|$)")) {
  throw 'luna_pinyin_simp is not the first generated schema.'
}
if ([regex]::IsMatch($verified, '(?m)^\s*- schema: quick5\s*$')) {
  throw 'The unavailable quick5 schema remains in generated data.'
}

[PSCustomObject]@{
  version = '0.1.3-preview'
  defaultSchema = 'luna_pinyin_simp'
  removedUnavailableSchema = 'quick5'
  simplifiedSchemaBytes = (Get-Item $simplifiedSchemaPath).Length
  migration = 'Installer redeploys shared data while preserving the independent ContextIME user directory.'
} | ConvertTo-Json | Set-Content 'contextime-0.1.3-data-report.json' -Encoding utf8

Get-Content 'contextime-0.1.3-data-report.json'
