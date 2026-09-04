[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot,

  [string]$IdentityPath = (Join-Path $PSScriptRoot '..\contextime.identity.json'),

  [string]$ReportPath = (Join-Path (Get-Location) 'contextime-identity-report.json')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$identityFile = (Resolve-Path $IdentityPath).Path
$identity = Get-Content $identityFile -Raw | ConvertFrom-Json
$utf8 = [System.Text.UTF8Encoding]::new($false)

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Read-UpstreamText {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  return [System.IO.File]::ReadAllText((Resolve-UpstreamPath $RelativePath))
}

function Write-UpstreamText {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Content
  )
  [System.IO.File]::WriteAllText((Resolve-UpstreamPath $RelativePath), $Content, $utf8)
}

function Replace-Literal {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New,
    [int]$MinimumCount = 1
  )
  $content = Read-UpstreamText $RelativePath
  $count = [regex]::Matches($content, [regex]::Escape($Old)).Count
  if ($count -lt $MinimumCount) {
    throw "Expected at least $MinimumCount occurrence(s) in ${RelativePath}: $Old"
  }
  Write-UpstreamText $RelativePath ($content.Replace($Old, $New))
}

function Replace-RegexOnce {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [Parameter(Mandatory = $true)][string]$Replacement
  )
  $content = Read-UpstreamText $RelativePath
  $matches = [regex]::Matches($content, $Pattern)
  if ($matches.Count -ne 1) {
    throw "Expected exactly one regex match in $RelativePath, found $($matches.Count): $Pattern"
  }
  $updated = [regex]::Replace(
    $content,
    $Pattern,
    [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $Replacement },
    1
  )
  Write-UpstreamText $RelativePath $updated
}

function Format-GuidBlock {
  param(
    [Parameter(Mandatory = $true)][string]$Guid,
    [Parameter(Mandatory = $true)][string]$Declaration
  )
  $upper = $Guid.Trim('{}').ToUpperInvariant()
  $parts = $upper.Split('-')
  if ($parts.Count -ne 5) { throw "Invalid GUID: $Guid" }
  $tail = $parts[3] + $parts[4]
  $bytes = for ($i = 0; $i -lt $tail.Length; $i += 2) {
    '0x' + $tail.Substring($i, 2).ToLowerInvariant()
  }
  return @"
// {$upper}
$Declaration = {
    0x$($parts[0].ToLowerInvariant()),
    0x$($parts[1].ToLowerInvariant()),
    0x$($parts[2].ToLowerInvariant()),
    {$($bytes -join ', ')}};
"@.Trim()
}

function Set-GuidBlock {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Variable,
    [Parameter(Mandatory = $true)][string]$Guid,
    [switch]$Static
  )
  $declaration = if ($Static) { "static const GUID $Variable" } else { "const GUID $Variable" }
  $pattern = '(?ms)(?:// \{[0-9A-Fa-f-]{36}\}\s*)?(?:static )?const GUID ' +
    [regex]::Escape($Variable) + ' = \{.*?\}\};'
  Replace-RegexOnce $RelativePath $pattern (Format-GuidBlock $Guid $declaration)
}

function Assert-NotContains {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string[]]$Forbidden
  )
  $content = Read-UpstreamText $RelativePath
  foreach ($token in $Forbidden) {
    if ($content.Contains($token)) {
      throw "Identity isolation audit failed: $RelativePath still contains '$token'"
    }
  }
}

# Product constants, registry roots and user-visible input method name.
Replace-Literal 'include/WeaselConstants.h' '#define WEASEL_CODE_NAME "Weasel"' '#define WEASEL_CODE_NAME "ContextIME"'
Replace-Literal 'include/WeaselConstants.h' '#define WEASEL_REG_KEY L"Software\\Rime\\Weasel"' '#define WEASEL_REG_KEY L"Software\\ContextIME"'
Replace-Literal 'include/WeaselConstants.h' '#define RIME_REG_KEY L"Software\\Rime"' '#define RIME_REG_KEY L"Software\\ContextIME"'

Replace-Literal 'include/WeaselUtility.h' 'L"%TEMP%\\rime.weasel"' 'L"%TEMP%\\contextime"'
Replace-Literal 'include/WeaselUtility.h' 'L"Software\\Rime\\Weasel"' 'L"Software\\ContextIME"'
Replace-RegexOnce 'include/WeaselUtility.h' '(?ms)inline std::wstring get_weasel_ime_name\(\) \{.*?^\}' @"
inline std::wstring get_weasel_ime_name() {
  return L"ContextIME";
}
"@.Trim()

Replace-Literal 'RimeWithWeasel/WeaselUtility.cpp' 'L"Software\\Rime\\Weasel"' 'L"Software\\ContextIME"'
Replace-Literal 'RimeWithWeasel/WeaselUtility.cpp' 'L"%AppData%\\Rime"' 'L"%AppData%\\ContextIME"'
Replace-Literal 'RimeWithWeasel/RimeWithWeasel.cpp' '"rime.weasel"' '"contextime.ime"' 2
Replace-Literal 'WeaselDeployer/Configurator.cpp' '"rime.weasel"' '"contextime.ime"'
Replace-Literal 'WeaselServer/WeaselServerApp.cpp' '"Software\\Rime\\Weasel\\Updates"' '"Software\\ContextIME\\Updates"'

# IPC, service and process-level singleton isolation.
Replace-Literal 'include/WeaselIPC.h' 'L"WeaselIPCWindow_1.0"' 'L"ContextIMEIPCWindow_1.0"'
Replace-Literal 'include/WeaselIPC.h' 'L"WeaselNamedPipe"' 'L"ContextIMENamedPipe"'
Replace-Literal 'WeaselServer/WeaselService.h' 'L"WeaselInputService"' 'L"ContextIMEInputService"'
Replace-Literal 'WeaselDeployer/Configurator.cpp' 'L"WeaselDeployerMutex"' 'L"ContextIMEDeployerMutex"' 3
Replace-Literal 'WeaselDeployer/WeaselDeployer.cpp' 'L"WeaselDeployerExclusiveMutex"' 'L"ContextIMEDeployerExclusiveMutex"'

# Setup registry and private data directory.
Replace-Literal 'WeaselSetup/WeaselSetup.cpp' 'L"Software\\Rime\\Weasel"' 'L"Software\\ContextIME"'
Replace-Literal 'WeaselSetup/WeaselSetup.cpp' 'L"Software\\Rime\\weasel"' 'L"Software\\ContextIME"' 8
Replace-Literal 'WeaselSetup/WeaselSetup.cpp' 'L"Software\\Rime\\weasel\\Updates"' 'L"Software\\ContextIME\\Updates"' 2
Replace-Literal 'WeaselSetup/WeaselSetup.cpp' 'L"%APPDATA%\\Rime"' 'L"%APPDATA%\\ContextIME"'

# TSF identity. Both the TSF DLL and the setup executable carry copies.
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'c_clsidTextService' $identity.guids.textService -Static
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'c_guidProfile' $identity.guids.profile -Static
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'c_guidLangBarItemButton' $identity.guids.languageBar -Static
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'c_guidDisplayAttributeInput' $identity.guids.displayAttribute -Static
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'GUID_LBI_INPUTMODE' $identity.guids.inputMode
Set-GuidBlock 'WeaselTSF/Globals.cpp' 'GUID_IME_MODE_PRESERVED_KEY' $identity.guids.preservedKey

Set-GuidBlock 'WeaselSetup/imesetup.cpp' 'c_clsidTextService' $identity.guids.textService -Static
Set-GuidBlock 'WeaselSetup/imesetup.cpp' 'c_guidProfile' $identity.guids.profile -Static
Replace-Literal 'WeaselSetup/imesetup.cpp' 'A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A' $identity.guids.textService 2
Replace-Literal 'WeaselSetup/imesetup.cpp' '3D02CAB6-2B8E-4781-BA20-1C9267529467' $identity.guids.profile 2

# Do not overwrite Weasel's system DLL/IME files. ContextIME uses its own basename.
Replace-Literal 'WeaselSetup/imesetup.cpp' 'std::wstring srcFileName = L"weasel";' 'std::wstring srcFileName = L"contextime";'
Replace-Literal 'WeaselSetup/imesetup.cpp' 'L"\\weasel" + ext' 'L"\\contextime" + ext' 4
Replace-Literal 'WeaselSetup/imesetup.cpp' 'L"weaselARM64X" + ext' 'L"contextimeARM64X" + ext'
Replace-Literal 'WeaselTSF/Register.cpp' 'L"weasel.ime"' 'L"contextime.ime"'
Replace-Literal 'WeaselTSF/Register.cpp' '"\\..\\weasel.dll"' '"\\..\\contextime.dll"'

# Remove remaining Chinese upstream branding from compiled UI resources without
# renaming C++ identifiers or executable filenames.
Get-ChildItem $root -Recurse -File -Include *.rc,*.cpp,*.h | ForEach-Object {
  $text = [System.IO.File]::ReadAllText($_.FullName)
  if ($text.Contains('小狼毫')) {
    [System.IO.File]::WriteAllText($_.FullName, $text.Replace('小狼毫', 'ContextIME'), $utf8)
  }
}

# Rebrand and isolate the NSIS installer while retaining internal executable
# filenames for the first preview. DLL/IME filenames are intentionally renamed.
$installerPath = 'output/install.nsi'
$installer = Read-UpstreamText $installerPath
$masks = [ordered]@{
  'WeaselServer.exe' = '@@INTERNAL_SERVER_EXE@@'
  'WeaselSetup.exe' = '@@INTERNAL_SETUP_EXE@@'
  'WeaselDeployer.exe' = '@@INTERNAL_DEPLOYER_EXE@@'
  'WeaselRoot' = '@@INTERNAL_ROOT_VALUE@@'
  'weasel.ico' = '@@INTERNAL_ICON@@'
}
foreach ($entry in $masks.GetEnumerator()) {
  $installer = $installer.Replace($entry.Key, $entry.Value)
}
$installer = $installer.Replace('小狼毫', 'ContextIME')
$installer = $installer.Replace('Weasel', 'ContextIME')
$installer = $installer.Replace('weasel', 'contextime')
foreach ($entry in $masks.GetEnumerator()) {
  $installer = $installer.Replace($entry.Value, $entry.Key)
}
$installer = $installer.Replace('Software\Rime\ContextIME', 'Software\ContextIME')
$installer = $installer.Replace('SOFTWARE\Rime\ContextIME', 'SOFTWARE\ContextIME')
$installer = $installer.Replace('DeleteRegKey HKLM SOFTWARE\Rime', 'DeleteRegKey HKLM SOFTWARE\ContextIME')
$installer = $installer.Replace('$PROGRAMFILES64\Rime', '$PROGRAMFILES64\ContextIME')
$installer = $installer.Replace('$PROGRAMFILES\Rime', '$PROGRAMFILES\ContextIME')
$installer = $installer.Replace('!define WEASEL_ROOT $INSTDIR\contextime-${WEASEL_VERSION}', '!define WEASEL_ROOT $INSTDIR\contextime-0.1.0-preview')
$installer = $installer.Replace('Name "ContextIME ${WEASEL_VERSION}"', 'Name "ContextIME 0.1.0 Preview"')
$installer = $installer.Replace('OutFile "archives\contextime-${PRODUCT_VERSION}-installer.exe"', 'OutFile "archives\contextime-0.1.0-preview-installer.exe"')
$installer = $installer.Replace('VIProductVersion "${WEASEL_VERSION}.${WEASEL_BUILD}"', 'VIProductVersion "0.1.0.0"')
$installer = $installer.Replace('WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayVersion" "${WEASEL_VERSION}.${WEASEL_BUILD}"', 'WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayVersion" "0.1.0-preview"')
$installer = $installer.Replace('WriteRegStr HKLM "${REG_UNINST_KEY}" "Publisher" "式恕堂"', 'WriteRegStr HKLM "${REG_UNINST_KEY}" "Publisher" "ContextIME Project"')
$installer = $installer.Replace('WriteRegStr HKLM "${REG_UNINST_KEY}" "URLInfoAbout" "https://rime.im/"', 'WriteRegStr HKLM "${REG_UNINST_KEY}" "URLInfoAbout" "https://github.com/kun002/ContextIME-public"')
$installer = $installer.Replace('WriteRegStr HKLM "${REG_UNINST_KEY}" "HelpLink" "https://rime.im/docs/"', 'WriteRegStr HKLM "${REG_UNINST_KEY}" "HelpLink" "https://github.com/kun002/ContextIME-public"')
$installer = $installer.Replace('StrCpy $R2 "/i"', 'StrCpy $R2 "/s"')
$updatePattern = '(?ms)  ; option CheckForUpdates\r?\n.*?  end:\r?\n'
$updateReplacement = @"
  ; ContextIME preview does not consume Weasel update channels.
  WriteRegStr HKCU "Software\ContextIME\Updates" "CheckForUpdates" "0"
"@
if ([regex]::Matches($installer, $updatePattern).Count -ne 1) {
  throw 'Could not isolate the installer update channel block.'
}
$installer = [regex]::Replace(
  $installer,
  $updatePattern,
  [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $updateReplacement },
  1
)
Write-UpstreamText $installerPath $installer

# Replace the packaged upstream readme with an explicit derivative notice.
$previewReadme = @"
ContextIME 0.1.0 Preview
========================

This is the first identity-isolated ContextIME native input method preview.
It is derived from Weasel 0.17.4 and librime 1.13.1.

Isolation in this preview:
- independent TSF service and profile GUIDs;
- independent registry and uninstall keys;
- independent Program Files and AppData directories;
- independent IPC window, named pipe, service and mutex names;
- independent contextime.dll/contextime.ime system filenames.

The internal helper executable names are still inherited from Weasel in this
preview. That does not make this installer the upstream Weasel product.

License: the Weasel-derived native frontend is distributed under GPL-3.0.
Source and project information: https://github.com/kun002/ContextIME-public
"@
[System.IO.File]::WriteAllText((Resolve-UpstreamPath 'output/README.txt'), $previewReadme, $utf8)

# Static isolation gates. These are product safety checks, not merely branding.
Assert-NotContains 'WeaselTSF/Globals.cpp' @(
  'A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A',
  '3D02CAB6-2B8E-4781-BA20-1C9267529467',
  '341F9E3A-B8AD-499D-936C-48701E329FB2',
  '2AC87E79-3260-4B32-9DEA-F8390976C20B'
)
Assert-NotContains 'WeaselSetup/imesetup.cpp' @(
  'A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A',
  '3D02CAB6-2B8E-4781-BA20-1C9267529467',
  'L"\\weasel" + ext',
  'L"weaselARM64X" + ext'
)
Assert-NotContains 'include/WeaselIPC.h' @('WeaselIPCWindow_1.0', 'WeaselNamedPipe')
Assert-NotContains 'RimeWithWeasel/WeaselUtility.cpp' @('Software\\Rime\\Weasel', '%AppData%\\Rime')
Assert-NotContains 'output/install.nsi' @(
  'Software\Rime\Weasel',
  'SOFTWARE\Rime\Weasel',
  'Uninstall\Weasel',
  'DeleteRegKey HKLM SOFTWARE\Rime',
  '$PROGRAMFILES64\Rime',
  '$PROGRAMFILES\Rime',
  'archives\weasel-'
)

$changed = @(git -C $root diff --name-only)
if ($LASTEXITCODE -ne 0) { throw 'Unable to collect the upstream identity patch diff.' }
$report = [ordered]@{
  product = $identity.product
  upstream = $identity.upstream
  guids = $identity.guids
  registry = $identity.registry
  directories = $identity.directories
  runtime = $identity.runtime
  changedFiles = $changed
  generatedAtUtc = [DateTime]::UtcNow.ToString('o')
}
$report | ConvertTo-Json -Depth 8 | Set-Content $ReportPath -Encoding utf8
Get-Content $ReportPath
