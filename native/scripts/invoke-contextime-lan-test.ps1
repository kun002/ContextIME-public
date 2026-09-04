[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$NodeName,
  [Parameter(Mandatory = $true)][string]$PackagePath,
  [string]$ShareName = 'ContextIMELanTest$',
  [string]$TaskPath = '\ContextIME\',
  [string]$TaskName = 'LAN Test Node',
  [ValidateRange(1, 480)][int]$DurationMinutes = 30,
  [ValidateRange(0, 28800)][int]$DurationSeconds = 0,
  [ValidateRange(5, 120)][int]$SmokeTimeoutSeconds = 30,
  [ValidateRange(0, 10000)][int]$IterationPauseMilliseconds = 250,
  [ValidateRange(30, 1800)][int]$ControlPlaneGraceSeconds = 180,
  [ValidateRange(10, 300)][int]$CancellationGraceSeconds = 60,
  [string]$ServerPath,
  [string]$EvidenceRoot,
  [switch]$AllowDirtyHarness,
  [switch]$KeepTaskRunningOnTimeout
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'The ContextIME LAN controller requires Windows.' }
if ($NodeName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,254}$') { throw "Invalid node name: $NodeName" }
if ($ShareName -notmatch '^[A-Za-z0-9_$-]{3,80}$') { throw "Invalid SMB share name: $ShareName" }

function Test-PathWithinRoot {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$Root
  )

  $resolvedPath = [System.IO.Path]::GetFullPath($Path)
  $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
  return $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-RelativePath {
  param(
    [Parameter(Mandatory = $true)][string]$BasePath,
    [Parameter(Mandatory = $true)][string]$Path
  )

  $base = [System.IO.Path]::GetFullPath($BasePath).TrimEnd('\') + '\'
  $target = [System.IO.Path]::GetFullPath($Path)
  return [Uri]::UnescapeDataString(([Uri]::new($base)).MakeRelativeUri([Uri]::new($target)).ToString()).Replace('/', '\')
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$PackagePath = [System.IO.Path]::GetFullPath($PackagePath)
if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw "LAN package not found: $PackagePath" }
if ([System.IO.Path]::GetExtension($PackagePath) -ne '.zip') { throw 'The LAN package must be a .zip file.' }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
  $EvidenceRoot = Join-Path $repositoryRoot 'artifacts\native-evidence\lan-node'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($PackagePath)
try {
  $manifestEntry = $archive.GetEntry('manifest.json')
  if ($null -eq $manifestEntry) { throw 'The LAN package has no root manifest.json.' }
  $reader = [System.IO.StreamReader]::new($manifestEntry.Open(), [System.Text.Encoding]::UTF8)
  try { $manifest = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
} finally {
  $archive.Dispose()
}
if ([string]$manifest.schemaVersion -ne 'contextime.lan-package.v1') { throw "Unsupported LAN package schema: $($manifest.schemaVersion)" }
if ([string]$manifest.testKind -ne 'notepad_stability') { throw "Phase 1 only supports notepad_stability packages: $($manifest.testKind)" }
if ([string]$manifest.entryPoint -ne 'native\scripts\run-contextime-stability.ps1' -or
    [string]$manifest.repositoryCommit -notmatch '^[0-9a-fA-F]{40}$') {
  throw 'The LAN package entry point or repository commit is invalid.'
}
if (-not [bool]$manifest.harnessFilesClean -and -not $AllowDirtyHarness) {
  throw 'The LAN package contains uncommitted harness files. Use a clean package for acceptance, or pass -AllowDirtyHarness for development only.'
}

$packageId = [string]$manifest.packageId
if ($packageId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{7,127}$') { throw "Invalid package id: $packageId" }
$packageSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PackagePath).Hash.ToLowerInvariant()
$targetDurationSeconds = if ($DurationSeconds -gt 0) { $DurationSeconds } else { $DurationMinutes * 60 }
$nodeTestTimeoutSeconds = $targetDurationSeconds + $ControlPlaneGraceSeconds
$controllerTimeoutSeconds = $nodeTestTimeoutSeconds + 60
$runId = '{0}-{1}-{2}' -f $env:COMPUTERNAME.ToLowerInvariant(), (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ'), ([guid]::NewGuid().ToString('N').Substring(0, 8))

$exchangeRoot = "\\$NodeName\$ShareName"
$remotePackageRoot = Join-Path $exchangeRoot 'packages'
$remoteRequestRoot = Join-Path $exchangeRoot 'requests'
$remoteRunRoot = Join-Path $exchangeRoot 'runs'
foreach ($path in @($exchangeRoot, $remotePackageRoot, $remoteRequestRoot, $remoteRunRoot)) {
  if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "LAN node exchange path is unavailable: $path" }
}

$nodeInfoPath = Join-Path $exchangeRoot 'node-info.json'
if (-not (Test-Path -LiteralPath $nodeInfoPath -PathType Leaf)) { throw "LAN node-info.json is missing: $nodeInfoPath" }
$nodeInfo = [System.IO.File]::ReadAllText($nodeInfoPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
if ([string]$nodeInfo.schemaVersion -ne 'contextime.lan-node-install.v1') {
  throw "Unsupported LAN node-info schema: $($nodeInfo.schemaVersion)"
}
$parsedNodeAddress = $null
$nodeNameIsAddress = [System.Net.IPAddress]::TryParse($NodeName, [ref]$parsedNodeAddress)
if (-not $nodeNameIsAddress -and [string]$nodeInfo.computerName -ne $NodeName -and
    [string]$nodeInfo.computerName -ne $NodeName.Split('.')[0]) {
  throw "The SMB exchange belongs to $($nodeInfo.computerName), not $NodeName."
}
if ([string]$nodeInfo.logonType -notmatch 'Interactive') {
  throw "The node task is not registered with InteractiveToken: $($nodeInfo.logonType)"
}
if ([string]$nodeInfo.runLevel -ne 'Limited') {
  throw "The node task is not registered with the limited run level: $($nodeInfo.runLevel)"
}
$expectedWorkerPath = [System.IO.Path]::GetFullPath((Join-Path ([string]$nodeInfo.nodeRoot) 'worker\ContextIMELanNodeWorker.ps1'))
if ([string]$nodeInfo.workerPath -ne $expectedWorkerPath -or
    [string]::IsNullOrWhiteSpace([string]$nodeInfo.taskCommand) -or
    [string]::IsNullOrWhiteSpace([string]$nodeInfo.taskArguments)) {
  throw 'node-info.json does not describe the exact installed worker action. Reinstall the LAN node.'
}

$taskFullName = ($TaskPath.TrimEnd('\') + '\' + $TaskName)
$taskQueryOutput = @(& schtasks.exe /Query /S $NodeName /TN $taskFullName /XML 2>&1)
if ($LASTEXITCODE -ne 0) {
  throw "Remote scheduled-task query failed with exit code $LASTEXITCODE.`n$($taskQueryOutput -join "`n")"
}
try {
  [xml]$taskXml = $taskQueryOutput -join "`r`n"
} catch {
  throw "Remote scheduled-task XML could not be parsed: $($_.Exception.Message)"
}
$logonTypeNode = $taskXml.SelectSingleNode("//*[local-name()='LogonType']")
$runLevelNode = $taskXml.SelectSingleNode("//*[local-name()='RunLevel']")
$execNodes = @($taskXml.SelectNodes("//*[local-name()='Exec']"))
$commandNode = if ($execNodes.Count -eq 1) { $execNodes[0].SelectSingleNode("./*[local-name()='Command']") } else { $null }
$argumentsNode = if ($execNodes.Count -eq 1) { $execNodes[0].SelectSingleNode("./*[local-name()='Arguments']") } else { $null }
$liveLogonType = if ($null -eq $logonTypeNode) { '<missing>' } else { [string]$logonTypeNode.InnerText }
if ($liveLogonType -ne 'InteractiveToken') {
  throw "The live remote task is not InteractiveToken: $liveLogonType"
}
$liveRunLevel = if ($null -eq $runLevelNode) { 'LeastPrivilege' } else { [string]$runLevelNode.InnerText }
if ($liveRunLevel -ne 'LeastPrivilege') {
  throw "The live remote task is not limited/least privilege: $liveRunLevel"
}
$liveCommand = if ($null -eq $commandNode) { '' } else { [string]$commandNode.InnerText }
$liveArguments = if ($null -eq $argumentsNode) { '' } else { [string]$argumentsNode.InnerText }
if ($execNodes.Count -ne 1 -or
    -not $liveCommand.Equals([string]$nodeInfo.taskCommand, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not $liveArguments.Equals([string]$nodeInfo.taskArguments, [System.StringComparison]::Ordinal)) {
  throw 'The live remote task action does not exactly match the installed ContextIME LAN node worker.'
}
if ($liveArguments.IndexOf("-File `"$expectedWorkerPath`"", [System.StringComparison]::OrdinalIgnoreCase) -lt 0 -or
    $liveArguments.IndexOf("-NodeRoot `"$([string]$nodeInfo.nodeRoot)`"", [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
  throw 'The live remote task action does not target the expected worker and node root.'
}

$activeRequests = @(Get-ChildItem -LiteralPath $remoteRequestRoot -File | Where-Object {
  $_.Name -match '\.(request|running)\.json$'
})
if ($activeRequests.Count -gt 0) {
  throw "The LAN node already has an active request. Wait for it to finish or inspect: $($activeRequests.Name -join ', ')"
}

$remotePackagePath = Join-Path $remotePackageRoot "$packageId.zip"
if (Test-Path -LiteralPath $remotePackagePath -PathType Leaf) {
  $remoteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $remotePackagePath).Hash.ToLowerInvariant()
  if ($remoteHash -ne $packageSha256) { throw "A different package already uses id $packageId on the node." }
} else {
  $uploadPath = "$remotePackagePath.uploading-$PID"
  if (Test-Path -LiteralPath $uploadPath) { throw "A stale package upload path exists: $uploadPath" }
  Copy-Item -LiteralPath $PackagePath -Destination $uploadPath
  $uploadHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $uploadPath).Hash.ToLowerInvariant()
  if ($uploadHash -ne $packageSha256) { throw 'The synchronized package SHA-256 does not match the local package.' }
  Move-Item -LiteralPath $uploadPath -Destination $remotePackagePath
}

$remoteRunDirectory = Join-Path $remoteRunRoot $runId
$localRunDirectory = Join-Path $EvidenceRoot $runId
if ((Test-Path -LiteralPath $remoteRunDirectory) -or (Test-Path -LiteralPath $localRunDirectory)) {
  throw "The LAN run id already exists: $runId"
}
$request = [ordered]@{
  schemaVersion = 'contextime.lan-request.v1'
  runId = $runId
  createdUtc = (Get-Date).ToUniversalTime().ToString('o')
  requestedByComputer = $env:COMPUTERNAME
  node = [ordered]@{
    computerName = [string]$nodeInfo.computerName
    requiredInteractiveLogonType = 'InteractiveToken'
  }
  package = [ordered]@{
    id = $packageId
    relativePath = "packages\$packageId.zip"
    sha256 = $packageSha256
    repositoryCommit = [string]$manifest.repositoryCommit
    harnessFilesClean = [bool]$manifest.harnessFilesClean
  }
  test = [ordered]@{
    kind = 'notepad_stability'
    durationSeconds = $targetDurationSeconds
    timeoutSeconds = $nodeTestTimeoutSeconds
    smokeTimeoutSeconds = $SmokeTimeoutSeconds
    iterationPauseMilliseconds = $IterationPauseMilliseconds
    serverPath = if ([string]::IsNullOrWhiteSpace($ServerPath)) { $null } else { $ServerPath }
  }
}
$requestPath = Join-Path $remoteRequestRoot "$runId.request.json"
$temporaryRequestPath = "$requestPath.uploading-$PID"
$requestJson = $request | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($temporaryRequestPath, $requestJson, [System.Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $temporaryRequestPath -Destination $requestPath

$triggerOutput = @(& schtasks.exe /Run /S $NodeName /TN $taskFullName 2>&1)
if ($LASTEXITCODE -ne 0) {
  $cancelPath = Join-Path $remoteRequestRoot "$runId.cancel"
  $cancelTemporaryPath = "$cancelPath.$PID.tmp"
  [System.IO.File]::WriteAllText($cancelTemporaryPath, (Get-Date).ToUniversalTime().ToString('o'), [System.Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath $cancelTemporaryPath -Destination $cancelPath -Force
  throw "Remote scheduled-task trigger failed with exit code $LASTEXITCODE.`n$($triggerOutput -join "`n")"
}

$controllerStartedUtc = (Get-Date).ToUniversalTime().ToString('o')
$resultPath = Join-Path $remoteRunDirectory 'result.json'
$deadline = [datetime]::UtcNow.AddSeconds($controllerTimeoutSeconds)
while ([datetime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
  Start-Sleep -Seconds 2
}

$controllerTimedOut = -not (Test-Path -LiteralPath $resultPath -PathType Leaf)
if ($controllerTimedOut) {
  $cancelPath = Join-Path $remoteRequestRoot "$runId.cancel"
  $cancelTemporaryPath = "$cancelPath.$PID.tmp"
  [System.IO.File]::WriteAllText($cancelTemporaryPath, (Get-Date).ToUniversalTime().ToString('o'), [System.Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath $cancelTemporaryPath -Destination $cancelPath -Force
  $cancelDeadline = [datetime]::UtcNow.AddSeconds($CancellationGraceSeconds)
  while ([datetime]::UtcNow -lt $cancelDeadline -and -not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
    Start-Sleep -Seconds 2
  }
  if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf) -and -not $KeepTaskRunningOnTimeout) {
    $endOutput = @(& schtasks.exe /End /S $NodeName /TN $taskFullName 2>&1)
    $endExitCode = $LASTEXITCODE
  } else {
    $endOutput = @()
    $endExitCode = $null
  }
}

if (Test-Path -LiteralPath $remoteRunDirectory -PathType Container) {
  Copy-Item -LiteralPath $remoteRunDirectory -Destination $localRunDirectory -Recurse
}

if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
  $timeoutEvidence = [ordered]@{
    schemaVersion = 'contextime.lan-controller-timeout.v1'
    runId = $runId
    nodeName = $NodeName
    controllerStartedUtc = $controllerStartedUtc
    completedUtc = (Get-Date).ToUniversalTime().ToString('o')
    timeoutSeconds = $controllerTimeoutSeconds
    cancellationRequested = $true
    scheduledTaskEndExitCode = $endExitCode
    scheduledTaskEndOutput = @($endOutput)
    remoteRunDirectory = $remoteRunDirectory
    localRunDirectory = if (Test-Path -LiteralPath $localRunDirectory) { $localRunDirectory } else { $null }
    passed = $false
  }
  if (-not (Test-Path -LiteralPath $localRunDirectory)) { New-Item -ItemType Directory -Path $localRunDirectory | Out-Null }
  [System.IO.File]::WriteAllText((Join-Path $localRunDirectory 'controller-timeout.json'), ($timeoutEvidence | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
  $timeoutEvidence | ConvertTo-Json -Depth 8
  throw "The LAN node did not publish result.json before the controller timeout. Evidence: $localRunDirectory"
}

if (-not (Test-Path -LiteralPath (Join-Path $localRunDirectory 'result.json') -PathType Leaf)) {
  throw "The remote result existed but was not copied locally: $localRunDirectory"
}
$localResult = [System.IO.File]::ReadAllText((Join-Path $localRunDirectory 'result.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
if ([string]$localResult.schemaVersion -ne 'contextime.lan-result.v1' -or [string]$localResult.runId -ne $runId) {
  throw 'The copied LAN result schema or run id is invalid.'
}
if ([string]$localResult.workerSha256 -ne [string]$nodeInfo.workerSha256) {
  throw 'The worker SHA-256 in the LAN result does not match node-info.json.'
}
if ([string]$localResult.computerName -ne [string]$nodeInfo.computerName -or
    [string]$localResult.package.id -ne $packageId -or
    [string]$localResult.package.sha256 -ne $packageSha256 -or
    [string]$localResult.package.repositoryCommit -ne [string]$manifest.repositoryCommit) {
  throw 'The copied LAN result node or package identity does not match the request.'
}
if ([string]$localResult.artifactManifest -ne 'artifacts-manifest.json') {
  throw 'The copied LAN result references an unexpected artifact manifest path.'
}
$artifactManifestPath = Join-Path $localRunDirectory 'artifacts-manifest.json'
if (-not (Test-Path -LiteralPath $artifactManifestPath -PathType Leaf)) { throw 'The copied LAN result has no artifact manifest.' }
$artifactManifest = [System.IO.File]::ReadAllText($artifactManifestPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
if ([string]$artifactManifest.schemaVersion -ne 'contextime.lan-artifacts.v1') {
  throw "Unsupported LAN artifact manifest schema: $($artifactManifest.schemaVersion)"
}
$manifestPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in @($artifactManifest.files)) {
  $relativePath = ([string]$file.path).Replace('/', '\')
  if ([string]::IsNullOrWhiteSpace($relativePath) -or [System.IO.Path]::IsPathRooted($relativePath) -or
      $relativePath -in @('result.json', 'artifacts-manifest.json') -or -not $manifestPaths.Add($relativePath)) {
    throw "The copied LAN artifact manifest contains an invalid or duplicate path: $relativePath"
  }
  $artifactPath = Join-Path $localRunDirectory $relativePath
  if (-not (Test-PathWithinRoot -Path $artifactPath -Root $localRunDirectory)) {
    throw "A copied LAN artifact path escapes its run directory: $relativePath"
  }
  if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) { throw "A copied LAN artifact is missing: $($file.path)" }
  $item = Get-Item -LiteralPath $artifactPath
  if ([long]$item.Length -ne [long]$file.length) { throw "A copied LAN artifact length mismatched: $($file.path)" }
  $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash.ToLowerInvariant()
  if ($hash -ne ([string]$file.sha256).ToLowerInvariant()) { throw "A copied LAN artifact SHA-256 mismatched: $($file.path)" }
}
$actualArtifactPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $localRunDirectory -File -Recurse | Where-Object {
  $_.FullName -notin @((Join-Path $localRunDirectory 'result.json'), $artifactManifestPath)
} | ForEach-Object {
  [void]$actualArtifactPaths.Add((Get-RelativePath -BasePath $localRunDirectory -Path $_.FullName))
}
if ($manifestPaths.Count -eq 0 -or $manifestPaths.Count -ne $actualArtifactPaths.Count) {
  throw 'The copied LAN artifact manifest does not enumerate the complete run directory.'
}
foreach ($actualPath in $actualArtifactPaths) {
  if (-not $manifestPaths.Contains($actualPath)) {
    throw "A copied LAN artifact is not covered by the manifest: $actualPath"
  }
}

if ([bool]$localResult.passed) {
  foreach ($requiredArtifact in @(
    'interactive-session.json',
    'node-status.json',
    'resource-before.json',
    'resource-after.json',
    'test.stdout.log',
    'test.stderr.log',
    'contextime-stability.json'
  )) {
    if (-not $manifestPaths.Contains($requiredArtifact)) { throw "The passing LAN result is missing required evidence: $requiredArtifact" }
  }
  if ([string]$localResult.status -ne 'passed' -or -not [bool]$localResult.stabilityPassed -or
      -not [bool]$localResult.interactiveSession.passed -or [bool]$localResult.timedOut -or
      [bool]$localResult.cancelled -or [bool]$localResult.forcedTermination -or
      [int]$localResult.testExitCode -ne 0 -or @($localResult.errors).Count -ne 0) {
    throw 'The passing LAN result contains a failed status, session, timeout, cancellation, exit code, or error field.'
  }
  if (@($manifestPaths | Where-Object { $_ -like 'contextime-stability.json.iterations\*-notepad.json' }).Count -eq 0 -or
      @($manifestPaths | Where-Object { $_ -like 'contextime-stability.json.iterations\*.candidate.png' }).Count -eq 0) {
    throw 'The passing LAN result has no Notepad iteration JSON or candidate screenshots.'
  }
}

$summary = [ordered]@{
  schemaVersion = 'contextime.lan-controller-result.v1'
  runId = $runId
  nodeName = $NodeName
  packageId = $packageId
  packageSha256 = $packageSha256
  repositoryCommit = [string]$manifest.repositoryCommit
  workerSha256 = [string]$localResult.workerSha256
  controllerStartedUtc = $controllerStartedUtc
  completedUtc = (Get-Date).ToUniversalTime().ToString('o')
  remoteStatus = [string]$localResult.status
  artifactCount = @($artifactManifest.files).Count
  localEvidencePath = $localRunDirectory
  passed = [bool]$localResult.passed
}
$summary | ConvertTo-Json -Depth 8
if (-not [bool]$localResult.passed) {
  throw "The LAN node test did not pass. Evidence: $localRunDirectory"
}
