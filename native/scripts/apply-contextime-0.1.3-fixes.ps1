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
  $normalizedOld = $Old.Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedNew = $New.Replace("`r`n", "`n").Replace("`r", "`n")
  $count = [regex]::Matches($content, [regex]::Escape($normalizedOld)).Count
  if ($count -lt 1) {
    throw "Expected at least one occurrence in ${RelativePath}: $Old"
  }
  $updated = $content.Replace($normalizedOld, $normalizedNew)
  if ($updated.Contains($normalizedOld)) {
    throw "Version replacement left an old token in ${RelativePath}: $Old"
  }
  [System.IO.File]::WriteAllText($path, $updated, $utf8NoBom)
}

# Keep the patch stack explicit: 0.1.0 identity -> 0.1.1 runtime fixes ->
# 0.1.2 coexistence fix -> this 0.1.3 simplified-output release.
Replace-LiteralRequired 'output/install.nsi' '0.1.2 Preview' '0.1.3 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.1.2-preview-installer.exe' 'contextime-0.1.3-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.1.2-preview' '0.1.3-preview'
Replace-LiteralRequired 'output/install.nsi' '0.1.2.0' '0.1.3.0'
Replace-LiteralRequired 'output/README.txt' '0.1.2 Preview' '0.1.3 Preview'

# WeaselSetup is a 32-bit process on x64 Windows. The upstream uninstall path
# called the second regsvr32 /u before disabling WOW64 redirection, so it
# unregistered the 32-bit TSF DLL twice and left the 64-bit CLSID behind.
$wow64UnregisterOld = @'
  if (is_wow64()) {
    retval += func(imePath, false, true, false, false, silent);
    PVOID OldValue = NULL;
    if (Wow64DisableWow64FsRedirection(&OldValue) == FALSE) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRCANCELFSREDIRECT,
                            IDS_STR_UNINSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }

    if (is_arm64_machine()) {
'@
$wow64UnregisterNew = @'
  if (is_wow64()) {
    PVOID OldValue = NULL;
    if (Wow64DisableWow64FsRedirection(&OldValue) == FALSE) {
      MSG_NOT_SILENT_BY_IDS(silent, IDS_STR_ERRCANCELFSREDIRECT,
                            IDS_STR_UNINSTALL_FAILED, MB_ICONERROR | MB_OK);
      return 1;
    }
    retval += func(imePath, false, true, false, false, silent);

    if (is_arm64_machine()) {
'@
Replace-LiteralRequired 'WeaselSetup/imesetup.cpp' $wow64UnregisterOld $wow64UnregisterNew

# Remove ContextIME's own language-list entries directly. The upstream code
# gated this on Software\Rime\Weasel\Hant, which is neither owned nor written
# by ContextIME and therefore left the installed profile in Windows settings.
$layoutUninstallOld = @'
  const WCHAR KEY[] = L"Software\\Rime\\Weasel";
  HKEY hKey;
  LSTATUS ret = RegOpenKey(HKEY_CURRENT_USER, KEY, &hKey);
  if (ret == ERROR_SUCCESS) {
    DWORD type = 0;
    DWORD data = 0;
    DWORD len = sizeof(data);
    ret = RegQueryValueEx(hKey, L"Hant", NULL, &type, (LPBYTE)&data, &len);
    if (ret == ERROR_SUCCESS && type == REG_DWORD) {
      HMODULE hInputDLL = LoadLibrary(TEXT("input.dll"));
      if (hInputDLL) {
        PTF_INSTALLLAYOUTORTIP pfnInstallLayoutOrTip;
        pfnInstallLayoutOrTip = (PTF_INSTALLLAYOUTORTIP)GetProcAddress(
            hInputDLL, "InstallLayoutOrTip");
        if (pfnInstallLayoutOrTip) {
          if (data != 0)
            (*pfnInstallLayoutOrTip)(PSZTITLE_HANT, ILOT_UNINSTALL);
          else
            (*pfnInstallLayoutOrTip)(PSZTITLE_HANS, ILOT_UNINSTALL);
        }
        FreeLibrary(hInputDLL);
      }
    }
    RegCloseKey(hKey);
  }
'@
$layoutUninstallNew = @'
  HMODULE hInputDLL = LoadLibrary(TEXT("input.dll"));
  if (hInputDLL) {
    PTF_INSTALLLAYOUTORTIP pfnInstallLayoutOrTip;
    pfnInstallLayoutOrTip = (PTF_INSTALLLAYOUTORTIP)GetProcAddress(
        hInputDLL, "InstallLayoutOrTip");
    if (pfnInstallLayoutOrTip) {
      (*pfnInstallLayoutOrTip)(PSZTITLE_HANS, ILOT_UNINSTALL);
      (*pfnInstallLayoutOrTip)(PSZTITLE_HANT, ILOT_UNINSTALL);
    }
    FreeLibrary(hInputDLL);
  }
'@
Replace-LiteralRequired 'WeaselSetup/imesetup.cpp' $layoutUninstallOld $layoutUninstallNew

# DllUnregisterServer tried to delete a misspelled TIP path under HKCR. Use
# the real CTF location in both the machine and current-user hives. Running
# 32-bit and 64-bit regsvr32 cleans each machine view; the current-user key
# stores the per-user LanguageProfile/Enable state created during activation.
Replace-LiteralRequired 'WeaselTSF/Register.cpp' `
  'static const char c_szTipKeyPrefix[] = "Software\\Microsft\\CTF\\TIP\\";' `
  'static const char c_szTipKeyPrefix[] = "Software\\Microsoft\\CTF\\TIP\\";'
Replace-LiteralRequired 'WeaselTSF/Register.cpp' `
  'RecurseDeleteKeyA(HKEY_CLASSES_ROOT, tipKey);' `
  @'
RecurseDeleteKeyA(HKEY_LOCAL_MACHINE, tipKey);
  RecurseDeleteKeyA(HKEY_CURRENT_USER, tipKey);
'@

# makensis needs an explicit encoding marker to decode localized strings.
# Without this BOM, the Simplified Chinese uninstall display name was written
# to the registry as mojibake even though the compiled native resources were
# already correctly encoded as UTF-16LE.
$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installerText = [System.IO.File]::ReadAllText($installerPath)
[System.IO.File]::WriteAllText($installerPath, $installerText, $utf8WithBom)
$installerBytes = [System.IO.File]::ReadAllBytes($installerPath)
if ($installerBytes.Length -lt 3 -or
    $installerBytes[0] -ne 0xEF -or
    $installerBytes[1] -ne 0xBB -or
    $installerBytes[2] -ne 0xBF) {
  throw 'The localized NSIS script is not marked as UTF-8 with BOM.'
}

[PSCustomObject]@{
  version = '0.1.3-preview'
  confirmedRootCause = 'The first schema in default.yaml was the Traditional Chinese luna_pinyin profile.'
  runtimeFix = 'Reuse the packaged luna_pinyin_simp schema as the first and default profile.'
  installerScriptEncoding = 'UTF-8 with BOM'
  uninstallFixes = @(
    'unregister the 64-bit TSF DLL after disabling WOW64 file-system redirection',
    'remove both ContextIME InstallLayoutOrTip language profiles',
    'delete the correctly spelled ContextIME TIP key from both machine views and the current-user hive'
  )
  preservedUserData = '%APPDATA%\ContextIME'
} | ConvertTo-Json | Set-Content 'contextime-0.1.3-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.1.3-fix-report.json'
