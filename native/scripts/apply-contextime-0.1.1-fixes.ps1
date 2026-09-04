[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$utf16LeBom = [System.Text.UnicodeEncoding]::new($false, $true)

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Replace-LiteralExact {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New,
    [int]$ExpectedCount = 1
  )
  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path)
  $count = [regex]::Matches($content, [regex]::Escape($Old)).Count
  if ($count -ne $ExpectedCount) {
    throw "Expected exactly $ExpectedCount occurrence(s) in ${RelativePath}, found ${count}: $Old"
  }
  [System.IO.File]::WriteAllText($path, $content.Replace($Old, $New), $utf8NoBom)
}

# Fix TSF-side leftovers that still referenced the upstream Weasel runtime.
Replace-LiteralExact 'WeaselTSF/WeaselTSF.cpp' `
  'RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",' `
  'RegGetStringValue(HKEY_CURRENT_USER, L"Software\\ContextIME",'
Replace-LiteralExact 'WeaselTSF/WeaselTSF.cpp' `
  'CreateMutex(NULL, TRUE, L"WeaselDeployerExclusiveMutex")' `
  'CreateMutex(NULL, TRUE, L"ContextIMEDeployerExclusiveMutex")'

# Force the native baseline to start each new Rime session in Chinese mode.
$sessionOld = @'
  RimeSessionId session_id = (RimeSessionId)rime_api->create_session();
  if (m_global_ascii_mode) {
'@
$sessionNew = @'
  RimeSessionId session_id = (RimeSessionId)rime_api->create_session();
  if (!m_global_ascii_mode) {
    rime_api->set_option(session_id, "ascii_mode", false);
  }
  if (m_global_ascii_mode) {
'@
Replace-LiteralExact 'RimeWithWeasel/RimeWithWeasel.cpp' $sessionOld $sessionNew

# Complete system-file detection isolation for install/upgrade/uninstall checks.
Replace-LiteralExact 'WeaselSetup/imesetup.cpp' `
  '_wcsicmp(imeFile, L"weasel.ime") == 0' `
  '_wcsicmp(imeFile, L"contextime.ime") == 0' `
  2

# Bump the preview package and switch the installed server to Chinese mode.
$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
$installer = $installer.Replace('0.1.0 Preview', '0.1.1 Preview')
$installer = $installer.Replace('0.1.0-preview', '0.1.1-preview')
$installer = $installer.Replace('0.1.0.0', '0.1.1.0')
$installer = $installer.Replace('contextime-0.1.0-preview-installer.exe', 'contextime-0.1.1-preview-installer.exe')
$installer = $installer.Replace('/contextimedir', '/weaseldir')
$startPattern = '(?ms)  ; Start ContextIMEServer\r?\n  Exec "\$INSTDIR\\WeaselServer\.exe"\r?\n'
if ([regex]::Matches($installer, $startPattern).Count -ne 1) {
  throw 'Unable to patch the ContextIME server startup block.'
}
$startReplacement = @'
  ; Start ContextIMEServer and force a Chinese-mode baseline.
  Exec "$INSTDIR\WeaselServer.exe"
  Sleep 1500
  ExecWait '"$INSTDIR\WeaselServer.exe" /nascii'
'@
$installer = [regex]::Replace(
  $installer,
  $startPattern,
  [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $startReplacement },
  1
)
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8NoBom)

# These five compiled resource files are UTF-16LE. The identity pass rewrote
# branded resources as UTF-8, causing the installer/settings dialog mojibake.
$resourceRelativePaths = @(
  'WeaselDeployer/WeaselDeployer.rc',
  'WeaselIME/WeaselIME.rc',
  'WeaselServer/WeaselServer.rc',
  'WeaselSetup/WeaselSetup.rc',
  'WeaselTSF/WeaselTSF.rc'
)
$resourceFiles = foreach ($relative in $resourceRelativePaths) {
  Get-Item (Resolve-UpstreamPath $relative)
}
foreach ($file in $resourceFiles) {
  $text = [System.IO.File]::ReadAllText($file.FullName)
  [System.IO.File]::WriteAllText($file.FullName, $text, $utf16LeBom)
  $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
  if ($bytes.Length -lt 2 -or $bytes[0] -ne 0xFF -or $bytes[1] -ne 0xFE) {
    throw "UTF-16LE BOM verification failed: $($file.FullName)"
  }
}

$readmePath = Resolve-UpstreamPath 'output/README.txt'
$readme = [System.IO.File]::ReadAllText($readmePath)
$readme = $readme.Replace('0.1.0 Preview', '0.1.1 Preview')
[System.IO.File]::WriteAllText($readmePath, $readme, $utf8NoBom)

# Normalize generated text diffs to LF. This avoids CR characters being treated
# as trailing whitespace after the Windows patch scripts rewrite source files.
$changedFiles = @(git -C $root diff --name-only)
if ($LASTEXITCODE -ne 0) { throw 'Unable to enumerate generated patch files.' }
$textExtensions = @('.cpp', '.h', '.hpp', '.nsi', '.txt', '.yaml', '.yml', '.bat')
foreach ($relative in $changedFiles) {
  $path = Join-Path $root $relative
  if (-not (Test-Path $path -PathType Leaf)) { continue }
  if ([System.IO.Path]::GetExtension($path).ToLowerInvariant() -notin $textExtensions) { continue }
  $text = [System.IO.File]::ReadAllText($path)
  $text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
  [System.IO.File]::WriteAllText($path, $text, $utf8NoBom)
}

@{
  version = '0.1.1-preview'
  fixes = @(
    'restore UTF-16LE BOM for packaged Windows resource files',
    'isolate remaining TSF registry and mutex references',
    'force new native sessions to start in Chinese mode',
    'switch the installed server to Chinese mode after startup',
    'complete contextime.ime detection isolation',
    'normalize generated source diffs to LF'
  )
  resourceFilesWithUtf16LeBom = $resourceFiles.Count
  resourceFiles = $resourceRelativePaths
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.1.1-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.1.1-fix-report.json'
