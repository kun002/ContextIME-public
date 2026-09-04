[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Replace-LiteralOnce {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New
  )

  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path)
  $count = [regex]::Matches($content, [regex]::Escape($Old)).Count
  if ($count -ne 1) {
    throw "Expected exactly one occurrence in ${RelativePath}, found ${count}: $Old"
  }
  $updated = $content.Replace($Old, $New).Replace("`r`n", "`n")
  [System.IO.File]::WriteAllText($path, $updated, $utf8NoBom)
}

# Weasel and ContextIME previously shared this process-wide mutex. When Weasel
# was already running, ContextIME's server returned immediately from Start(),
# leaving the TSF profile active but all keystrokes in pass-through English.
Replace-LiteralOnce 'WeaselIPCServer/WeaselServerImpl.cpp' `
  'std::wstring instanceName = L"(WEASEL)Furandōru-Sukāretto-";' `
  'std::wstring instanceName = L"(CONTEXTIME)NativeServer-";'

$serverPath = Resolve-UpstreamPath 'WeaselIPCServer/WeaselServerImpl.cpp'
$serverSource = [System.IO.File]::ReadAllText($serverPath)
if ($serverSource.Contains('(WEASEL)Furandōru-Sukāretto-')) {
  throw 'ContextIME server still contains the upstream Weasel singleton mutex.'
}
if (-not $serverSource.Contains('(CONTEXTIME)NativeServer-')) {
  throw 'ContextIME server singleton mutex was not installed.'
}

# Bump the package generated after the 0.1.1 fixes.
$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
$installer = $installer.Replace('0.1.1 Preview', '0.1.2 Preview')
$installer = $installer.Replace('0.1.1-preview', '0.1.2-preview')
$installer = $installer.Replace('0.1.1.0', '0.1.2.0')
$installer = $installer.Replace('contextime-0.1.1-preview-installer.exe', 'contextime-0.1.2-preview-installer.exe')
$installer = $installer.Replace("`r`n", "`n")
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8NoBom)

$readmePath = Resolve-UpstreamPath 'output/README.txt'
$readme = [System.IO.File]::ReadAllText($readmePath)
$readme = $readme.Replace('0.1.1 Preview', '0.1.2 Preview')
[System.IO.File]::WriteAllText($readmePath, $readme, $utf8NoBom)

[PSCustomObject]@{
  version = '0.1.2-preview'
  confirmedRootCause = 'ContextIME and Weasel shared the same server singleton mutex'
  serverMutex = '(CONTEXTIME)NativeServer-<username>'
  expectedRuntimeResult = 'Weasel and ContextIME servers can run concurrently'
} | ConvertTo-Json | Set-Content 'contextime-0.1.2-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.1.2-fix-report.json'
