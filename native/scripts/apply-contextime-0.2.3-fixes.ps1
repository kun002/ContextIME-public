[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$utf8WithBom = [System.Text.UTF8Encoding]::new($true)

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Replace-LiteralRequired {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New
  )

  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n").Replace("`r", "`n")
  if (-not $content.Contains($Old)) {
    throw "Expected version token in ${RelativePath}: $Old"
  }
  $updated = $content.Replace($Old, $New)
  if ($updated.Contains($Old)) {
    throw "Version replacement left an old token in ${RelativePath}: $Old"
  }
  [System.IO.File]::WriteAllText($path, $updated, $utf8NoBom)
}

Replace-LiteralRequired 'output/install.nsi' '0.2.2 Preview' '0.2.3 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.2.2-preview-installer.exe' 'contextime-0.2.3-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.2.2-preview' '0.2.3-preview'
Replace-LiteralRequired 'output/install.nsi' '0.2.2.0' '0.2.3.0'
Replace-LiteralRequired 'output/README.txt' '0.2.2 Preview' '0.2.3 Preview'

# Upstream Weasel packages YAML, text and grammar data explicitly. ContextIME
# also requires its shared librime-lua entry point, so keep this file fatal:
# NSIS must fail instead of producing another installer without rime.lua.
Replace-LiteralRequired `
  'output/install.nsi' `
  "  File `"data\*.yaml`"`n  File /nonfatal `"data\*.txt`"" `
  "  File `"data\*.yaml`"`n  File `"data\rime.lua`"`n  File /nonfatal `"data\*.txt`""

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installerText = [System.IO.File]::ReadAllText($installerPath)
[System.IO.File]::WriteAllText($installerPath, $installerText, $utf8WithBom)

[PSCustomObject]@{
  version = '0.2.3-preview'
  route = 'native IME command fragment packaging hotfix'
  runtimeChanges = @()
  installerChange = 'Package data\rime.lua as a required shared-data file.'
  observedFailure = 'The 0.2.2 installer packaged the schema but omitted rime.lua, causing nil Lua processor and translator errors on the key path.'
  rollbackBoundary = 'Remove the 0.2.3 version, data and installer layers; the failed 0.2.2 artifact remains identifiable by its recorded hash.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.3-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.2.3-fix-report.json'
