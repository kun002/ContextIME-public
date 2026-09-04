[CmdletBinding()]
param(
  [ValidateRange(1, 480)]
  [int]$DurationMinutes = 30,
  [ValidateRange(0, 86400)]
  [int]$DurationSeconds = 0,
  [ValidateSet('notepad')]
  [string]$Application = 'notepad',
  [string]$EvidencePath,
  [string]$ServerPath,
  [ValidateRange(5, 120)]
  [int]$TimeoutSeconds = 30,
  [ValidateRange(0, 10000)]
  [int]$IterationPauseMilliseconds = 250,
  [string]$CancellationPath,
  [string]$RepositoryCommit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
  throw 'The ContextIME stability test requires Windows.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$appSmokeScript = Join-Path $repositoryRoot 'native\scripts\run-contextime-app-smoke.ps1'
if (-not (Test-Path -LiteralPath $appSmokeScript -PathType Leaf)) {
  throw "The ContextIME application smoke script was not found: $appSmokeScript"
}

if (-not $EvidencePath) {
  $stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
  $EvidencePath = Join-Path $repositoryRoot "artifacts\native-evidence\$stamp\contextime-stability.json"
}
$EvidencePath = [System.IO.Path]::GetFullPath($EvidencePath)
$iterationDirectory = "$EvidencePath.iterations"
if ([System.IO.File]::Exists($EvidencePath) -or [System.IO.Directory]::Exists($iterationDirectory)) {
  throw "Stability evidence already exists. Choose a new -EvidencePath: $EvidencePath"
}
if ($Application -eq 'notepad' -and @(Get-Process -Name Notepad -ErrorAction SilentlyContinue).Count -gt 0) {
  throw 'Close all Notepad processes before the isolated stability run. No process was terminated.'
}
New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($EvidencePath)) -Force | Out-Null
New-Item -ItemType Directory -Path $iterationDirectory | Out-Null

function Resolve-ContextIMEServerPath {
  param([string]$RequestedPath)

  if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
    $resolvedRequestedPath = [System.IO.Path]::GetFullPath($RequestedPath)
    if (-not (Test-Path -LiteralPath $resolvedRequestedPath -PathType Leaf)) {
      throw "ContextIME Server was not found: $resolvedRequestedPath"
    }
    return $resolvedRequestedPath
  }

  foreach ($registryPath in @(
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ContextIME',
    'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\ContextIME'
  )) {
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

$ServerPath = Resolve-ContextIMEServerPath -RequestedPath $ServerPath
$targetDurationSeconds = if ($DurationSeconds -gt 0) { $DurationSeconds } else { $DurationMinutes * 60 }
if (-not [string]::IsNullOrWhiteSpace($CancellationPath)) {
  $CancellationPath = [System.IO.Path]::GetFullPath($CancellationPath)
}

if ($null -eq ('ContextIME.NativeTests.StabilityNativeMethods' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace ContextIME.NativeTests
{
    public static class StabilityNativeMethods
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr FindWindow(string className, string windowName);

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    }
}
'@
}

function Test-ContextIMEPipe {
  try {
    return @(
      Get-ChildItem -LiteralPath '\\.\pipe\' -ErrorAction Stop |
        Where-Object { $_.Name -match '(?i)ContextIMENamedPipe$' }
    ).Count -gt 0
  } catch {
    return $false
  }
}

function Get-ContextIMEServerSnapshot {
  $observedUtc = (Get-Date).ToUniversalTime().ToString('o')
  $window = [ContextIME.NativeTests.StabilityNativeMethods]::FindWindow('ContextIMEIPCWindow_1.0', 'ContextIMEIPCWindow_1.0')
  if ($window -eq [IntPtr]::Zero) {
    return [ordered]@{
      observedUtc = $observedUtc
      exists = $false
      ipcWindowHandle = $null
      processId = 0
      startTimeUtc = $null
      workingSetBytes = 0
      privateMemoryBytes = 0
      handleCount = 0
      responding = $false
      pipePresent = Test-ContextIMEPipe
    }
  }

  [uint32]$serverProcessId = 0
  [void][ContextIME.NativeTests.StabilityNativeMethods]::GetWindowThreadProcessId($window, [ref]$serverProcessId)
  try {
    $process = Get-Process -Id $serverProcessId -ErrorAction Stop
    $process.Refresh()
    return [ordered]@{
      observedUtc = $observedUtc
      exists = $true
      ipcWindowHandle = '0x{0:x}' -f $window.ToInt64()
      processId = [int]$serverProcessId
      startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
      workingSetBytes = [long]$process.WorkingSet64
      privateMemoryBytes = [long]$process.PrivateMemorySize64
      handleCount = [int]$process.HandleCount
      responding = [bool]$process.Responding
      pipePresent = Test-ContextIMEPipe
    }
  } catch {
    return [ordered]@{
      observedUtc = $observedUtc
      exists = $false
      ipcWindowHandle = '0x{0:x}' -f $window.ToInt64()
      processId = [int]$serverProcessId
      startTimeUtc = $null
      workingSetBytes = 0
      privateMemoryBytes = 0
      handleCount = 0
      responding = $false
      pipePresent = Test-ContextIMEPipe
      error = $_.Exception.Message
    }
  }
}

function Wait-ApplicationProcessExit {
  param(
    [int]$ApplicationProcessId,
    [int]$TimeoutMilliseconds = 5000
  )

  if ($ApplicationProcessId -le 0) { return $false }
  $exitClock = [System.Diagnostics.Stopwatch]::StartNew()
  do {
    if ($null -eq (Get-Process -Id $ApplicationProcessId -ErrorAction SilentlyContinue)) {
      return $true
    }
    Start-Sleep -Milliseconds 100
  } while ($exitClock.ElapsedMilliseconds -lt $TimeoutMilliseconds)
  return $false
}

$requiredIterationFields = @(
  'ProfileActivated',
  'ProfileRestored',
  'ForegroundAcquired',
  'InitialCompositionDismissed',
  'FixturePrepared',
  'ForegroundBeforePinyinInput',
  'ForegroundBeforeCandidateCapture',
  'ForegroundAfterCandidateCapture',
  'ForegroundBeforeCommit',
  'ForegroundBeforeEnglishSwitch',
  'ForegroundBeforeEnglishInput',
  'ForegroundAfterInput',
  'CandidateWindowDetected',
  'CommitMatched',
  'EnglishModeMatched',
  'ChineseModeRestored',
  'CleanupForegroundAcquired',
  'CompositionDismissedBeforeClose',
  'ProbeClosed',
  'Passed'
)

$startedUtc = (Get-Date).ToUniversalTime().ToString('o')
$clock = [System.Diagnostics.Stopwatch]::StartNew()
$iterationRecords = [System.Collections.Generic.List[object]]::new()
$failures = [System.Collections.Generic.List[string]]::new()
$initialServer = Get-ContextIMEServerSnapshot
$finalServer = $initialServer
$iterationNumber = 0

$osState = Get-ItemProperty -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$serverFile = Get-Item -LiteralPath $ServerPath
$scriptSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
$serverSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ServerPath).Hash.ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($RepositoryCommit)) {
  $resolvedCommit = @(& git -C $repositoryRoot rev-parse HEAD 2>$null)
  if ($LASTEXITCODE -ne 0 -or $resolvedCommit.Count -ne 1) {
    throw 'Could not resolve the repository commit. Pass -RepositoryCommit when running from a packaged harness.'
  }
  $RepositoryCommit = [string]$resolvedCommit[0]
}
if ($RepositoryCommit -notmatch '^[0-9a-fA-F]{40}$') {
  throw "RepositoryCommit must be a full 40-character Git SHA: $RepositoryCommit"
}
$repositoryCommit = $RepositoryCommit.ToLowerInvariant()

function New-StabilitySummary {
  param([bool]$Complete)

  $elapsedSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
  $successfulIterations = @($iterationRecords | Where-Object { $_.passed }).Count
  $serverStable = [bool]$initialServer.exists -and [bool]$initialServer.pipePresent -and
    [bool]$finalServer.exists -and [bool]$finalServer.pipePresent -and
    $finalServer.processId -eq $initialServer.processId -and
    $finalServer.startTimeUtc -eq $initialServer.startTimeUtc -and
    @($iterationRecords | Where-Object {
      -not $_.server.exists -or -not $_.server.pipePresent -or
      $_.server.processId -ne $initialServer.processId -or
      $_.server.startTimeUtc -ne $initialServer.startTimeUtc
    }).Count -eq 0
  $durationSatisfied = $elapsedSeconds -ge $targetDurationSeconds
  $passed = $Complete -and $durationSatisfied -and $serverStable -and
    $iterationRecords.Count -gt 0 -and $successfulIterations -eq $iterationRecords.Count -and $failures.Count -eq 0
  $privateMemorySamples = @($initialServer.privateMemoryBytes) + @($iterationRecords | ForEach-Object { $_.server.privateMemoryBytes }) + @($finalServer.privateMemoryBytes)
  $handleSamples = @($initialServer.handleCount) + @($iterationRecords | ForEach-Object { $_.server.handleCount }) + @($finalServer.handleCount)

  return [ordered]@{
    schemaVersion = 'contextime.stability.v1'
    startedUtc = $startedUtc
    completedUtc = if ($Complete) { (Get-Date).ToUniversalTime().ToString('o') } else { $null }
    inProgress = -not $Complete
    application = $Application
    targetDurationSeconds = $targetDurationSeconds
    elapsedSeconds = $elapsedSeconds
    durationSatisfied = $durationSatisfied
    iterationCount = $iterationRecords.Count
    successfulIterationCount = $successfulIterations
    repositoryCommit = $repositoryCommit
    harnessScriptSha256 = $scriptSha256
    windows = [ordered]@{
      productName = [string]$osState.ProductName
      displayVersion = [string]$osState.DisplayVersion
      build = [string]$osState.CurrentBuildNumber + '.' + [string]$osState.UBR
    }
    runtime = [ordered]@{
      serverPath = $ServerPath
      serverVersion = $serverFile.VersionInfo.FileVersion
      serverSha256 = $serverSha256
    }
    initialServer = $initialServer
    finalServer = $finalServer
    serverStable = $serverStable
    privateMemoryDeltaBytes = [long]$finalServer.privateMemoryBytes - [long]$initialServer.privateMemoryBytes
    peakPrivateMemoryBytes = [long](($privateMemorySamples | Measure-Object -Maximum).Maximum)
    handleCountDelta = [int]$finalServer.handleCount - [int]$initialServer.handleCount
    peakHandleCount = [int](($handleSamples | Measure-Object -Maximum).Maximum)
    iterations = @($iterationRecords)
    failures = @($failures)
    cancellationRequested = -not [string]::IsNullOrWhiteSpace($CancellationPath) -and
      [System.IO.File]::Exists($CancellationPath)
    passed = $passed
  }
}

function Write-StabilitySummary {
  param([bool]$Complete)

  $summary = New-StabilitySummary -Complete:$Complete
  $json = $summary | ConvertTo-Json -Depth 12
  [System.IO.File]::WriteAllText($EvidencePath, $json, [System.Text.UTF8Encoding]::new($false))
  return $summary
}

try {
  if (-not $initialServer.exists -or -not $initialServer.pipePresent) {
    throw 'ContextIME Server or ContextIMENamedPipe was unavailable before the stability run.'
  }

  do {
    if (-not [string]::IsNullOrWhiteSpace($CancellationPath) -and
        [System.IO.File]::Exists($CancellationPath)) {
      $failures.Add("Cancellation requested before iteration $($iterationNumber + 1).")
      break
    }
    $iterationNumber++
    $iterationStartedUtc = (Get-Date).ToUniversalTime().ToString('o')
    $iterationEvidencePath = Join-Path $iterationDirectory ('{0:d5}-{1}.json' -f $iterationNumber, $Application)
    $childError = $null
    try {
      & $appSmokeScript `
        -Application $Application `
        -EvidencePath $iterationEvidencePath `
        -ServerPath $ServerPath `
        -TimeoutSeconds $TimeoutSeconds *> $null
    } catch {
      $childError = $_.Exception.Message
    }

    $appEvidence = $null
    if ([System.IO.File]::Exists($iterationEvidencePath)) {
      try {
        $appEvidence = [System.IO.File]::ReadAllText($iterationEvidencePath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
      } catch {
        $childError = "Iteration evidence could not be parsed: $($_.Exception.Message)"
      }
    } elseif (-not $childError) {
      $childError = 'The application smoke did not produce iteration evidence.'
    }

    $failedFields = @()
    $appErrors = @()
    if ($null -ne $appEvidence) {
      $failedFields = @($requiredIterationFields | Where-Object { -not [bool]$appEvidence.$_ })
      $appErrors = @($appEvidence.Errors)
    }
    $applicationProcessExited = $null -ne $appEvidence -and
      (Wait-ApplicationProcessExit -ApplicationProcessId ([int]$appEvidence.ApplicationProcessId))
    $finalServer = Get-ContextIMEServerSnapshot
    $serverMatchesInitial = [bool]$finalServer.exists -and [bool]$finalServer.pipePresent -and
      $finalServer.processId -eq $initialServer.processId -and
      $finalServer.startTimeUtc -eq $initialServer.startTimeUtc
    $iterationPassed = -not $childError -and $null -ne $appEvidence -and
      $failedFields.Count -eq 0 -and $appErrors.Count -eq 0 -and
      $applicationProcessExited -and $serverMatchesInitial

    $iterationRecord = [ordered]@{
      number = $iterationNumber
      startedUtc = $iterationStartedUtc
      completedUtc = (Get-Date).ToUniversalTime().ToString('o')
      elapsedSeconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
      evidencePath = $iterationEvidencePath
      candidateScreenshotPath = if ($null -ne $appEvidence) { [string]$appEvidence.CandidateScreenshotPath } else { $null }
      applicationProcessId = if ($null -ne $appEvidence) { [int]$appEvidence.ApplicationProcessId } else { 0 }
      applicationProcessExited = $applicationProcessExited
      candidateChangedPixels = if ($null -ne $appEvidence) { [int]$appEvidence.CandidateChangedPixels } else { 0 }
      committedText = if ($null -ne $appEvidence) { [string]$appEvidence.CommittedText } else { $null }
      finalText = if ($null -ne $appEvidence) { [string]$appEvidence.FinalText } else { $null }
      failedFields = @($failedFields)
      appErrors = @($appErrors)
      childError = $childError
      server = $finalServer
      passed = $iterationPassed
    }
    $iterationRecords.Add($iterationRecord)

    if (-not $iterationPassed) {
      $failure = "Iteration $iterationNumber failed: child=$childError fields=$($failedFields -join ',') appErrors=$($appErrors -join ';') appProcessExited=$applicationProcessExited serverStable=$serverMatchesInitial"
      $failures.Add($failure)
      [void](Write-StabilitySummary -Complete:$false)
      break
    }

    [void](Write-StabilitySummary -Complete:$false)
    Write-Host ('iteration={0} elapsed={1:n1}s serverPid={2} privateMiB={3:n1} handles={4}' -f `
      $iterationNumber,
      $clock.Elapsed.TotalSeconds,
      $finalServer.processId,
      ($finalServer.privateMemoryBytes / 1MB),
      $finalServer.handleCount)

    if ($clock.Elapsed.TotalSeconds -lt $targetDurationSeconds -and $IterationPauseMilliseconds -gt 0) {
      Start-Sleep -Milliseconds $IterationPauseMilliseconds
    }
  } while ($clock.Elapsed.TotalSeconds -lt $targetDurationSeconds)
} catch {
  $failures.Add($_.Exception.ToString())
} finally {
  $clock.Stop()
  $finalServer = Get-ContextIMEServerSnapshot
}

$finalSummary = Write-StabilitySummary -Complete:$true
$finalSummary | ConvertTo-Json -Depth 12
if (-not $finalSummary.passed) {
  throw "ContextIME stability test failed. Evidence: $EvidencePath"
}

Write-Host "ContextIME stability evidence: $EvidencePath"
