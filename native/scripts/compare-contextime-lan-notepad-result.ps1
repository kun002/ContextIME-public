[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$LocalStabilityPath,
  [Parameter(Mandatory = $true)][string]$LanRunDirectory,
  [string]$OutputPath,
  [switch]$ScreenshotsReviewed,
  [switch]$ResourceTrendReviewed
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$LocalStabilityPath = [System.IO.Path]::GetFullPath($LocalStabilityPath)
$LanRunDirectory = [System.IO.Path]::GetFullPath($LanRunDirectory)
if (-not (Test-Path -LiteralPath $LocalStabilityPath -PathType Leaf)) { throw "Local stability JSON not found: $LocalStabilityPath" }
if (-not (Test-Path -LiteralPath $LanRunDirectory -PathType Container)) { throw "LAN run directory not found: $LanRunDirectory" }
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $LanRunDirectory 'lan-notepad-comparison.json' }
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

$lanResultPath = Join-Path $LanRunDirectory 'result.json'
$lanStabilityPath = Join-Path $LanRunDirectory 'contextime-stability.json'
$resourceBeforePath = Join-Path $LanRunDirectory 'resource-before.json'
$resourceAfterPath = Join-Path $LanRunDirectory 'resource-after.json'
foreach ($requiredPath in @($lanResultPath, $lanStabilityPath, $resourceBeforePath, $resourceAfterPath)) {
  if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) { throw "Required LAN evidence not found: $requiredPath" }
}

$local = [System.IO.File]::ReadAllText($LocalStabilityPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$remote = [System.IO.File]::ReadAllText($lanStabilityPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$lanResult = [System.IO.File]::ReadAllText($lanResultPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$resourceBefore = [System.IO.File]::ReadAllText($resourceBeforePath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$resourceAfter = [System.IO.File]::ReadAllText($resourceAfterPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
$localIterationDirectory = "$LocalStabilityPath.iterations"
$remoteIterationDirectory = "$lanStabilityPath.iterations"
foreach ($requiredDirectory in @($localIterationDirectory, $remoteIterationDirectory)) {
  if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) { throw "Iteration evidence directory not found: $requiredDirectory" }
}

$requiredFields = @(
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

function Read-IterationEvidence {
  param([Parameter(Mandatory = $true)][string]$Directory)

  return @(Get-ChildItem -LiteralPath $Directory -File -Filter '*-notepad.json' | Sort-Object Name | ForEach-Object {
    [ordered]@{
      name = $_.Name
      path = $_.FullName
      evidence = [System.IO.File]::ReadAllText($_.FullName, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
    }
  })
}

function Test-ObjectProperty {
  param(
    [Parameter(Mandatory = $true)]$Value,
    [Parameter(Mandatory = $true)][string]$Name
  )

  return $null -ne $Value.PSObject.Properties[$Name]
}

function Test-StabilityEvidence {
  param(
    [Parameter(Mandatory = $true)][string]$Label,
    [Parameter(Mandatory = $true)]$Summary,
    [Parameter(Mandatory = $true)][object[]]$Iterations
  )

  $failures = [System.Collections.Generic.List[string]]::new()
  if ([string]$Summary.schemaVersion -ne 'contextime.stability.v1' -or [bool]$Summary.inProgress -or
      [string]::IsNullOrWhiteSpace([string]$Summary.completedUtc)) {
    $failures.Add("$Label stability summary is not a completed v1 document.")
  }
  if (-not [bool]$Summary.passed) { $failures.Add("$Label summary passed is false.") }
  if (-not [bool]$Summary.durationSatisfied -or [double]$Summary.elapsedSeconds -lt [double]$Summary.targetDurationSeconds) {
    $failures.Add("$Label did not satisfy its target duration.")
  }
  if (-not [bool]$Summary.serverStable) { $failures.Add("$Label Server PID/start time/pipe stability failed.") }
  if (@($Summary.failures).Count -gt 0) { $failures.Add("$Label summary failures is not empty.") }
  if (-not (Test-ObjectProperty -Value $Summary -Name 'cancellationRequested')) {
    $failures.Add("$Label summary predates the cancellation evidence contract and must be rerun.")
  } elseif ([bool]$Summary.cancellationRequested) {
    $failures.Add("$Label summary recorded cancellation.")
  }
  if ($Iterations.Count -ne [int]$Summary.iterationCount -or $Iterations.Count -le 0) {
    $failures.Add("$Label child iteration count does not match the summary.")
  }
  if ([int]$Summary.successfulIterationCount -ne $Iterations.Count -or @($Summary.iterations).Count -ne $Iterations.Count) {
    $failures.Add("$Label successful or embedded iteration count does not match child evidence.")
  }

  for ($index = 0; $index -lt $Iterations.Count; $index++) {
    $child = $Iterations[$index].evidence
    foreach ($field in $requiredFields) {
      if (-not [bool]$child.$field) { $failures.Add("$Label iteration $($index + 1) failed field $field.") }
    }
    if (@($child.Errors).Count -gt 0) { $failures.Add("$Label iteration $($index + 1) Errors is not empty.") }
    if ([int]$child.CandidateChangedPixels -le 0) { $failures.Add("$Label iteration $($index + 1) has no candidate pixel change.") }
    if (-not (Test-ObjectProperty -Value $child -Name 'CleanupForegroundAcquireAttempts') -or
        [int]$child.CleanupForegroundAcquireAttempts -lt 1 -or [int]$child.CleanupForegroundAcquireAttempts -gt 3) {
      $failures.Add("$Label iteration $($index + 1) cleanup foreground attempts are outside the contract.")
    }
    if ([string]$child.CommittedText -ne ([string]::Concat([char]0x8F93, [char]0x5165, [char]0x6CD5)) -or
        [string]$child.FinalText -ne ([string]::Concat([char]0x8F93, [char]0x5165, [char]0x6CD5) + 'abc')) {
      $failures.Add("$Label iteration $($index + 1) text contract mismatched.")
    }
  }

  $processExitFailures = 0
  $processExitEvidenceMissing = 0
  foreach ($embedded in @($Summary.iterations)) {
    if (-not (Test-ObjectProperty -Value $embedded -Name 'applicationProcessExited')) {
      $processExitEvidenceMissing++
    } elseif (-not [bool]$embedded.applicationProcessExited) {
      $processExitFailures++
    }
  }
  if ($processExitEvidenceMissing -gt 0) {
    $failures.Add("$Label has $processExitEvidenceMissing iterations that predate the Notepad process-exit contract and must be rerun.")
  }
  if ($processExitFailures -gt 0) { $failures.Add("$Label has $processExitFailures iterations whose Notepad process did not exit.") }
  for ($index = 0; $index -lt @($Summary.iterations).Count; $index++) {
    $embedded = @($Summary.iterations)[$index]
    if ([int]$embedded.number -ne ($index + 1) -or -not [bool]$embedded.passed -or
        @($embedded.failedFields).Count -ne 0 -or @($embedded.appErrors).Count -ne 0 -or
        -not [string]::IsNullOrWhiteSpace([string]$embedded.childError) -or
        -not [bool]$embedded.server.exists -or -not [bool]$embedded.server.pipePresent) {
      $failures.Add("$Label embedded iteration $($index + 1) does not satisfy its process/server/error contract.")
    }
  }

  return [ordered]@{
    label = $Label
    iterationCount = $Iterations.Count
    elapsedSeconds = [double]$Summary.elapsedSeconds
    targetDurationSeconds = [double]$Summary.targetDurationSeconds
    repositoryCommit = [string]$Summary.repositoryCommit
    privateMemoryDeltaBytes = [long]$Summary.privateMemoryDeltaBytes
    peakPrivateMemoryBytes = [long]$Summary.peakPrivateMemoryBytes
    handleCountDelta = [int]$Summary.handleCountDelta
    peakHandleCount = [int]$Summary.peakHandleCount
    failureCount = $failures.Count
    failures = @($failures)
    passed = $failures.Count -eq 0
  }
}

$localIterations = Read-IterationEvidence -Directory $localIterationDirectory
$remoteIterations = Read-IterationEvidence -Directory $remoteIterationDirectory
$localAudit = Test-StabilityEvidence -Label 'local' -Summary $local -Iterations $localIterations
$remoteAudit = Test-StabilityEvidence -Label 'lan' -Summary $remote -Iterations $remoteIterations
$crossFailures = [System.Collections.Generic.List[string]]::new()
if ([string]$lanResult.schemaVersion -ne 'contextime.lan-result.v1') {
  $crossFailures.Add('The LAN node result schema is invalid.')
}
if ([string]$local.repositoryCommit -ne [string]$remote.repositoryCommit) {
  $crossFailures.Add('Local and LAN stability evidence use different repository commits.')
}
if ([string]$remote.repositoryCommit -ne [string]$lanResult.package.repositoryCommit) {
  $crossFailures.Add('LAN stability evidence and the synchronized package use different repository commits.')
}
if ([string]$local.application -ne 'notepad' -or [string]$remote.application -ne 'notepad') {
  $crossFailures.Add('Both sides must be Notepad stability evidence.')
}
if (-not [bool]$lanResult.passed -or [string]$lanResult.status -ne 'passed') {
  $crossFailures.Add('The LAN node result envelope did not pass.')
}
if (@($lanResult.errors).Count -ne 0 -or -not [bool]$lanResult.interactiveSession.passed -or
    [bool]$lanResult.timedOut -or [bool]$lanResult.cancelled -or [bool]$lanResult.forcedTermination) {
  $crossFailures.Add('The LAN node result contains errors, a rejected session, timeout, cancellation, or forced termination.')
}
if (-not [bool]$resourceBefore.interactiveSession.passed -or -not [bool]$resourceAfter.interactiveSession.passed) {
  $crossFailures.Add('The LAN resource snapshots were not captured in an accepted interactive session.')
}
if (-not [bool]$resourceBefore.contextIMEServer.exists -or -not [bool]$resourceBefore.contextIMEServer.pipePresent -or
    -not [bool]$resourceAfter.contextIMEServer.exists -or -not [bool]$resourceAfter.contextIMEServer.pipePresent -or
    [int]$resourceBefore.contextIMEServer.processId -ne [int]$resourceAfter.contextIMEServer.processId -or
    [string]$resourceBefore.contextIMEServer.startTimeUtc -ne [string]$resourceAfter.contextIMEServer.startTimeUtc) {
  $crossFailures.Add('The LAN outer resource snapshots do not preserve the ContextIME Server PID/start time/pipe contract.')
}
if (@($resourceBefore.notepadProcesses).Count -ne 0 -or @($resourceAfter.notepadProcesses).Count -ne 0) {
  $crossFailures.Add('The LAN resource snapshots contain a Notepad process outside the isolated loop.')
}
if ([int]$resourceBefore.contextIMEServer.processId -ne [int]$remote.initialServer.processId -or
    [int]$resourceAfter.contextIMEServer.processId -ne [int]$remote.finalServer.processId) {
  $crossFailures.Add('The LAN outer and stability resource snapshots refer to different ContextIME Server processes.')
}

$localScreenshots = @(Get-ChildItem -LiteralPath $localIterationDirectory -File -Filter '*.candidate.png').Count
$remoteScreenshots = @(Get-ChildItem -LiteralPath $remoteIterationDirectory -File -Filter '*.candidate.png').Count
if ($localScreenshots -ne $localIterations.Count) { $crossFailures.Add('Local candidate screenshot count does not match its iterations.') }
if ($remoteScreenshots -ne $remoteIterations.Count) { $crossFailures.Add('LAN candidate screenshot count does not match its iterations.') }

$mechanicalPassed = [bool]$localAudit.passed -and [bool]$remoteAudit.passed -and $crossFailures.Count -eq 0
$manualReviewPassed = [bool]$ScreenshotsReviewed -and [bool]$ResourceTrendReviewed
$passed = $mechanicalPassed -and $manualReviewPassed
$comparison = [ordered]@{
  schemaVersion = 'contextime.lan-notepad-comparison.v1'
  generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
  localStabilityPath = $LocalStabilityPath
  lanRunDirectory = $LanRunDirectory
  local = $localAudit
  lan = $remoteAudit
  crossFailureCount = $crossFailures.Count
  crossFailures = @($crossFailures)
  localCandidateScreenshotCount = $localScreenshots
  lanCandidateScreenshotCount = $remoteScreenshots
  lanOuterResource = [ordered]@{
    privateMemoryDeltaBytes = [long]$resourceAfter.contextIMEServer.privateMemoryBytes - [long]$resourceBefore.contextIMEServer.privateMemoryBytes
    handleCountDelta = [int]$resourceAfter.contextIMEServer.handleCount - [int]$resourceBefore.contextIMEServer.handleCount
  }
  mechanicalPassed = $mechanicalPassed
  screenshotsReviewed = [bool]$ScreenshotsReviewed
  resourceTrendReviewed = [bool]$ResourceTrendReviewed
  manualReviewRequired = -not $manualReviewPassed
  passed = $passed
}
[System.IO.File]::WriteAllText($OutputPath, ($comparison | ConvertTo-Json -Depth 12), [System.Text.UTF8Encoding]::new($false))
$comparison | ConvertTo-Json -Depth 12
if (-not $passed) {
  if ($mechanicalPassed -and -not $manualReviewPassed) {
    throw "Mechanical LAN comparison passed, but representative candidate screenshots and resource trends still require review. Re-run with -ScreenshotsReviewed -ResourceTrendReviewed after review. Evidence: $OutputPath"
  }
  throw "LAN Notepad comparison failed. Evidence: $OutputPath"
}
