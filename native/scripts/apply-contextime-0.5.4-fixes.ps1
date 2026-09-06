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
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Replace-LiteralRequired {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New,
    [int]$ExpectedCount = 1
  )

  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedOld = $Old.Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedNew = $New.Replace("`r`n", "`n").Replace("`r", "`n")
  $count = [regex]::Matches($content, [regex]::Escape($normalizedOld)).Count
  if ($count -ne $ExpectedCount) {
    throw "Expected exactly $ExpectedCount occurrence(s) in ${RelativePath}, found ${count}: $Old"
  }
  [System.IO.File]::WriteAllText(
    $path,
    $content.Replace($normalizedOld, $normalizedNew),
    $utf8NoBom
  )
}

Replace-LiteralRequired 'output/install.nsi' '0.5.3 Preview' '0.5.4 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.5.3-preview-installer.exe' 'contextime-0.5.4-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.5.3-preview' '0.5.4-preview' 2
Replace-LiteralRequired 'output/install.nsi' '0.5.3.0' '0.5.4.0'
Replace-LiteralRequired 'output/README.txt' '0.5.3 Preview' '0.5.4 Preview'

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($required in @(
  'ContextIME 0.5.4 Preview',
  'contextime-0.5.4-preview-installer.exe',
  'File "contextime-context-service.exe"',
  'File "contextime-project-dictionary-manager.exe"',
  'LNKFORPROJECTDICT',
  '"$INSTDIR\contextime-project-dictionary-manager.exe"',
  'ContextIMEContextService',
  'contextime-context-service.exe" --quit'
)) {
  if (-not $installer.Contains($required)) {
    throw "ContextIME 0.5.4 installer audit missing: $required"
  }
}
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8WithBom)

[PSCustomObject]@{
  version = '0.5.4-preview'
  route = 'User-approved manual terms in the project dictionary manager; terminal surfaces default to KEEP'
  terminalApplicationDefault = 'KEEP'
  integratedTerminalDefault = 'KEEP'
  executable = 'contextime-project-dictionary-manager.exe'
  transport = 'bounded CIPM frames on ContextIME.ProjectIndexer.v1'
  operations = @('list', 'view', 'enable', 'disable', 'remove', 'add_term', 'remove_entry')
  storeOwner = 'Context Service Project Indexer background thread'
  activeSnapshot = 'term upsert and single-entry removal republish the active snapshot when the lease is live'
  diskIoOnKeyPath = $false
  ipcOnKeyPath = $false
  nodeOnKeyPath = $false
  runtimeFallback = 'Terminal policy and project bridge failures leave ordinary librime pinyin input active.'
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.5.4-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.5.4-fix-report.json'
