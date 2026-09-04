[CmdletBinding()]
param(
  [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$engineInclude = Join-Path $repositoryRoot 'src\context-engine\include'
$serviceInclude = Join-Path $repositoryRoot 'src\context-service\include'
$serviceSourceDirectory = Join-Path $repositoryRoot 'src\context-service\src'
$projectInclude = Join-Path $repositoryRoot 'src\project-indexer\include'
$engineSource = Join-Path $repositoryRoot 'src\context-engine\src\context_engine.cpp'
$protocolSource = Join-Path $repositoryRoot 'src\context-service\src\context_protocol.cpp'
$decisionCacheSource = Join-Path $repositoryRoot 'src\context-service\src\decision_cache.cpp'
$applicationContextSource = Join-Path $repositoryRoot 'src\context-service\src\application_context.cpp'
$editorContextSource = Join-Path $repositoryRoot 'src\context-service\src\editor_context.cpp'
$foregroundApplicationSource = Join-Path $repositoryRoot 'src\context-service\src\foreground_application_win.cpp'
$clientSource = Join-Path $repositoryRoot 'src\context-service\src\context_service_client_win.cpp'
$serverSource = Join-Path $repositoryRoot 'src\context-service\src\context_service_server_win.cpp'
$mainSource = Join-Path $repositoryRoot 'src\context-service\src\context_service_main_win.cpp'
$projectDictionarySource = Join-Path $repositoryRoot 'src\project-indexer\src\project_dictionary.cpp'
$activeProjectSnapshotSource = Join-Path $repositoryRoot 'src\project-indexer\src\active_project_snapshot.cpp'
$projectProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_indexer_protocol.cpp'
$projectManagementProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_management_protocol.cpp'
$projectServerSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_indexer_server_win.cpp'
$projectCandidateProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_protocol.cpp'
$projectCandidateServerSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_server_win.cpp'
$protocolTest = Join-Path $repositoryRoot 'tests\context-service\context_protocol_test.cpp'
$decisionCacheTest = Join-Path $repositoryRoot 'tests\context-service\decision_cache_test.cpp'
$applicationContextTest = Join-Path $repositoryRoot 'tests\context-service\application_context_test.cpp'
$editorContextTest = Join-Path $repositoryRoot 'tests\context-service\editor_context_test.cpp'
$windowsTest = Join-Path $repositoryRoot 'tests\context-service\context_service_win_test.cpp'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repositoryRoot 'artifacts\context-service-tests'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$commonInputs = @(
  $engineSource,
  $protocolSource,
  $decisionCacheSource,
  $applicationContextSource,
  $editorContextSource,
  $protocolTest,
  $decisionCacheTest,
  $applicationContextTest,
  $editorContextTest,
  (Join-Path $engineInclude 'contextime\context_engine.h'),
  (Join-Path $serviceInclude 'contextime\context_protocol.h'),
  (Join-Path $serviceInclude 'contextime\decision_cache.h'),
  (Join-Path $serviceInclude 'contextime\application_context.h'),
  (Join-Path $serviceInclude 'contextime\editor_context.h'),
  (Join-Path $serviceInclude 'contextime\context_service.h')
)
foreach ($required in $commonInputs) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Context Service test input is missing: $required"
  }
}

if ($env:OS -eq 'Windows_NT') {
  $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'cl.exe is required. Run this script from an MSVC developer environment.'
  }

  $protocolExecutable = Join-Path $OutputDirectory 'context-protocol-tests.exe'
  $protocolObjects = Join-Path $OutputDirectory 'protocol-obj'
  [System.IO.Directory]::CreateDirectory($protocolObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$engineInclude" "/I$serviceInclude" `
    $engineSource $protocolSource $protocolTest `
    "/Fo$protocolObjects\" "/Fe$protocolExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Context Protocol compilation failed with exit code $LASTEXITCODE."
  }
  & $protocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Context Protocol tests failed with exit code $LASTEXITCODE."
  }

  $cacheExecutable = Join-Path $OutputDirectory 'decision-cache-tests.exe'
  $cacheObjects = Join-Path $OutputDirectory 'cache-obj'
  [System.IO.Directory]::CreateDirectory($cacheObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$engineInclude" "/I$serviceInclude" `
    $engineSource $decisionCacheSource $decisionCacheTest `
    "/Fo$cacheObjects\" "/Fe$cacheExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Decision Cache compilation failed with exit code $LASTEXITCODE."
  }
  & $cacheExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Decision Cache tests failed with exit code $LASTEXITCODE."
  }

  $applicationExecutable = Join-Path $OutputDirectory 'application-context-tests.exe'
  $applicationObjects = Join-Path $OutputDirectory 'application-obj'
  [System.IO.Directory]::CreateDirectory($applicationObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$engineInclude" "/I$serviceInclude" `
    $applicationContextSource $applicationContextTest `
    "/Fo$applicationObjects\" "/Fe$applicationExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Application Context compilation failed with exit code $LASTEXITCODE."
  }
  & $applicationExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Application Context tests failed with exit code $LASTEXITCODE."
  }

  $editorExecutable = Join-Path $OutputDirectory 'editor-context-tests.exe'
  $editorObjects = Join-Path $OutputDirectory 'editor-obj'
  [System.IO.Directory]::CreateDirectory($editorObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$engineInclude" "/I$serviceInclude" `
    $engineSource $applicationContextSource $editorContextSource $editorContextTest `
    "/Fo$editorObjects\" "/Fe$editorExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Editor Context compilation failed with exit code $LASTEXITCODE."
  }
  & $editorExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Editor Context tests failed with exit code $LASTEXITCODE."
  }

  foreach ($required in @(
    $foregroundApplicationSource,
    $clientSource,
    $serverSource,
    $mainSource,
    $projectDictionarySource,
    $activeProjectSnapshotSource,
    $projectProtocolSource,
    $projectManagementProtocolSource,
    $projectServerSource,
    $projectCandidateProtocolSource,
    $projectCandidateServerSource,
    (Join-Path $projectInclude 'contextime\active_project_snapshot.h'),
    (Join-Path $projectInclude 'contextime\project_candidate_protocol.h'),
    (Join-Path $projectInclude 'contextime\project_candidate_server.h'),
    (Join-Path $projectInclude 'contextime\project_dictionary.h'),
    (Join-Path $projectInclude 'contextime\project_indexer_protocol.h'),
    (Join-Path $projectInclude 'contextime\project_management_protocol.h'),
    (Join-Path $projectInclude 'contextime\project_indexer_server.h'),
    $windowsTest
  )) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "Context Service Windows input is missing: $required"
    }
  }

  $windowsExecutable = Join-Path $OutputDirectory 'context-service-windows-tests.exe'
  $windowsObjects = Join-Path $OutputDirectory 'windows-test-obj'
  [System.IO.Directory]::CreateDirectory($windowsObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$engineInclude" "/I$serviceInclude" `
    $engineSource $protocolSource $applicationContextSource $editorContextSource `
    $foregroundApplicationSource $clientSource $serverSource $windowsTest `
    "/Fo$windowsObjects\" "/Fe$windowsExecutable" `
    /link Advapi32.lib User32.lib
  if ($LASTEXITCODE -ne 0) {
    throw "Context Service Windows test compilation failed with exit code $LASTEXITCODE."
  }
  & $windowsExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Context Service Windows tests failed with exit code $LASTEXITCODE."
  }

  $serviceExecutable = Join-Path $OutputDirectory 'contextime-context-service.exe'
  $serviceObjects = Join-Path $OutputDirectory 'service-obj'
  [System.IO.Directory]::CreateDirectory($serviceObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$engineInclude" "/I$serviceInclude" "/I$projectInclude" `
    "/I$serviceSourceDirectory" `
    $engineSource $protocolSource $applicationContextSource $editorContextSource `
    $foregroundApplicationSource $serverSource $projectDictionarySource `
    $activeProjectSnapshotSource $projectProtocolSource $projectServerSource `
    $projectManagementProtocolSource `
    $projectCandidateProtocolSource $projectCandidateServerSource $mainSource `
    "/Fo$serviceObjects\" "/Fe$serviceExecutable" `
    /link Advapi32.lib User32.lib /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup
  if ($LASTEXITCODE -ne 0) {
    throw "Context Service executable compilation failed with exit code $LASTEXITCODE."
  }

  $lifecycleId = [Guid]::NewGuid().ToString('N')
  $lifecycleProjectRoot = Join-Path $OutputDirectory "lifecycle-project-$lifecycleId"
  $lifecycleProjectPipeLeaf = "ContextIME.ProjectIndexer.Lifecycle.$lifecycleId"
  $lifecycleCandidatePipeLeaf = "ContextIME.ProjectCandidate.Lifecycle.$lifecycleId"
  $lifecycleArguments = @(
    '--pipe', "\\.\pipe\ContextIME.ContextService.Lifecycle.$lifecycleId",
    '--mutex', "Local\ContextIME.ContextService.Lifecycle.$lifecycleId",
    '--stop-event', "Local\ContextIME.ContextService.Lifecycle.Stop.$lifecycleId",
    '--project-pipe', "\\.\pipe\$lifecycleProjectPipeLeaf",
    '--project-candidate-pipe', "\\.\pipe\$lifecycleCandidatePipeLeaf",
    '--project-dictionary-root', $lifecycleProjectRoot,
    '--client-timeout-ms', '250'
  )
  $serviceProcess = $null
  $restartProcess = $null
  try {
    $serviceProcess = Start-Process `
      -FilePath $serviceExecutable `
      -ArgumentList $lifecycleArguments `
      -WindowStyle Hidden `
      -PassThru
    Start-Sleep -Milliseconds 250
    if ($serviceProcess.HasExited) {
      throw "Context Service exited during lifecycle startup: $($serviceProcess.ExitCode)"
    }

    $projectPipe = [System.IO.Pipes.NamedPipeClientStream]::new(
      '.',
      $lifecycleProjectPipeLeaf,
      [System.IO.Pipes.PipeDirection]::InOut
    )
    try {
      $projectPipe.Connect(2000)
      if (-not $projectPipe.IsConnected) {
        throw 'Context Service Project Indexer pipe did not accept a connection.'
      }
    } finally {
      $projectPipe.Dispose()
    }

    $candidatePipe = [System.IO.Pipes.NamedPipeClientStream]::new(
      '.',
      $lifecycleCandidatePipeLeaf,
      [System.IO.Pipes.PipeDirection]::InOut
    )
    try {
      $candidatePipe.Connect(2000)
      if (-not $candidatePipe.IsConnected) {
        throw 'Context Service Project Candidate pipe did not accept a connection.'
      }
    } finally {
      $candidatePipe.Dispose()
    }

    $duplicate = Start-Process `
      -FilePath $serviceExecutable `
      -ArgumentList $lifecycleArguments `
      -WindowStyle Hidden `
      -PassThru
    if (-not $duplicate.WaitForExit(3000) -or $duplicate.ExitCode -ne 0) {
      throw 'A duplicate Context Service instance did not exit successfully.'
    }

    $quit = Start-Process `
      -FilePath $serviceExecutable `
      -ArgumentList @($lifecycleArguments + '--quit') `
      -WindowStyle Hidden `
      -PassThru
    if (-not $quit.WaitForExit(3000) -or $quit.ExitCode -ne 0) {
      throw 'Context Service --quit did not complete successfully.'
    }
    if (-not $serviceProcess.WaitForExit(5000) -or $serviceProcess.ExitCode -ne 0) {
      throw 'Context Service did not stop cleanly after --quit.'
    }

    $restartProcess = Start-Process `
      -FilePath $serviceExecutable `
      -ArgumentList $lifecycleArguments `
      -WindowStyle Hidden `
      -PassThru
    Start-Sleep -Milliseconds 250
    if ($restartProcess.HasExited) {
      throw "Context Service did not restart after a clean stop: $($restartProcess.ExitCode)"
    }
    $restartQuit = Start-Process `
      -FilePath $serviceExecutable `
      -ArgumentList @($lifecycleArguments + '--quit') `
      -WindowStyle Hidden `
      -PassThru
    if (-not $restartQuit.WaitForExit(3000) -or $restartQuit.ExitCode -ne 0 -or
        -not $restartProcess.WaitForExit(5000) -or $restartProcess.ExitCode -ne 0) {
      throw 'Restarted Context Service did not stop cleanly.'
    }
  } finally {
    foreach ($process in @($serviceProcess, $restartProcess)) {
      if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
      }
    }
  }
  Write-Host "Context Service tests passed: $windowsExecutable"
  Write-Host "Decision Cache tests passed: $cacheExecutable"
  Write-Host "Application Context tests passed: $applicationExecutable"
  Write-Host "Editor Context tests passed: $editorExecutable"
  Write-Host 'Context Service lifecycle passed: project index/candidate pipes, singleton, quit, restart'
  Write-Host "Context Service executable built: $serviceExecutable"
} else {
  $compiler = Get-Command g++ -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'g++ is required to compile the Context Protocol tests.'
  }
  $protocolExecutable = Join-Path $OutputDirectory 'context-protocol-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$engineInclude" "-I$serviceInclude" `
    $engineSource $protocolSource $protocolTest `
    -o $protocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Context Protocol compilation failed with exit code $LASTEXITCODE."
  }
  & $protocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Context Protocol tests failed with exit code $LASTEXITCODE."
  }

  $cacheExecutable = Join-Path $OutputDirectory 'decision-cache-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic -pthread `
    "-I$engineInclude" "-I$serviceInclude" `
    $engineSource $decisionCacheSource $decisionCacheTest `
    -o $cacheExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Decision Cache compilation failed with exit code $LASTEXITCODE."
  }
  & $cacheExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Decision Cache tests failed with exit code $LASTEXITCODE."
  }

  $applicationExecutable = Join-Path $OutputDirectory 'application-context-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$engineInclude" "-I$serviceInclude" `
    $applicationContextSource $applicationContextTest `
    -o $applicationExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Application Context compilation failed with exit code $LASTEXITCODE."
  }
  & $applicationExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Application Context tests failed with exit code $LASTEXITCODE."
  }

  $editorExecutable = Join-Path $OutputDirectory 'editor-context-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$engineInclude" "-I$serviceInclude" `
    $engineSource $applicationContextSource $editorContextSource $editorContextTest `
    -o $editorExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Editor Context compilation failed with exit code $LASTEXITCODE."
  }
  & $editorExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Editor Context tests failed with exit code $LASTEXITCODE."
  }
  Write-Host "Context Protocol tests passed: $protocolExecutable"
  Write-Host "Decision Cache tests passed: $cacheExecutable"
  Write-Host "Application Context tests passed: $applicationExecutable"
  Write-Host "Editor Context tests passed: $editorExecutable"
}
