[CmdletBinding()]
param(
  [ValidateSet('notepad', 'browser', 'vscode', 'visualstudio', 'terminal')]
  [string]$Application = 'notepad',
  [ValidateSet('baseline', 'terminal-context', 'vscode-context', 'vscode-project-candidate')]
  [string]$Scenario = 'baseline',
  [string]$EvidencePath,
  [string]$InputText = 'shurufa',
  [string]$ExpectedText = '',
  [string]$ExpectedEnglish = 'abc',
  [string]$VSCodeExtensionPath,
  [guid]$TextServiceGuid = '9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9',
  [guid]$ProfileGuid = 'A200BA94-B1A7-4668-A22B-CA61EC1E79F7',
  [string]$ServerPath,
  [ValidateRange(0, 65535)]
  [int]$LanguageId = 0x0804,
  [ValidateRange(5, 120)]
  [int]$TimeoutSeconds = 20,
  [switch]$ForceBuild,
  [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $ExpectedText) {
  $ExpectedText = [string]::Concat([char]0x8F93, [char]0x5165, [char]0x6CD5)
}
if ($Scenario -eq 'terminal-context' -and $Application -ne 'terminal') {
  throw 'The terminal-context scenario requires -Application terminal.'
}
if ($Scenario -eq 'vscode-context' -and $Application -ne 'vscode') {
  throw 'The vscode-context scenario requires -Application vscode.'
}
if ($Scenario -eq 'vscode-project-candidate' -and $Application -ne 'vscode') {
  throw 'The vscode-project-candidate scenario requires -Application vscode.'
}
if ($Scenario -in @('vscode-context', 'vscode-project-candidate')) {
  if ([string]::IsNullOrWhiteSpace($VSCodeExtensionPath)) {
    throw 'The vscode-context scenario requires -VSCodeExtensionPath.'
  }
  $VSCodeExtensionPath = (Resolve-Path -LiteralPath $VSCodeExtensionPath).Path
}

if ($env:OS -ne 'Windows_NT') {
  throw 'The ContextIME application smoke test requires Windows.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePath = Join-Path $repositoryRoot 'native\tests\app-smoke\ContextIMEAppSmoke.cs'
$toolDirectory = Join-Path $repositoryRoot 'artifacts\native-tools'
$executablePath = Join-Path $toolDirectory 'contextime-app-smoke.exe'

if (-not $EvidencePath) {
  $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
  $EvidencePath = Join-Path $repositoryRoot "artifacts\native-evidence\$stamp\contextime-$Application-smoke.json"
}
$EvidencePath = [System.IO.Path]::GetFullPath($EvidencePath)

New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($EvidencePath)) -Force | Out-Null
foreach ($staleEvidencePath in @(
  $EvidencePath,
  "$EvidencePath.candidate.png",
  "$EvidencePath.manual-candidate.png",
  "$EvidencePath.protected-candidate.png",
  "$EvidencePath.comment-candidate.png",
  "$EvidencePath.project-candidate.png",
  "$EvidencePath.project-dictionary.dict",
  "$EvidencePath.trace.log"
)) {
  if ([System.IO.File]::Exists($staleEvidencePath)) {
    [System.IO.File]::Delete($staleEvidencePath)
  }
}

$source = Get-Item $sourcePath
$needsBuild = $ForceBuild -or -not (Test-Path $executablePath) -or ((Get-Item $executablePath).LastWriteTimeUtc -lt $source.LastWriteTimeUtc)
if ($needsBuild) {
  if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -ForceBuild -CompileOnly
    if ($LASTEXITCODE -ne 0) {
      throw "Windows PowerShell failed to compile the application smoke tool: $LASTEXITCODE"
    }
  } else {
    if (Test-Path $executablePath) {
      Remove-Item -LiteralPath $executablePath -Force
    }
    $gacRoot = Join-Path $env:WINDIR 'Microsoft.NET\assembly\GAC_MSIL'
    $uiAutomationClient = Get-ChildItem -Path (Join-Path $gacRoot 'UIAutomationClient') -Recurse -Filter 'UIAutomationClient.dll' | Select-Object -First 1
    $uiAutomationTypes = Get-ChildItem -Path (Join-Path $gacRoot 'UIAutomationTypes') -Recurse -Filter 'UIAutomationTypes.dll' | Select-Object -First 1
    $windowsBase = Get-ChildItem -Path (Join-Path $gacRoot 'WindowsBase') -Recurse -Filter 'WindowsBase.dll' | Select-Object -First 1
    if (-not $uiAutomationClient -or -not $uiAutomationTypes -or -not $windowsBase) {
      throw 'The .NET Framework UI Automation assemblies are unavailable.'
    }
    Add-Type `
      -Path $sourcePath `
      -OutputAssembly $executablePath `
      -OutputType WindowsApplication `
      -ReferencedAssemblies @(
        'System.dll',
        'System.Core.dll',
        'System.Drawing.dll',
        'System.Web.Extensions.dll',
        'System.Windows.Forms.dll',
        $uiAutomationClient.FullName,
        $uiAutomationTypes.FullName,
        $windowsBase.FullName
      )
  }
}

if ($CompileOnly) {
  Write-Host "Compiled ContextIME application smoke tool: $executablePath"
  return
}

function Resolve-ContextIMEServerPath {
  param([string]$RequestedPath)

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $resolvedRequestedPath = [System.IO.Path]::GetFullPath($RequestedPath)
    if (-not (Test-Path -LiteralPath $resolvedRequestedPath -PathType Leaf)) {
      throw "ContextIME Server was not found: $resolvedRequestedPath"
    }
    return $resolvedRequestedPath
  }

  $registryPaths = @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ContextIME',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\ContextIME'
  )
  foreach ($registryPath in $registryPaths) {
    if (-not (Test-Path -LiteralPath $registryPath)) { continue }
    $installState = Get-ItemProperty -LiteralPath $registryPath
    foreach ($valueName in @('WeaselRoot', 'InstallDir')) {
      $root = [string]$installState.$valueName
      if ([string]::IsNullOrWhiteSpace($root)) { continue }
      $candidate = Join-Path $root 'WeaselServer.exe'
      if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return [System.IO.Path]::GetFullPath($candidate)
      }
    }
  }

  throw 'The installed ContextIME Server path was not found. Pass -ServerPath explicitly.'
}

function Set-ContextIMEChineseMode {
  param([Parameter(Mandatory = $true)][string]$ExecutablePath)

  $modeProcess = Start-Process -FilePath $ExecutablePath -ArgumentList '/nascii' -PassThru -WindowStyle Hidden
  if (-not $modeProcess.WaitForExit(5000)) {
    Stop-Process -Id $modeProcess.Id -Force
    throw 'ContextIME Server /nascii exceeded 5 seconds.'
  }
  if ($modeProcess.ExitCode -ne 0) {
    throw "ContextIME Server /nascii failed with exit code $($modeProcess.ExitCode)."
  }
  Start-Sleep -Milliseconds 350
}

$ServerPath = Resolve-ContextIMEServerPath -RequestedPath $ServerPath

function Quote-NativeArgument {
  param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)
  return '"' + $Value.Replace('"', '\"') + '"'
}

$argumentParts = @(
  '--application', (Quote-NativeArgument $Application),
  '--scenario', (Quote-NativeArgument $Scenario),
  '--evidence', (Quote-NativeArgument $EvidencePath),
  '--input', (Quote-NativeArgument $InputText),
  '--expected', (Quote-NativeArgument $ExpectedText),
  '--expected-english', (Quote-NativeArgument $ExpectedEnglish),
  '--text-service', (Quote-NativeArgument $TextServiceGuid.ToString('D')),
  '--profile', (Quote-NativeArgument $ProfileGuid.ToString('D')),
  '--language', $LanguageId,
  '--timeout-ms', ($TimeoutSeconds * 1000)
)
if ($VSCodeExtensionPath) {
  $argumentParts += @('--vscode-extension', (Quote-NativeArgument $VSCodeExtensionPath))
}
$arguments = $argumentParts -join ' '

Set-ContextIMEChineseMode -ExecutablePath $ServerPath
try {
  $process = Start-Process -FilePath $executablePath -ArgumentList $arguments -PassThru
  if (-not $process.WaitForExit(($TimeoutSeconds + 20) * 1000)) {
    Stop-Process -Id $process.Id -Force
    throw "ContextIME application smoke test exceeded $($TimeoutSeconds + 20) seconds."
  }
} finally {
  Set-ContextIMEChineseMode -ExecutablePath $ServerPath
}

if (-not (Test-Path $EvidencePath -PathType Leaf)) {
  throw "Application smoke process exited with code $($process.ExitCode) without evidence: $EvidencePath"
}

$evidenceJson = [System.IO.File]::ReadAllText($EvidencePath, [System.Text.Encoding]::UTF8)
$evidence = $evidenceJson | ConvertFrom-Json
$evidence | ConvertTo-Json -Depth 8
if ($process.ExitCode -ne 0 -or -not $evidence.Passed) {
  throw "ContextIME application smoke test failed with exit code $($process.ExitCode). Evidence: $EvidencePath"
}

Write-Host "ContextIME application smoke evidence: $EvidencePath"
