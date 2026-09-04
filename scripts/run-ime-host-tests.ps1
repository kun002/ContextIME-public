[CmdletBinding()]
param(
  [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engineInclude = Join-Path $repositoryRoot 'src\context-engine\include'
$serviceInclude = Join-Path $repositoryRoot 'src\context-service\include'
$hostInclude = Join-Path $repositoryRoot 'src\ime-host\include'
$projectInclude = Join-Path $repositoryRoot 'src\project-indexer\include'
$serviceSourceDirectory = Join-Path $repositoryRoot 'src\context-service\src'
$engineSource = Join-Path $repositoryRoot 'src\context-engine\src\context_engine.cpp'
$protocolSource = Join-Path $repositoryRoot 'src\context-service\src\context_protocol.cpp'
$cacheSource = Join-Path $repositoryRoot 'src\context-service\src\decision_cache.cpp'
$applicationSource = Join-Path $repositoryRoot 'src\context-service\src\application_context.cpp'
$editorSource = Join-Path $repositoryRoot 'src\context-service\src\editor_context.cpp'
$foregroundSource = Join-Path $repositoryRoot 'src\context-service\src\foreground_application_win.cpp'
$clientSource = Join-Path $repositoryRoot 'src\context-service\src\context_service_client_win.cpp'
$serverSource = Join-Path $repositoryRoot 'src\context-service\src\context_service_server_win.cpp'
$applierSource = Join-Path $repositoryRoot 'src\ime-host\src\ime_state_applier.cpp'
$workerSource = Join-Path $repositoryRoot 'src\ime-host\src\context_refresh_worker_win.cpp'
$applierTest = Join-Path $repositoryRoot 'tests\ime-host\ime_state_applier_test.cpp'
$workerTest = Join-Path $repositoryRoot 'tests\ime-host\context_refresh_worker_win_test.cpp'
$projectDictionarySource = Join-Path $repositoryRoot 'src\project-indexer\src\project_dictionary.cpp'
$activeProjectSnapshotSource = Join-Path $repositoryRoot 'src\project-indexer\src\active_project_snapshot.cpp'
$projectCandidateProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_protocol.cpp'
$projectCandidateServerSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_server_win.cpp'
$projectCandidateClientSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_client_win.cpp'
$projectCandidateWorkerSource = Join-Path $repositoryRoot 'src\ime-host\src\project_candidate_refresh_worker_win.cpp'
$projectCandidateWorkerTest = Join-Path $repositoryRoot 'tests\ime-host\project_candidate_refresh_worker_win_test.cpp'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repositoryRoot 'artifacts\ime-host-tests'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$commonInputs = @(
  $engineSource,
  $applierSource,
  $applierTest,
  (Join-Path $engineInclude 'contextime\context_engine.h'),
  (Join-Path $hostInclude 'contextime\ime_state_applier.h')
)
foreach ($required in $commonInputs) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "IME Host test input is missing: $required"
  }
}

if ($env:OS -eq 'Windows_NT') {
  $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'cl.exe is required. Run this script from an MSVC developer environment.'
  }

  $applierExecutable = Join-Path $OutputDirectory 'ime-state-applier-tests.exe'
  $applierObjects = Join-Path $OutputDirectory 'applier-obj'
  [System.IO.Directory]::CreateDirectory($applierObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$engineInclude" "/I$hostInclude" `
    $engineSource $applierSource $applierTest `
    "/Fo$applierObjects\" "/Fe$applierExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "IME State Applier compilation failed with exit code $LASTEXITCODE."
  }
  & $applierExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "IME State Applier tests failed with exit code $LASTEXITCODE."
  }

  foreach ($required in @(
      $protocolSource,
      $cacheSource,
      $applicationSource,
      $editorSource,
      $foregroundSource,
      $clientSource,
      $serverSource,
      $workerSource,
      $workerTest,
      (Join-Path $hostInclude 'contextime\context_refresh_worker.h')
    )) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "Context Refresh Worker test input is missing: $required"
    }
  }

  $workerExecutable = Join-Path $OutputDirectory 'context-refresh-worker-tests.exe'
  $workerObjects = Join-Path $OutputDirectory 'worker-obj'
  [System.IO.Directory]::CreateDirectory($workerObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$engineInclude" "/I$serviceInclude" "/I$hostInclude" `
    $engineSource $protocolSource $cacheSource $applicationSource `
    $editorSource $foregroundSource $clientSource $serverSource `
    $workerSource $workerTest `
    "/Fo$workerObjects\" "/Fe$workerExecutable" `
    /link Advapi32.lib User32.lib
  if ($LASTEXITCODE -ne 0) {
    throw "Context Refresh Worker compilation failed with exit code $LASTEXITCODE."
  }
  & $workerExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Context Refresh Worker tests failed with exit code $LASTEXITCODE."
  }

  foreach ($required in @(
      $projectDictionarySource,
      $activeProjectSnapshotSource,
      $projectCandidateProtocolSource,
      $projectCandidateServerSource,
      $projectCandidateClientSource,
      $projectCandidateWorkerSource,
      $projectCandidateWorkerTest,
      (Join-Path $hostInclude 'contextime\project_candidate_refresh_worker.h')
    )) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "Project Candidate Worker test input is missing: $required"
    }
  }
  $projectCandidateExecutable = Join-Path $OutputDirectory 'project-candidate-refresh-worker-tests.exe'
  $projectCandidateObjects = Join-Path $OutputDirectory 'project-candidate-obj'
  [System.IO.Directory]::CreateDirectory($projectCandidateObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$projectInclude" "/I$hostInclude" "/I$serviceSourceDirectory" `
    $projectDictionarySource $activeProjectSnapshotSource `
    $projectCandidateProtocolSource $projectCandidateServerSource `
    $projectCandidateClientSource $projectCandidateWorkerSource `
    $projectCandidateWorkerTest `
    "/Fo$projectCandidateObjects\" "/Fe$projectCandidateExecutable" `
    /link Advapi32.lib
  if ($LASTEXITCODE -ne 0) {
    throw "Project Candidate Refresh Worker compilation failed with exit code $LASTEXITCODE."
  }
  & $projectCandidateExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Candidate Refresh Worker tests failed with exit code $LASTEXITCODE."
  }

  Write-Host "IME State Applier tests passed: $applierExecutable"
  Write-Host "Context Refresh Worker tests passed: $workerExecutable"
  Write-Host "Project Candidate Refresh Worker tests passed: $projectCandidateExecutable"
} else {
  $compiler = Get-Command g++ -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'g++ is required to compile the IME State Applier tests.'
  }
  $applierExecutable = Join-Path $OutputDirectory 'ime-state-applier-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$engineInclude" "-I$hostInclude" `
    $engineSource $applierSource $applierTest `
    -o $applierExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "IME State Applier compilation failed with exit code $LASTEXITCODE."
  }
  & $applierExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "IME State Applier tests failed with exit code $LASTEXITCODE."
  }
  Write-Host "IME State Applier tests passed: $applierExecutable"
}
