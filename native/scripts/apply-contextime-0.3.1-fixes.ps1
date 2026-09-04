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

# Keep the proven 0.3.0 Context Service lifecycle layer unchanged and publish
# the candidate-visibility recovery as a real, independently upgradable build.
Replace-LiteralRequired 'output/install.nsi' '0.3.0 Preview' '0.3.1 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.3.0-preview-installer.exe' 'contextime-0.3.1-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.3.0-preview' '0.3.1-preview' 2
Replace-LiteralRequired 'output/install.nsi' '0.3.0.0' '0.3.1.0'
Replace-LiteralRequired 'output/README.txt' '0.3.0 Preview' '0.3.1 Preview'

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($required in @(
  'ContextIME 0.3.1 Preview',
  'contextime-0.3.1-preview-installer.exe',
  'File "contextime-context-service.exe"',
  'ContextIMEContextService',
  'contextime-context-service.exe" --quit',
  'Exec "$INSTDIR\contextime-context-service.exe"'
)) {
  if (-not $installer.Contains($required)) {
    throw "ContextIME 0.3.1 installer audit missing: $required"
  }
}
if ([regex]::Matches($installer, 'ContextIMEContextService').Count -ne 3) {
  throw 'Context Service autorun must be written once and removed in upgrade and uninstall paths.'
}
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8WithBom)

[PSCustomObject]@{
  version = '0.3.1-preview'
  route = 'M3 terminal candidate visibility recovery patch release'
  executable = 'contextime-context-service.exe'
  autorunValue = 'ContextIMEContextService'
  lifecycle = @('package', 'single-instance start', 'quit before upgrade', 'restart', 'quit before uninstall')
  runtimeFix = 'Candidate UI destruction clears the visibility state used by composition protection.'
  contextServiceOnKeyPath = $false
  windowsServiceManagerUsed = $false
  runtimeFallback = 'Context Service unavailable leaves ordinary Weasel/librime input active.'
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.3.1-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.3.1-fix-report.json'
