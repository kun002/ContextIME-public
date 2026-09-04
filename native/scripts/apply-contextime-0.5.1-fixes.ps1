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
  $count = [regex]::Matches($content, [regex]::Escape($Old)).Count
  if ($count -ne $ExpectedCount) {
    throw "Expected exactly $ExpectedCount occurrence(s) in ${RelativePath}, found ${count}: $Old"
  }
  [System.IO.File]::WriteAllText(
    $path,
    $content.Replace($Old, $New),
    $utf8NoBom
  )
}

# Keep the verified 0.5.0 candidate bridge unchanged. This version bump ships
# the bounded large-project background update and its cache invalidation gate
# as an independently upgradable host candidate.
Replace-LiteralRequired 'output/install.nsi' '0.5.0 Preview' '0.5.1 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.5.0-preview-installer.exe' 'contextime-0.5.1-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.5.0-preview' '0.5.1-preview' 2
Replace-LiteralRequired 'output/install.nsi' '0.5.0.0' '0.5.1.0'
Replace-LiteralRequired 'output/README.txt' '0.5.0 Preview' '0.5.1 Preview'

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($required in @(
  'ContextIME 0.5.1 Preview',
  'contextime-0.5.1-preview-installer.exe',
  'File "contextime-context-service.exe"',
  'ContextIMEContextService',
  'contextime-context-service.exe" --quit',
  'ExecWait ''"$INSTDIR\WeaselServer.exe" /nascii'''
)) {
  if (-not $installer.Contains($required)) {
    throw "ContextIME 0.5.1 installer audit missing: $required"
  }
}
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8WithBom)

$dictionary = [System.IO.File]::ReadAllText(
  (Resolve-UpstreamPath 'RimeWithWeasel/ContextIME/project_dictionary.cpp'))
foreach ($required in @(
  'TryLoadCached(project_id, snapshot)',
  'std::vector<ProjectDictionaryEntry> merged',
  'CacheSnapshot(persisted)',
  '*persisted_snapshot = std::move(persisted)'
)) {
  if (-not $dictionary.Contains($required)) {
    throw "Large-project dictionary audit missing: $required"
  }
}

[PSCustomObject]@{
  version = '0.5.1-preview'
  route = 'M5 large-project dictionary background update host candidate'
  maximumProjectEntries = 100000
  maximumUpsertBatch = 64
  cacheOwner = 'Project Indexer single background owner'
  cacheValidation = @('file existence', 'file size', 'last write time')
  merge = 'bounded incoming map plus linear merge with sorted immutable snapshot'
  activePublication = 'exact persisted snapshot; no immediate full dictionary reload'
  diskIoOnKeyPath = $false
  ipcOnKeyPath = $false
  nodeOnKeyPath = $false
  runtimeFallback = 'Store or project bridge failure removes project candidates; ordinary librime translators remain active.'
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.5.1-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.5.1-fix-report.json'
