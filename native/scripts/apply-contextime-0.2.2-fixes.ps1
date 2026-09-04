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

# Apply after the immutable 0.2.1 layer. The upstream TSF and IPC patches stay
# unchanged; command fragments are added by the versioned Rime data layer.
Replace-LiteralRequired 'output/install.nsi' '0.2.1 Preview' '0.2.2 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.2.1-preview-installer.exe' 'contextime-0.2.2-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.2.1-preview' '0.2.2-preview'
Replace-LiteralRequired 'output/install.nsi' '0.2.1.0' '0.2.2.0'
Replace-LiteralRequired 'output/README.txt' '0.2.1 Preview' '0.2.2 Preview'

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installerText = [System.IO.File]::ReadAllText($installerPath)
[System.IO.File]::WriteAllText($installerPath, $installerText, $utf8WithBom)

[PSCustomObject]@{
  version = '0.2.2-preview'
  route = 'native IME command fragment recognition'
  runtimeChanges = @('In-process librime-lua processor and translator configured by the ContextIME developer schema.')
  dataChange = 'Package stateless command composition for a finite, user-overridable command-root set.'
  rollbackBoundary = 'Remove the 0.2.2 version and data layers; the 0.2.1 package remains reproducible from its immutable schema snapshot.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.2-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.2.2-fix-report.json'
