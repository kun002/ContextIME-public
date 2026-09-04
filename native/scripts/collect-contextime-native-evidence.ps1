[CmdletBinding()]
param(
  [ValidateSet('Any', 'Installed', 'Uninstalled')]
  [string]$ExpectedState = 'Any',

  [string]$OutputDirectory = (Join-Path (Get-Location) 'artifacts\native-evidence'),

  [string]$IdentityPath = (Join-Path $PSScriptRoot '..\contextime.identity.json'),

  [string]$ReferenceReportPath,

  [switch]$FailOnMismatch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identityFile = (Resolve-Path $IdentityPath).Path
$identity = Get-Content $identityFile -Raw -Encoding UTF8 | ConvertFrom-Json
$timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$reportDirectory = Join-Path $OutputDirectory $timestamp
[System.IO.Directory]::CreateDirectory($reportDirectory) | Out-Null

function Get-RegistryKeySnapshot {
  param(
    [Parameter(Mandatory = $true)]
    [Microsoft.Win32.RegistryHive]$Hive,

    [Parameter(Mandatory = $true)]
    [Microsoft.Win32.RegistryView]$View,

    [Parameter(Mandatory = $true)]
    [string]$SubKey,

    [string[]]$ValueNames = @()
  )

  $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
  try {
    $key = $baseKey.OpenSubKey($SubKey)
    if ($null -eq $key) {
      return [ordered]@{
        exists = $false
        hive = $Hive.ToString()
        view = $View.ToString()
        subKey = $SubKey
        values = [ordered]@{}
      }
    }

    try {
      $values = [ordered]@{}
      $namesToRead = if ($ValueNames.Count -gt 0) { $ValueNames } else { $key.GetValueNames() }
      foreach ($name in $namesToRead) {
        if ($name -notin $key.GetValueNames()) { continue }
        $displayName = if ([string]::IsNullOrEmpty($name)) { '(Default)' } else { $name }
        $value = $key.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($value -is [byte[]]) {
          $values[$displayName] = "<binary:$($value.Length)-bytes>"
        } elseif ($value -is [string[]]) {
          $values[$displayName] = @($value)
        } else {
          $values[$displayName] = $value
        }
      }
      return [ordered]@{
        exists = $true
        hive = $Hive.ToString()
        view = $View.ToString()
        subKey = $SubKey
        values = $values
      }
    } finally {
      $key.Dispose()
    }
  } finally {
    $baseKey.Dispose()
  }
}

function Get-FileSnapshot {
  param([Parameter(Mandatory = $true)][string]$Path)

  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    return [ordered]@{ path = $Path; exists = $false }
  }

  $item = Get-Item -LiteralPath $Path
  return [ordered]@{
    path = $item.FullName
    exists = $true
    bytes = $item.Length
    lastWriteUtc = $item.LastWriteTimeUtc.ToString('o')
    sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}

function Get-DirectorySnapshot {
  param([Parameter(Mandatory = $true)][string]$Path)

  if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    return [ordered]@{ path = $Path; exists = $false }
  }

  $item = Get-Item -LiteralPath $Path
  return [ordered]@{
    path = $item.FullName
    exists = $true
    lastWriteUtc = $item.LastWriteTimeUtc.ToString('o')
  }
}

function ConvertTo-ComparisonJson {
  param([Parameter(Mandatory = $true)][AllowEmptyCollection()][object]$Value)

  $json = $Value | ConvertTo-Json -Depth 10 -Compress
  $isoTimestamp = '"(?<value>\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2}))"'
  return [regex]::Replace($json, $isoTimestamp, {
    param($match)
    $timestamp = [DateTimeOffset]::Parse(
      $match.Groups['value'].Value,
      [Globalization.CultureInfo]::InvariantCulture
    ).ToUniversalTime().ToString('o')
    return '"' + $timestamp + '"'
  })
}

function Get-KeyboardLayoutSnapshots {
  param([Parameter(Mandatory = $true)][Microsoft.Win32.RegistryView]$View)

  $subKey = 'SYSTEM\CurrentControlSet\Control\Keyboard Layouts'
  $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    $View
  )
  try {
    $layoutsKey = $baseKey.OpenSubKey($subKey)
    if ($null -eq $layoutsKey) { return @() }
    try {
      $matches = @()
      foreach ($layoutName in $layoutsKey.GetSubKeyNames()) {
        $layoutKey = $layoutsKey.OpenSubKey($layoutName)
        if ($null -eq $layoutKey) { continue }
        try {
          $imeFile = [string]$layoutKey.GetValue('Ime File', '')
          if ($imeFile -notmatch '^(?i:contextime|weasel).*\.ime$') { continue }
          $matches += [ordered]@{
            view = $View.ToString()
            layout = $layoutName
            imeFile = $imeFile
            layoutText = [string]$layoutKey.GetValue('Layout Text', '')
          }
        } finally {
          $layoutKey.Dispose()
        }
      }
      return @($matches)
    } finally {
      $layoutsKey.Dispose()
    }
  } finally {
    $baseKey.Dispose()
  }
}

function Find-InstallRoots {
  param([object[]]$RegistrySnapshots)

  $roots = @()
  foreach ($snapshot in $RegistrySnapshots) {
    if (-not $snapshot.exists) { continue }
    foreach ($name in @('InstallDir', 'WeaselRoot')) {
      if ($snapshot.values.Contains($name)) {
        $candidate = [string]$snapshot.values[$name]
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
          $roots += $candidate
        }
      }
    }
  }
  return @($roots | Sort-Object -Unique)
}

$registryViews = @(
  [Microsoft.Win32.RegistryView]::Registry64,
  [Microsoft.Win32.RegistryView]::Registry32
)
$machineRoot = [string]$identity.registry.machineRoot
$userRoot = [string]$identity.registry.userRoot
$uninstallKey = [string]$identity.registry.uninstallKey
$textServiceGuid = ([string]$identity.guids.textService).Trim('{}').ToUpperInvariant()
$profileGuid = ([string]$identity.guids.profile).Trim('{}').ToUpperInvariant()
$tipKey = "SOFTWARE\Microsoft\CTF\TIP\{$textServiceGuid}"
$classKey = "SOFTWARE\Classes\CLSID\{$textServiceGuid}"

$contextimeMachineKeys = @()
$contextimeUserKeys = @()
$contextimeUninstallKeys = @()
$contextimeClassKeys = @()
$contextimeTipMachineKeys = @()
$contextimeTipUserKeys = @()
$weaselMachineKeys = @()
$weaselUninstallKeys = @()
$autorunKeys = @()
foreach ($view in $registryViews) {
  $contextimeMachineKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey $machineRoot
  $contextimeUserKeys += Get-RegistryKeySnapshot -Hive CurrentUser -View $view -SubKey $userRoot
  $contextimeUninstallKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey $uninstallKey
  $contextimeClassKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey $classKey
  $contextimeTipMachineKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey $tipKey
  $contextimeTipUserKeys += Get-RegistryKeySnapshot -Hive CurrentUser -View $view -SubKey $tipKey
  $weaselMachineKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey 'Software\Rime\Weasel'
  $weaselUninstallKeys += Get-RegistryKeySnapshot -Hive LocalMachine -View $view -SubKey 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel'
  $autorunKeys += Get-RegistryKeySnapshot `
    -Hive LocalMachine `
    -View $view `
    -SubKey 'Software\Microsoft\Windows\CurrentVersion\Run' `
    -ValueNames @('ContextIMEServer', 'WeaselServer')
}

$installRoots = Find-InstallRoots -RegistrySnapshots @($contextimeMachineKeys + $contextimeUninstallKeys)
$installDirectories = @($installRoots | ForEach-Object { Get-DirectorySnapshot -Path $_ })

$processes = @(
  Get-Process -Name WeaselServer -ErrorAction SilentlyContinue | ForEach-Object {
    $path = $null
    try { $path = $_.Path } catch { $path = '<access-denied>' }
    if ([string]::IsNullOrWhiteSpace($path)) { $path = '<unavailable>' }
    [ordered]@{
      id = $_.Id
      name = $_.ProcessName
      path = $path
      startTimeUtc = try { $_.StartTime.ToUniversalTime().ToString('o') } catch { $null }
    }
  }
)

$pipes = @()
try {
  $pipes = @(
    Get-ChildItem -LiteralPath '\\.\pipe\' -ErrorAction Stop |
      Where-Object { $_.Name -match '(?i)contextime|weasel' } |
      Select-Object -ExpandProperty Name |
      Sort-Object -Unique
  )
} catch {
  $pipes = @("<enumeration-failed:$($_.Exception.Message)>")
}

$languageTips = @()
try {
  $languageTips = @(
    Get-WinUserLanguageList | ForEach-Object {
      foreach ($tip in $_.InputMethodTips) {
        [ordered]@{ languageTag = $_.LanguageTag; inputMethodTip = $tip }
      }
    }
  )
} catch {
  $languageTips = @([ordered]@{
    languageTag = $null
    inputMethodTip = $null
    error = $_.Exception.Message
  })
}

$windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
$systemFiles = @(
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'System32\contextime.dll')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'System32\contextime.ime')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'SysWOW64\contextime.dll')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'SysWOW64\contextime.ime')
)
$weaselSystemFiles = @(
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'System32\weasel.dll')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'System32\weasel.ime')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'SysWOW64\weasel.dll')
  Get-FileSnapshot -Path (Join-Path $windowsRoot 'SysWOW64\weasel.ime')
)

$userDataPath = [Environment]::ExpandEnvironmentVariables([string]$identity.directories.userData)
$logPath = [Environment]::ExpandEnvironmentVariables([string]$identity.directories.log)
$weaselUserData = Get-DirectorySnapshot -Path ([Environment]::ExpandEnvironmentVariables('%APPDATA%\Rime'))
$contextimeProcessCount = @(
  $processes | Where-Object { $_.path -is [string] -and $_.path -match '(?i)\\ContextIME\\' }
).Count
$contextimePipeCount = @($pipes | Where-Object { $_ -match '(?i)contextime' }).Count
$contextimeServerByRuntime = $processes.Count -gt 0 -and $contextimePipeCount -gt 0
$serverProcessEvidence = if ($contextimeProcessCount -gt 0) {
  'executable-path'
} elseif ($contextimeServerByRuntime) {
  'process-name-and-contextime-pipe'
} else {
  'none'
}
$contextimeTip = "0804:{$textServiceGuid}{$profileGuid}"

$observed = [ordered]@{
  machineRegistry = @($contextimeMachineKeys | Where-Object { $_.exists }).Count -gt 0
  uninstallRegistry = @($contextimeUninstallKeys | Where-Object { $_.exists }).Count -gt 0
  installDirectory = @($installDirectories | Where-Object { $_.exists }).Count -gt 0
  tsfClsid = @($contextimeClassKeys | Where-Object { $_.exists }).Count -gt 0
  tsfTip = @($contextimeTipMachineKeys + $contextimeTipUserKeys | Where-Object { $_.exists }).Count -gt 0
  languageProfile = @($languageTips | Where-Object { $_.inputMethodTip -eq $contextimeTip }).Count -gt 0
  systemTsfDll = @($systemFiles | Where-Object { $_.exists -and $_.path -match '(?i)contextime\.dll$' }).Count -gt 0
  serverProcess = $contextimeProcessCount -gt 0 -or $contextimeServerByRuntime
  serverProcessEvidence = $serverProcessEvidence
  ipcPipe = $contextimePipeCount -gt 0
  userDataDirectory = (Test-Path -LiteralPath $userDataPath -PathType Container)
}

$mismatches = @()
if ($ExpectedState -eq 'Installed') {
  foreach ($required in @('machineRegistry', 'uninstallRegistry', 'installDirectory', 'tsfClsid', 'tsfTip', 'languageProfile', 'systemTsfDll', 'serverProcess', 'ipcPipe', 'userDataDirectory')) {
    if (-not $observed[$required]) { $mismatches += "Installed state is missing: $required" }
  }
} elseif ($ExpectedState -eq 'Uninstalled') {
  foreach ($forbidden in @('machineRegistry', 'uninstallRegistry', 'installDirectory', 'tsfClsid', 'tsfTip', 'languageProfile', 'systemTsfDll', 'serverProcess', 'ipcPipe')) {
    if ($observed[$forbidden]) { $mismatches += "Uninstalled state still has: $forbidden" }
  }
}

$referenceComparison = [ordered]@{
  supplied = -not [string]::IsNullOrWhiteSpace($ReferenceReportPath)
  reportPath = $ReferenceReportPath
  weaselProtectedStateUnchanged = $null
  changedSections = @()
}
if ($referenceComparison.supplied) {
  $referenceFile = (Resolve-Path $ReferenceReportPath).Path
  $reference = Get-Content $referenceFile -Raw -Encoding UTF8 | ConvertFrom-Json
  $changedSections = @()
  $sections = @(
    [ordered]@{ name = 'machineRegistry'; referenceProperty = 'weaselMachineRegistry'; current = @($weaselMachineKeys) }
    [ordered]@{ name = 'uninstallRegistry'; referenceProperty = 'weaselUninstallRegistry'; current = @($weaselUninstallKeys) }
    [ordered]@{ name = 'systemFiles'; referenceProperty = 'weaselSystemFiles'; current = @($weaselSystemFiles) }
  )
  foreach ($section in $sections) {
    $before = ConvertTo-ComparisonJson $reference.coexistence.($section.referenceProperty)
    $after = ConvertTo-ComparisonJson $section.current
    if ($before -ne $after) { $changedSections += $section.name }
  }
  if ([bool]$reference.coexistence.weaselUserData.exists -ne [bool]$weaselUserData.exists) {
    $changedSections += 'userDataExistence'
  }
  $referenceComparison.reportPath = $referenceFile
  $referenceComparison.changedSections = @($changedSections)
  $referenceComparison.weaselProtectedStateUnchanged = $changedSections.Count -eq 0
  if ($changedSections.Count -gt 0) {
    $mismatches += "Weasel protected state changed: $($changedSections -join ', ')"
  }
}

$principal = [Security.Principal.WindowsPrincipal]::new(
  [Security.Principal.WindowsIdentity]::GetCurrent()
)
$report = [ordered]@{
  schemaVersion = 1
  collectedAtUtc = [DateTime]::UtcNow.ToString('o')
  expectedState = $ExpectedState
  identity = [ordered]@{
    product = $identity.product
    textServiceGuid = $textServiceGuid
    profileGuid = $profileGuid
    expectedInputMethodTip = $contextimeTip
    upstream = $identity.upstream
  }
  machine = [ordered]@{
    os = [Environment]::OSVersion.VersionString
    osBuild = [Environment]::OSVersion.Version.Build
    is64BitOperatingSystem = [Environment]::Is64BitOperatingSystem
    is64BitProcess = [Environment]::Is64BitProcess
    elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  }
  observed = $observed
  mismatches = $mismatches
  referenceComparison = $referenceComparison
  contextime = [ordered]@{
    machineRegistry = $contextimeMachineKeys
    userRegistry = $contextimeUserKeys
    uninstallRegistry = $contextimeUninstallKeys
    tsfClsidRegistry = $contextimeClassKeys
    tsfTipMachineRegistry = $contextimeTipMachineKeys
    tsfTipUserRegistry = $contextimeTipUserKeys
    installDirectories = $installDirectories
    systemFiles = $systemFiles
    userData = Get-DirectorySnapshot -Path $userDataPath
    log = Get-DirectorySnapshot -Path $logPath
  }
  coexistence = [ordered]@{
    weaselMachineRegistry = $weaselMachineKeys
    weaselUninstallRegistry = $weaselUninstallKeys
    weaselSystemFiles = $weaselSystemFiles
    weaselUserData = $weaselUserData
  }
  runtime = [ordered]@{
    serverProcesses = $processes
    matchingNamedPipes = $pipes
    inputMethodTips = $languageTips
    keyboardLayouts = @(
      Get-KeyboardLayoutSnapshots -View Registry64
      Get-KeyboardLayoutSnapshots -View Registry32
    )
    autorunRegistry = $autorunKeys
  }
}

$jsonPath = Join-Path $reportDirectory 'contextime-native-evidence.json'
$summaryPath = Join-Path $reportDirectory 'contextime-native-evidence.txt'
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$summaryLines = @(
  "ContextIME native evidence: $($report.collectedAtUtc)"
  "Expected state: $ExpectedState"
  "Observed: $($observed | ConvertTo-Json -Compress)"
  "Mismatches: $($mismatches.Count)"
)
if ($mismatches.Count -gt 0) { $summaryLines += $mismatches | ForEach-Object { "- $_" } }
$summaryLines += "JSON report: $jsonPath"
$summaryLines | Set-Content -LiteralPath $summaryPath -Encoding UTF8
$summaryLines | Write-Host

if ($FailOnMismatch -and $mismatches.Count -gt 0) {
  throw "ContextIME native state does not match '$ExpectedState'. Evidence: $jsonPath"
}

[PSCustomObject]@{
  expectedState = $ExpectedState
  mismatchCount = $mismatches.Count
  reportPath = $jsonPath
  summaryPath = $summaryPath
}
