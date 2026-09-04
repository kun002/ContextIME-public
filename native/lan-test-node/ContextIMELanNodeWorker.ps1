[CmdletBinding()]
param(
  [string]$NodeRoot = (Join-Path $env:ProgramData 'ContextIME\LanTestNode')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
  throw 'The ContextIME LAN node worker requires Windows.'
}

$ProtocolVersion = 'contextime.lan-node.v1'
$WorkerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
$NodeRoot = [System.IO.Path]::GetFullPath($NodeRoot)
$ExchangeRoot = Join-Path $NodeRoot 'exchange'
$RequestRoot = Join-Path $ExchangeRoot 'requests'
$PackageRoot = Join-Path $ExchangeRoot 'packages'
$RunRoot = Join-Path $ExchangeRoot 'runs'
$WorkRoot = Join-Path $NodeRoot 'work'
$PackageEntryPoint = 'native\scripts\run-contextime-stability.ps1'
$PackageFilePaths = @(
  'native\contextime.identity.json',
  'native\scripts\run-contextime-app-smoke.ps1',
  $PackageEntryPoint,
  'native\tests\app-smoke\ContextIMEAppSmoke.cs',
  'native\upstream.lock.json'
)

foreach ($path in @($ExchangeRoot, $RequestRoot, $PackageRoot, $RunRoot, $WorkRoot, (Join-Path $RequestRoot 'archive'))) {
  New-Item -ItemType Directory -Path $path -Force | Out-Null
}

function Write-JsonAtomic {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)]$Value
  )

  $directory = [System.IO.Path]::GetDirectoryName($Path)
  New-Item -ItemType Directory -Path $directory -Force | Out-Null
  $temporaryPath = "$Path.$PID.tmp"
  $json = $Value | ConvertTo-Json -Depth 16
  [System.IO.File]::WriteAllText($temporaryPath, $json, [System.Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

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
  $baseUri = [Uri]::new($base)
  $targetUri = [Uri]::new($target)
  return [Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/', '\')
}

if ($null -eq ('ContextIME.LanNode.NativeMethods' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace ContextIME.LanNode
{
    public static class NativeMethods
    {
        private const uint DESKTOP_SWITCHDESKTOP = 0x0100;

        [DllImport("wtsapi32.dll", SetLastError = true)]
        private static extern bool WTSQuerySessionInformation(
            IntPtr server,
            int sessionId,
            int infoClass,
            out IntPtr buffer,
            out int bytesReturned);

        [DllImport("wtsapi32.dll")]
        private static extern void WTSFreeMemory(IntPtr buffer);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint desiredAccess);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SwitchDesktop(IntPtr desktop);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool CloseDesktop(IntPtr desktop);

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr FindWindow(string className, string windowName);

        public static int GetConnectState(int sessionId)
        {
            IntPtr buffer;
            int bytes;
            if (!WTSQuerySessionInformation(IntPtr.Zero, sessionId, 8, out buffer, out bytes))
                return -1;
            try
            {
                return bytes >= 4 ? Marshal.ReadInt32(buffer) : -1;
            }
            finally
            {
                WTSFreeMemory(buffer);
            }
        }

        public static bool IsInputDesktopUnlocked()
        {
            IntPtr desktop = OpenInputDesktop(0, false, DESKTOP_SWITCHDESKTOP);
            if (desktop == IntPtr.Zero) return false;
            try
            {
                return SwitchDesktop(desktop);
            }
            finally
            {
                CloseDesktop(desktop);
            }
        }
    }
}
'@
}

function Get-InteractiveSessionState {
  $currentProcess = Get-Process -Id $PID
  $sessionId = [int]$currentProcess.SessionId
  $connectState = [ContextIME.LanNode.NativeMethods]::GetConnectState($sessionId)
  $explorer = @(Get-Process -Name explorer -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sessionId })
  $foregroundWindow = [ContextIME.LanNode.NativeMethods]::GetForegroundWindow()
  [uint32]$foregroundProcessId = 0
  if ($foregroundWindow -ne [IntPtr]::Zero) {
    [void][ContextIME.LanNode.NativeMethods]::GetWindowThreadProcessId($foregroundWindow, [ref]$foregroundProcessId)
  }
  $foregroundSessionId = -1
  if ($foregroundProcessId -gt 0) {
    $foregroundProcess = Get-Process -Id $foregroundProcessId -ErrorAction SilentlyContinue
    if ($null -ne $foregroundProcess) { $foregroundSessionId = [int]$foregroundProcess.SessionId }
  }
  $inputDesktopUnlocked = [ContextIME.LanNode.NativeMethods]::IsInputDesktopUnlocked()
  $passed = $sessionId -gt 0 -and $connectState -eq 0 -and $explorer.Count -gt 0 -and
    $inputDesktopUnlocked -and $foregroundWindow -ne [IntPtr]::Zero -and $foregroundSessionId -eq $sessionId

  return [ordered]@{
    observedUtc = (Get-Date).ToUniversalTime().ToString('o')
    processSessionId = $sessionId
    wtsConnectState = $connectState
    wtsConnectStateName = if ($connectState -eq 0) { 'WTSActive' } else { "state-$connectState" }
    explorerPresentInSession = $explorer.Count -gt 0
    inputDesktopUnlocked = $inputDesktopUnlocked
    foregroundWindowHandle = if ($foregroundWindow -eq [IntPtr]::Zero) { $null } else { '0x{0:x}' -f $foregroundWindow.ToInt64() }
    foregroundProcessId = [int]$foregroundProcessId
    foregroundSessionId = $foregroundSessionId
    passed = $passed
  }
}

function Get-ContextIMEServerSnapshot {
  $window = [ContextIME.LanNode.NativeMethods]::FindWindow('ContextIMEIPCWindow_1.0', 'ContextIMEIPCWindow_1.0')
  if ($window -eq [IntPtr]::Zero) {
    return [ordered]@{
      exists = $false
      processId = 0
      startTimeUtc = $null
      privateMemoryBytes = 0
      workingSetBytes = 0
      handleCount = 0
      responding = $false
      pipePresent = $false
    }
  }

  [uint32]$serverProcessId = 0
  [void][ContextIME.LanNode.NativeMethods]::GetWindowThreadProcessId($window, [ref]$serverProcessId)
  $server = Get-Process -Id $serverProcessId -ErrorAction SilentlyContinue
  if ($null -eq $server) {
    return [ordered]@{
      exists = $false
      processId = [int]$serverProcessId
      startTimeUtc = $null
      privateMemoryBytes = 0
      workingSetBytes = 0
      handleCount = 0
      responding = $false
      pipePresent = $false
    }
  }
  $server.Refresh()
  $pipePresent = @(
    Get-ChildItem -LiteralPath '\\.\pipe\' -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match '(?i)ContextIMENamedPipe$' }
  ).Count -gt 0
  return [ordered]@{
    exists = $true
    processId = [int]$serverProcessId
    startTimeUtc = $server.StartTime.ToUniversalTime().ToString('o')
    privateMemoryBytes = [long]$server.PrivateMemorySize64
    workingSetBytes = [long]$server.WorkingSet64
    handleCount = [int]$server.HandleCount
    responding = [bool]$server.Responding
    pipePresent = $pipePresent
  }
}

function Get-ResourceSnapshot {
  param([Parameter(Mandatory = $true)]$InteractiveSession)

  $os = Get-CimInstance Win32_OperatingSystem
  $notepad = @(Get-Process -Name Notepad -ErrorAction SilentlyContinue | ForEach-Object {
    [ordered]@{
      processId = $_.Id
      sessionId = $_.SessionId
      mainWindowHandle = '0x{0:x}' -f $_.MainWindowHandle.ToInt64()
      mainWindowTitle = $_.MainWindowTitle
      privateMemoryBytes = [long]$_.PrivateMemorySize64
      handleCount = [int]$_.HandleCount
      responding = [bool]$_.Responding
    }
  })
  return [ordered]@{
    observedUtc = (Get-Date).ToUniversalTime().ToString('o')
    computerName = $env:COMPUTERNAME
    windows = [ordered]@{
      caption = [string]$os.Caption
      version = [string]$os.Version
      buildNumber = [string]$os.BuildNumber
      architecture = [string]$os.OSArchitecture
    }
    interactiveSession = $InteractiveSession
    contextIMEServer = Get-ContextIMEServerSnapshot
    notepadProcesses = $notepad
  }
}

function Test-PackageManifest {
  param(
    [Parameter(Mandatory = $true)][string]$Workspace,
    [Parameter(Mandatory = $true)]$Manifest,
    [Parameter(Mandatory = $true)][string]$ExpectedPackageId
  )

  if ([string]$Manifest.schemaVersion -ne 'contextime.lan-package.v1') {
    throw "Unsupported LAN package schema: $($Manifest.schemaVersion)"
  }
  if ([string]$Manifest.packageId -ne $ExpectedPackageId) {
    throw "Package id mismatch: expected $ExpectedPackageId, found $($Manifest.packageId)"
  }
  if ([string]$Manifest.testKind -ne 'notepad_stability') {
    throw "The phase-1 node only accepts notepad_stability packages: $($Manifest.testKind)"
  }
  if ([string]$Manifest.entryPoint -ne $PackageEntryPoint) {
    throw "The phase-1 package entry point must be $PackageEntryPoint."
  }
  if ([string]$Manifest.repositoryCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'The package repository commit is invalid.'
  }

  $manifestFiles = @($Manifest.files)
  if ($manifestFiles.Count -ne $PackageFilePaths.Count) {
    throw "The phase-1 package must contain exactly $($PackageFilePaths.Count) manifest files."
  }
  $expectedPaths = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]$PackageFilePaths,
    [System.StringComparer]::OrdinalIgnoreCase)
  $observedPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
  foreach ($file in $manifestFiles) {
    $relativePath = ([string]$file.path).Replace('/', '\')
    if (-not $expectedPaths.Contains($relativePath) -or -not $observedPaths.Add($relativePath)) {
      throw "Package manifest contains an unexpected or duplicate file: $relativePath"
    }
    $candidate = Join-Path $Workspace $relativePath
    if (-not (Test-PathWithinRoot -Path $candidate -Root $Workspace) -or
        -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      throw "Package manifest references an invalid file: $relativePath"
    }
    $item = Get-Item -LiteralPath $candidate
    if ([long]$item.Length -ne [long]$file.length) {
      throw "Package file length mismatch: $relativePath"
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidate).Hash.ToLowerInvariant()
    if ($hash -ne ([string]$file.sha256).ToLowerInvariant()) {
      throw "Package file SHA-256 mismatch: $relativePath"
    }
  }
  foreach ($expectedPath in $PackageFilePaths) {
    if (-not $observedPaths.Contains($expectedPath)) {
      throw "Package manifest is missing a required file: $expectedPath"
    }
  }
}

function Expand-VerifiedPackageArchive {
  param(
    [Parameter(Mandatory = $true)][string]$ArchivePath,
    [Parameter(Mandatory = $true)][string]$DestinationPath
  )

  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $allowedFiles = [System.Collections.Generic.HashSet[string]]::new(
    [string[]](@('manifest.json') + $PackageFilePaths),
    [System.StringComparer]::OrdinalIgnoreCase)
  $archiveFiles = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
  $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
  try {
    foreach ($entry in $archive.Entries) {
      $relativePath = ([string]$entry.FullName).Replace('/', '\').TrimEnd('\')
      if ([string]::IsNullOrWhiteSpace($relativePath) -or [System.IO.Path]::IsPathRooted($relativePath)) {
        throw "The LAN package contains an invalid archive path: $($entry.FullName)"
      }
      $candidate = Join-Path $DestinationPath $relativePath
      if (-not (Test-PathWithinRoot -Path $candidate -Root $DestinationPath)) {
        throw "The LAN package archive path escapes the workspace: $($entry.FullName)"
      }
      if ([string]::IsNullOrEmpty([string]$entry.Name)) { continue }
      if (-not $allowedFiles.Contains($relativePath) -or -not $archiveFiles.Add($relativePath)) {
        throw "The LAN package contains an unexpected or duplicate archive file: $($entry.FullName)"
      }
    }
  } finally {
    $archive.Dispose()
  }
  if ($archiveFiles.Count -ne $allowedFiles.Count) {
    throw 'The LAN package archive is missing one or more required files.'
  }
  Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DestinationPath
}

function New-ArtifactManifest {
  param([Parameter(Mandatory = $true)][string]$RunDirectory)

  $files = @(Get-ChildItem -LiteralPath $RunDirectory -File -Recurse | Where-Object {
    $_.Name -notin @('result.json', 'artifacts-manifest.json') -and $_.Name -notlike '*.tmp'
  } | Sort-Object FullName | ForEach-Object {
    [ordered]@{
      path = Get-RelativePath -BasePath $RunDirectory -Path $_.FullName
      length = [long]$_.Length
      sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant()
    }
  })
  return [ordered]@{
    schemaVersion = 'contextime.lan-artifacts.v1'
    generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    files = $files
  }
}

$mutex = [System.Threading.Mutex]::new($false, 'Local\ContextIMELanTestNodeWorker')
if (-not $mutex.WaitOne(0)) {
  throw 'Another ContextIME LAN node worker is already running.'
}

$requestFile = $null
$runningRequestPath = $null
$runDirectory = $null
$runId = $null
$request = $null
$result = $null
$errors = [System.Collections.Generic.List[string]]::new()
$exitCode = -1
$timedOut = $false
$forcedTermination = $false
$cancelled = $false
$cancellationPath = $null
$stability = $null
$interactiveSession = $null
$startedUtc = (Get-Date).ToUniversalTime().ToString('o')

try {
  $requestFile = Get-ChildItem -LiteralPath $RequestRoot -File -Filter '*.request.json' |
    Sort-Object LastWriteTimeUtc, Name |
    Select-Object -First 1
  if ($null -eq $requestFile) {
    Write-JsonAtomic -Path (Join-Path $ExchangeRoot 'node-status.json') -Value ([ordered]@{
      schemaVersion = $ProtocolVersion
      status = 'idle'
      observedUtc = (Get-Date).ToUniversalTime().ToString('o')
      computerName = $env:COMPUTERNAME
      workerSha256 = $WorkerSha256
    })
    return
  }

  $request = [System.IO.File]::ReadAllText($requestFile.FullName, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
  if ([string]$request.schemaVersion -ne 'contextime.lan-request.v1') {
    throw "Unsupported LAN request schema: $($request.schemaVersion)"
  }
  $runId = [string]$request.runId
  if ($runId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{7,127}$') {
    throw "Invalid LAN run id: $runId"
  }
  if ([System.IO.Path]::GetFileName($requestFile.FullName) -ne "$runId.request.json") {
    throw 'The LAN request filename does not match its run id.'
  }
  if ([string]$request.node.computerName -ne $env:COMPUTERNAME -or
      [string]$request.node.requiredInteractiveLogonType -ne 'InteractiveToken') {
    throw 'The LAN request targets a different node or execution type.'
  }
  $runningRequestPath = Join-Path $RequestRoot "$runId.running.json"
  Move-Item -LiteralPath $requestFile.FullName -Destination $runningRequestPath
  $runDirectory = Join-Path $RunRoot $runId
  if (Test-Path -LiteralPath $runDirectory) {
    throw "The LAN run directory already exists: $runDirectory"
  }
  New-Item -ItemType Directory -Path $runDirectory | Out-Null
  $cancellationPath = Join-Path $RequestRoot "$runId.cancel"
  if (Test-Path -LiteralPath $cancellationPath -PathType Leaf) {
    $cancelled = $true
    throw 'The LAN request was cancelled before the interactive test started.'
  }

  $interactiveSession = Get-InteractiveSessionState
  Write-JsonAtomic -Path (Join-Path $runDirectory 'interactive-session.json') -Value $interactiveSession
  if (-not [bool]$interactiveSession.passed) {
    throw 'The scheduled task is not running in an active, unlocked interactive desktop session.'
  }

  $before = Get-ResourceSnapshot -InteractiveSession $interactiveSession
  Write-JsonAtomic -Path (Join-Path $runDirectory 'resource-before.json') -Value $before
  if (-not [bool]$before.contextIMEServer.exists -or -not [bool]$before.contextIMEServer.pipePresent) {
    throw 'ContextIME Server or ContextIMENamedPipe is unavailable on the LAN node.'
  }

  if ([string]$request.test.kind -ne 'notepad_stability') {
    throw "The phase-1 node only accepts notepad_stability requests: $($request.test.kind)"
  }
  $durationSeconds = [int]$request.test.durationSeconds
  $testTimeoutSeconds = [int]$request.test.timeoutSeconds
  $smokeTimeoutSeconds = [int]$request.test.smokeTimeoutSeconds
  $iterationPauseMilliseconds = [int]$request.test.iterationPauseMilliseconds
  if ($durationSeconds -lt 1 -or $durationSeconds -gt 28800) { throw 'durationSeconds must be between 1 and 28800.' }
  if ($testTimeoutSeconds -lt ($durationSeconds + 30) -or $testTimeoutSeconds -gt 32400) { throw 'timeoutSeconds must exceed durationSeconds by at least 30 seconds.' }
  if ($smokeTimeoutSeconds -lt 5 -or $smokeTimeoutSeconds -gt 120) { throw 'smokeTimeoutSeconds must be between 5 and 120.' }
  if ($iterationPauseMilliseconds -lt 0 -or $iterationPauseMilliseconds -gt 10000) { throw 'iterationPauseMilliseconds must be between 0 and 10000.' }

  $packageId = [string]$request.package.id
  if ($packageId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{7,127}$') { throw "Invalid package id: $packageId" }
  if ([string]$request.package.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
      [string]$request.package.repositoryCommit -notmatch '^[0-9a-fA-F]{40}$') {
    throw 'The request package SHA-256 or repository commit is invalid.'
  }
  $packagePath = Join-Path $ExchangeRoot ([string]$request.package.relativePath)
  if (-not (Test-PathWithinRoot -Path $packagePath -Root $PackageRoot) -or
      -not (Test-Path -LiteralPath $packagePath -PathType Leaf)) {
    throw 'The request package path is outside the node package root or missing.'
  }
  $runWorkspace = Join-Path $WorkRoot $runId
  if (Test-Path -LiteralPath $runWorkspace) { throw "The LAN workspace already exists: $runWorkspace" }
  New-Item -ItemType Directory -Path $runWorkspace | Out-Null
  $verifiedPackagePath = Join-Path $runWorkspace 'package.zip'
  Copy-Item -LiteralPath $packagePath -Destination $verifiedPackagePath
  $packageHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $verifiedPackagePath).Hash.ToLowerInvariant()
  if ($packageHash -ne ([string]$request.package.sha256).ToLowerInvariant()) {
    throw 'The synchronized package SHA-256 does not match the request.'
  }
  $workspace = Join-Path $runWorkspace 'package'
  New-Item -ItemType Directory -Path $workspace | Out-Null
  Expand-VerifiedPackageArchive -ArchivePath $verifiedPackagePath -DestinationPath $workspace
  $manifestPath = Join-Path $workspace 'manifest.json'
  if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'The LAN package has no manifest.json.' }
  $manifest = [System.IO.File]::ReadAllText($manifestPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
  Test-PackageManifest -Workspace $workspace -Manifest $manifest -ExpectedPackageId $packageId
  if ([string]$manifest.repositoryCommit -ne [string]$request.package.repositoryCommit) {
    throw 'The package repository commit does not match the request.'
  }
  if ([bool]$manifest.harnessFilesClean -ne [bool]$request.package.harnessFilesClean) {
    throw 'The package clean-state marker does not match the request.'
  }

  $entryPoint = Join-Path $workspace ([string]$manifest.entryPoint)
  if (-not (Test-PathWithinRoot -Path $entryPoint -Root $workspace) -or
      -not (Test-Path -LiteralPath $entryPoint -PathType Leaf)) {
    throw 'The LAN package entry point is invalid.'
  }

  $evidencePath = Join-Path $runDirectory 'contextime-stability.json'
  $stdoutPath = Join-Path $runDirectory 'test.stdout.log'
  $stderrPath = Join-Path $runDirectory 'test.stderr.log'
  $quotedEntry = $entryPoint.Replace("'", "''")
  $quotedEvidence = $evidencePath.Replace("'", "''")
  $quotedCancellation = $cancellationPath.Replace("'", "''")
  $quotedRepositoryCommit = ([string]$manifest.repositoryCommit).Replace("'", "''")
  $utilityModule = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1'
  if (-not (Test-Path -LiteralPath $utilityModule -PathType Leaf)) {
    throw "The Windows PowerShell Utility module is unavailable: $utilityModule"
  }
  $quotedUtilityModule = $utilityModule.Replace("'", "''")
  $serverArgument = ''
  if (-not [string]::IsNullOrWhiteSpace([string]$request.test.serverPath)) {
    $quotedServer = ([string]$request.test.serverPath).Replace("'", "''")
    $serverArgument = " -ServerPath '$quotedServer'"
  }
  $command = "Import-Module '$quotedUtilityModule' -ErrorAction Stop; & '$quotedEntry' -DurationSeconds $durationSeconds -EvidencePath '$quotedEvidence' -TimeoutSeconds $smokeTimeoutSeconds -IterationPauseMilliseconds $iterationPauseMilliseconds -CancellationPath '$quotedCancellation' -RepositoryCommit '$quotedRepositoryCommit'$serverArgument"
  $encodedCommand = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($command))

  Write-JsonAtomic -Path (Join-Path $runDirectory 'node-status.json') -Value ([ordered]@{
    schemaVersion = $ProtocolVersion
    runId = $runId
    status = 'running'
    startedUtc = $startedUtc
    interactiveSession = $interactiveSession
    packageId = $packageId
    repositoryCommit = [string]$manifest.repositoryCommit
    workerSha256 = $WorkerSha256
  })

  $windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $testProcess = Start-Process -FilePath $windowsPowerShell `
    -ArgumentList "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -EncodedCommand $encodedCommand" `
    -WorkingDirectory $workspace `
    -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath `
    -PassThru

  if (-not $testProcess.WaitForExit($testTimeoutSeconds * 1000)) {
    $timedOut = $true
    [System.IO.File]::WriteAllText($cancellationPath, (Get-Date).ToUniversalTime().ToString('o'), [System.Text.UTF8Encoding]::new($false))
    if (-not $testProcess.WaitForExit(60000)) {
      Stop-Process -Id $testProcess.Id -Force -ErrorAction SilentlyContinue
      $forcedTermination = $true
      [void]$testProcess.WaitForExit(10000)
    }
  }
  if ($null -ne $cancellationPath -and (Test-Path -LiteralPath $cancellationPath -PathType Leaf)) {
    $cancelled = $true
  }
  if ($testProcess.HasExited) {
    $testProcess.WaitForExit()
    $testProcess.Refresh()
    $exitCode = [int]$testProcess.ExitCode
  }

  if (Test-Path -LiteralPath $evidencePath -PathType Leaf) {
    try {
      $stability = [System.IO.File]::ReadAllText($evidencePath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
    } catch {
      $errors.Add("Stability evidence could not be parsed: $($_.Exception.Message)")
    }
  } else {
    $errors.Add('The Notepad stability test did not produce contextime-stability.json.')
  }
  if ($timedOut) { $errors.Add('The node-side test exceeded its timeout.') }
  if ($cancelled -and -not $timedOut) { $errors.Add('The LAN controller cancelled the node-side test.') }
  if ($forcedTermination) { $errors.Add('The node-side test ignored cancellation and was forcibly terminated.') }
  if ($exitCode -ne 0) { $errors.Add("The node-side test exited with code $exitCode.") }
  if ($null -ne $stability -and -not [bool]$stability.passed) { $errors.Add('The Notepad stability summary did not pass.') }
  if ($null -ne $stability -and [string]$stability.repositoryCommit -ne [string]$manifest.repositoryCommit) {
    $errors.Add('The Notepad stability summary repository commit does not match the package.')
  }
  if ($null -ne $stability -and ([string]$stability.application -ne 'notepad' -or
      [int]$stability.targetDurationSeconds -ne $durationSeconds)) {
    $errors.Add('The Notepad stability summary does not match the requested test contract.')
  }
} catch {
  $errors.Add($_.Exception.ToString())
} finally {
  if ($null -eq $interactiveSession) {
    try { $interactiveSession = Get-InteractiveSessionState } catch { $interactiveSession = $null }
  }
  if ($null -ne $runDirectory -and (Test-Path -LiteralPath $runDirectory)) {
    if ($null -ne $cancellationPath -and (Test-Path -LiteralPath $cancellationPath -PathType Leaf)) {
      $cancelled = $true
      Copy-Item -LiteralPath $cancellationPath -Destination (Join-Path $runDirectory 'cancel.requested') -Force
    }
    try {
      $after = Get-ResourceSnapshot -InteractiveSession $interactiveSession
      Write-JsonAtomic -Path (Join-Path $runDirectory 'resource-after.json') -Value $after
    } catch {
      $errors.Add("Resource-after snapshot failed: $($_.Exception.Message)")
      $after = $null
    }

    $passed = $errors.Count -eq 0 -and $exitCode -eq 0 -and $null -ne $stability -and [bool]$stability.passed
    $artifactManifest = New-ArtifactManifest -RunDirectory $runDirectory
    Write-JsonAtomic -Path (Join-Path $runDirectory 'artifacts-manifest.json') -Value $artifactManifest
    $result = [ordered]@{
      schemaVersion = 'contextime.lan-result.v1'
      runId = $runId
      status = if ($passed) { 'passed' } elseif ($timedOut) { 'timed_out' } elseif ($cancelled) { 'cancelled' } elseif ($null -ne $interactiveSession -and -not [bool]$interactiveSession.passed) { 'interactive_session_rejected' } else { 'failed' }
      startedUtc = $startedUtc
      completedUtc = (Get-Date).ToUniversalTime().ToString('o')
      computerName = $env:COMPUTERNAME
      workerSha256 = $WorkerSha256
      interactiveSession = $interactiveSession
      package = if ($null -eq $request) { $null } else { $request.package }
      test = if ($null -eq $request) { $null } else { $request.test }
      testExitCode = $exitCode
      timedOut = $timedOut
      cancelled = $cancelled
      forcedTermination = $forcedTermination
      stabilityPassed = $null -ne $stability -and [bool]$stability.passed
      artifactManifest = 'artifacts-manifest.json'
      errors = @($errors)
      passed = $passed
    }
    Write-JsonAtomic -Path (Join-Path $runDirectory 'result.json') -Value $result
    Write-JsonAtomic -Path (Join-Path $ExchangeRoot 'node-status.json') -Value ([ordered]@{
      schemaVersion = $ProtocolVersion
      runId = $runId
      status = $result.status
      completedUtc = $result.completedUtc
      workerSha256 = $WorkerSha256
      passed = $passed
    })
  }

  if ($null -ne $runningRequestPath -and (Test-Path -LiteralPath $runningRequestPath)) {
    $archiveName = if ([string]::IsNullOrWhiteSpace($runId)) { [System.IO.Path]::GetFileName($runningRequestPath) } else { "$runId.request.json" }
    Move-Item -LiteralPath $runningRequestPath -Destination (Join-Path $RequestRoot "archive\$archiveName") -Force
  }
  if ($null -ne $mutex) {
    try { $mutex.ReleaseMutex() } catch {}
    $mutex.Dispose()
  }
}

if ($null -eq $result -or -not [bool]$result.passed) {
  exit 1
}
