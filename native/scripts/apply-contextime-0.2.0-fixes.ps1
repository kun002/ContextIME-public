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
  $count = [regex]::Matches($content, [regex]::Escape($Old)).Count
  if ($count -lt 1) {
    throw "Expected at least one occurrence in ${RelativePath}: $Old"
  }
  $updated = $content.Replace($Old, $New)
  if ($updated.Contains($Old)) {
    throw "Version replacement left an old token in ${RelativePath}: $Old"
  }
  [System.IO.File]::WriteAllText($path, $updated, $utf8NoBom)
}

# Keep the released 0.1.3 patch stack immutable. This new layer only advances
# installer identity for the first developer-schema preview.
Replace-LiteralRequired 'output/install.nsi' '0.1.3 Preview' '0.2.0 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.1.3-preview-installer.exe' 'contextime-0.2.0-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.1.3-preview' '0.2.0-preview'
Replace-LiteralRequired 'output/install.nsi' '0.1.3.0' '0.2.0.0'
Replace-LiteralRequired 'output/README.txt' '0.1.3 Preview' '0.2.0 Preview'

# Preserve the localized NSIS script encoding established by 0.1.3.
$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installerText = [System.IO.File]::ReadAllText($installerPath)
[System.IO.File]::WriteAllText($installerPath, $installerText, $utf8WithBom)

[PSCustomObject]@{
  version = '0.2.0-preview'
  route = 'native IME developer schema'
  runtimeChanges = @()
  dataChange = 'Package ContextIME-owned developer schema without changing TSF, IPC or librime hot-path code.'
  rollbackBoundary = 'Remove this version patch and the 0.2.0 data layer; the reproducible 0.1.3 package remains unchanged.'
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.2.0-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.2.0-fix-report.json'
