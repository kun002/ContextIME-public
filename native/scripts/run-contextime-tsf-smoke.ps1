[CmdletBinding()]
param(
  [string]$EvidencePath,
  [string]$SequencePath,
  [string]$InputText = 'shurufa',
  [string]$ExpectedText = '',
  [string]$ExpectedEnglish = 'abc',
  [string]$FollowupInputText = '',
  [string]$ExpectedFollowupText = '',
  [ValidateSet('space', 'enter', 'escape', '1', '2', '3', '4', '5', '6', '7', '8', '9')]
  [string]$SelectionKey = 'space',
  [ValidateRange(0, 100)]
  [int]$PreSelectionBackspaceCount = 0,
  [ValidateRange(0, 10)]
  [int]$PageDownCount = 0,
  [guid]$TextServiceGuid = '9FA3541F-F3F9-4C67-AA42-6C9AB15FB6A9',
  [guid]$ProfileGuid = 'A200BA94-B1A7-4668-A22B-CA61EC1E79F7',
  [ValidateRange(0, 65535)]
  [int]$LanguageId = 0x0804,
  [ValidateRange(1, 60)]
  [int]$TimeoutSeconds = 8,
  [switch]$ForceBuild,
  [switch]$CompileOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
  throw 'The ContextIME TSF smoke test requires Windows.'
}
if ([string]::IsNullOrEmpty($FollowupInputText) -ne [string]::IsNullOrEmpty($ExpectedFollowupText)) {
  throw 'FollowupInputText and ExpectedFollowupText must either both be set or both be empty.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sourcePath = Join-Path $repositoryRoot 'native\tests\tsf-smoke\ContextIMETsfSmoke.cs'
$toolDirectory = Join-Path $repositoryRoot 'artifacts\native-tools'
$executablePath = Join-Path $toolDirectory 'contextime-tsf-smoke.exe'

if (-not $EvidencePath) {
  $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
  $EvidencePath = Join-Path $repositoryRoot "artifacts\native-evidence\$stamp\contextime-tsf-smoke.json"
}
$EvidencePath = [System.IO.Path]::GetFullPath($EvidencePath)
$sequenceStepCount = 0
if ($SequencePath) {
  $SequencePath = [System.IO.Path]::GetFullPath($SequencePath)
  if (-not (Test-Path -LiteralPath $SequencePath -PathType Leaf)) {
    throw "TSF smoke sequence was not found: $SequencePath"
  }
  $sequenceDefinition = Get-Content -LiteralPath $SequencePath -Raw | ConvertFrom-Json
  $sequenceStepCount = @($sequenceDefinition.Steps).Count
  if ($sequenceStepCount -lt 1) {
    throw "TSF smoke sequence has no steps: $SequencePath"
  }
}

New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null
New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($EvidencePath)) -Force | Out-Null

$source = Get-Item $sourcePath
$needsBuild = $ForceBuild -or -not (Test-Path $executablePath) -or ((Get-Item $executablePath).LastWriteTimeUtc -lt $source.LastWriteTimeUtc)
if ($needsBuild) {
  if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -ForceBuild -CompileOnly
    if ($LASTEXITCODE -ne 0) {
      throw "Windows PowerShell failed to compile the TSF smoke tool: $LASTEXITCODE"
    }
  } else {
    if (Test-Path $executablePath) {
      Remove-Item -LiteralPath $executablePath -Force
    }
    Add-Type `
      -Path $sourcePath `
      -OutputAssembly $executablePath `
      -OutputType WindowsApplication `
      -ReferencedAssemblies @(
        'System.dll',
        'System.Core.dll',
        'System.Drawing.dll',
        'System.Windows.Forms.dll',
        'System.Web.Extensions.dll'
      )
  }
}

if ($CompileOnly) {
  Write-Host "Compiled ContextIME TSF smoke tool: $executablePath"
  return
}

function Quote-NativeArgument {
  param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)
  return '"' + $Value.Replace('"', '\"') + '"'
}

$arguments = @(
  '--evidence', (Quote-NativeArgument $EvidencePath),
  '--input', (Quote-NativeArgument $InputText),
  '--expected', (Quote-NativeArgument $ExpectedText),
  '--expected-english', (Quote-NativeArgument $ExpectedEnglish),
  '--followup-input', (Quote-NativeArgument $FollowupInputText),
  '--expected-followup', (Quote-NativeArgument $ExpectedFollowupText),
  '--selection-key', (Quote-NativeArgument $SelectionKey),
  '--pre-selection-backspace-count', $PreSelectionBackspaceCount,
  '--page-down-count', $PageDownCount,
  '--text-service', (Quote-NativeArgument $TextServiceGuid.ToString('D')),
  '--profile', (Quote-NativeArgument $ProfileGuid.ToString('D')),
  '--language', $LanguageId,
  '--timeout-ms', ($TimeoutSeconds * 1000)
)
if ($SequencePath) {
  $arguments += @('--sequence', (Quote-NativeArgument $SequencePath))
}
$arguments = $arguments -join ' '

$process = Start-Process -FilePath $executablePath -ArgumentList $arguments -PassThru
$processTimeoutSeconds = if ($sequenceStepCount -gt 0) {
  (($TimeoutSeconds + 3) * $sequenceStepCount) + 10
} else {
  $TimeoutSeconds + 10
}
if (-not $process.WaitForExit($processTimeoutSeconds * 1000)) {
  Stop-Process -Id $process.Id -Force
  throw "ContextIME TSF smoke test exceeded $processTimeoutSeconds seconds."
}

if (-not (Test-Path $EvidencePath -PathType Leaf)) {
  throw "TSF smoke process exited with code $($process.ExitCode) without evidence: $EvidencePath"
}

$evidenceJson = [System.IO.File]::ReadAllText($EvidencePath, [System.Text.Encoding]::UTF8)
$evidence = $evidenceJson | ConvertFrom-Json
$evidence | ConvertTo-Json -Depth 8
if ($process.ExitCode -ne 0 -or -not $evidence.Passed) {
  throw "ContextIME TSF smoke test failed with exit code $($process.ExitCode). Evidence: $EvidencePath"
}

Write-Host "ContextIME TSF smoke evidence: $EvidencePath"
