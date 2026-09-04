[CmdletBinding()]
param(
  [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$includeDirectory = Join-Path $repositoryRoot 'src\project-indexer\include'
$contextServiceSourceDirectory = Join-Path $repositoryRoot 'src\context-service\src'
$dictionarySource = Join-Path $repositoryRoot 'src\project-indexer\src\project_dictionary.cpp'
$activeSnapshotSource = Join-Path $repositoryRoot 'src\project-indexer\src\active_project_snapshot.cpp'
$protocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_indexer_protocol.cpp'
$managementProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_management_protocol.cpp'
$managementClientSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_management_client_win.cpp'
$candidateProtocolSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_protocol.cpp'
$candidateServerSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_server_win.cpp'
$candidateClientSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_candidate_client_win.cpp'
$serverSource = Join-Path $repositoryRoot 'src\project-indexer\src\project_indexer_server_win.cpp'
$dictionaryTest = Join-Path $repositoryRoot 'tests\project-indexer\project_dictionary_test.cpp'
$activeSnapshotTest = Join-Path $repositoryRoot 'tests\project-indexer\active_project_snapshot_test.cpp'
$protocolTest = Join-Path $repositoryRoot 'tests\project-indexer\project_indexer_protocol_test.cpp'
$managementProtocolTest = Join-Path $repositoryRoot 'tests\project-indexer\project_management_protocol_test.cpp'
$candidateProtocolTest = Join-Path $repositoryRoot 'tests\project-indexer\project_candidate_protocol_test.cpp'
$performanceTest = Join-Path $repositoryRoot 'tests\project-indexer\project_dictionary_performance_test.cpp'
$installedPressureTest = Join-Path $repositoryRoot 'tests\project-indexer\project_dictionary_installed_pressure_win.cpp'
$windowsTest = Join-Path $repositoryRoot 'tests\project-indexer\project_indexer_win_test.cpp'
$managerSource = Join-Path $repositoryRoot 'src\settings\project_dictionary_manager_win.cpp'
$dictionaryHeader = Join-Path $includeDirectory 'contextime\project_dictionary.h'
$protocolHeader = Join-Path $includeDirectory 'contextime\project_indexer_protocol.h'
$managementProtocolHeader = Join-Path $includeDirectory 'contextime\project_management_protocol.h'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repositoryRoot 'artifacts\project-indexer-tests'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

foreach ($required in @(
  $dictionarySource,
  $activeSnapshotSource,
  $protocolSource,
  $managementProtocolSource,
  $candidateProtocolSource,
  $dictionaryTest,
  $activeSnapshotTest,
  $protocolTest,
  $managementProtocolTest,
  $candidateProtocolTest,
  $performanceTest,
  $dictionaryHeader,
  $protocolHeader,
  $managementProtocolHeader
)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Project Indexer test input is missing: $required"
  }
}

if ($env:OS -eq 'Windows_NT') {
  $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'cl.exe is required. Run this script from an MSVC developer environment.'
  }
  $dictionaryExecutable = Join-Path $OutputDirectory 'project-dictionary-tests.exe'
  $dictionaryObjects = Join-Path $OutputDirectory 'dictionary-obj'
  [System.IO.Directory]::CreateDirectory($dictionaryObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $dictionaryTest `
    "/Fo$dictionaryObjects\" "/Fe$dictionaryExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Project Dictionary compilation failed with exit code $LASTEXITCODE."
  }

  $activeSnapshotExecutable = Join-Path $OutputDirectory 'active-project-snapshot-tests.exe'
  $activeSnapshotObjects = Join-Path $OutputDirectory 'active-snapshot-obj'
  [System.IO.Directory]::CreateDirectory($activeSnapshotObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $activeSnapshotTest `
    "/Fo$activeSnapshotObjects\" "/Fe$activeSnapshotExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Active Project Snapshot compilation failed with exit code $LASTEXITCODE."
  }

  $protocolExecutable = Join-Path $OutputDirectory 'project-indexer-protocol-tests.exe'
  $protocolObjects = Join-Path $OutputDirectory 'protocol-obj'
  [System.IO.Directory]::CreateDirectory($protocolObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $protocolSource $protocolTest `
    "/Fo$protocolObjects\" "/Fe$protocolExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Project Indexer Protocol compilation failed with exit code $LASTEXITCODE."
  }

  $managementProtocolExecutable = Join-Path $OutputDirectory 'project-management-protocol-tests.exe'
  $managementProtocolObjects = Join-Path $OutputDirectory 'management-protocol-obj'
  [System.IO.Directory]::CreateDirectory($managementProtocolObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $protocolSource $managementProtocolSource `
    $managementProtocolTest `
    "/Fo$managementProtocolObjects\" "/Fe$managementProtocolExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Project Management Protocol compilation failed with exit code $LASTEXITCODE."
  }


  $candidateProtocolExecutable = Join-Path $OutputDirectory 'project-candidate-protocol-tests.exe'
  $candidateProtocolObjects = Join-Path $OutputDirectory 'candidate-protocol-obj'
  [System.IO.Directory]::CreateDirectory($candidateProtocolObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $candidateProtocolSource `
    $candidateProtocolTest `
    "/Fo$candidateProtocolObjects\" "/Fe$candidateProtocolExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Project Candidate Protocol compilation failed with exit code $LASTEXITCODE."
  }

  $performanceExecutable = Join-Path $OutputDirectory 'project-dictionary-performance-tests.exe'
  $performanceObjects = Join-Path $OutputDirectory 'performance-obj'
  [System.IO.Directory]::CreateDirectory($performanceObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $performanceTest `
    "/Fo$performanceObjects\" "/Fe$performanceExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Project Dictionary Performance compilation failed with exit code $LASTEXITCODE."
  }

  if (-not (Test-Path -LiteralPath $installedPressureTest -PathType Leaf)) {
    throw "Installed Project Dictionary pressure input is missing: $installedPressureTest"
  }
  $installedPressureExecutable = Join-Path $OutputDirectory 'project-dictionary-installed-pressure-tests.exe'
  $installedPressureObjects = Join-Path $OutputDirectory 'installed-pressure-obj'
  [System.IO.Directory]::CreateDirectory($installedPressureObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" "/I$contextServiceSourceDirectory" `
    $dictionarySource $protocolSource $installedPressureTest `
    "/Fo$installedPressureObjects\" "/Fe$installedPressureExecutable"
  if ($LASTEXITCODE -ne 0) {
    throw "Installed Project Dictionary pressure compilation failed with exit code $LASTEXITCODE."
  }

  foreach ($required in @(
    $serverSource,
    $managementClientSource,
    $managerSource,
    $candidateServerSource,
    $candidateClientSource,
    $windowsTest
  )) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
      throw "Project Indexer Windows test input is missing: $required"
    }
  }
  $windowsExecutable = Join-Path $OutputDirectory 'project-indexer-windows-tests.exe'
  $windowsObjects = Join-Path $OutputDirectory 'windows-obj'
  [System.IO.Directory]::CreateDirectory($windowsObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" "/I$contextServiceSourceDirectory" `
    $dictionarySource $activeSnapshotSource $protocolSource `
    $managementProtocolSource $managementClientSource $serverSource `
    $candidateProtocolSource $candidateServerSource $candidateClientSource `
    $windowsTest `
    "/Fo$windowsObjects\" "/Fe$windowsExecutable" `
    /link Advapi32.lib
  if ($LASTEXITCODE -ne 0) {
    throw "Project Indexer Windows compilation failed with exit code $LASTEXITCODE."
  }

  $managerExecutable = Join-Path $OutputDirectory 'contextime-project-dictionary-manager.exe'
  $managerObjects = Join-Path $OutputDirectory 'manager-obj'
  [System.IO.Directory]::CreateDirectory($managerObjects) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE `
    "/I$includeDirectory" "/I$contextServiceSourceDirectory" `
    $dictionarySource $protocolSource $managementProtocolSource `
    $managementClientSource $managerSource `
    "/Fo$managerObjects\" "/Fe$managerExecutable" `
    /link Advapi32.lib Comctl32.lib Gdi32.lib User32.lib `
    /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
  if ($LASTEXITCODE -ne 0) {
    throw "Project Dictionary Manager compilation failed with exit code $LASTEXITCODE."
  }
} else {
  $compiler = Get-Command g++ -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'g++ is required to compile the Project Dictionary tests.'
  }
  $dictionaryExecutable = Join-Path $OutputDirectory 'project-dictionary-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$includeDirectory" `
    $dictionarySource $dictionaryTest `
    -o $dictionaryExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Dictionary compilation failed with exit code $LASTEXITCODE."
  }

  $activeSnapshotExecutable = Join-Path $OutputDirectory 'active-project-snapshot-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic -pthread `
    "-I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $activeSnapshotTest `
    -o $activeSnapshotExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Active Project Snapshot compilation failed with exit code $LASTEXITCODE."
  }

  $protocolExecutable = Join-Path $OutputDirectory 'project-indexer-protocol-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$includeDirectory" `
    $dictionarySource $protocolSource $protocolTest `
    -o $protocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Indexer Protocol compilation failed with exit code $LASTEXITCODE."
  }

  $managementProtocolExecutable = Join-Path $OutputDirectory 'project-management-protocol-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$includeDirectory" `
    $dictionarySource $protocolSource $managementProtocolSource `
    $managementProtocolTest `
    -o $managementProtocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Management Protocol compilation failed with exit code $LASTEXITCODE."
  }


  $candidateProtocolExecutable = Join-Path $OutputDirectory 'project-candidate-protocol-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic -pthread `
    "-I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $candidateProtocolSource `
    $candidateProtocolTest `
    -o $candidateProtocolExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Candidate Protocol compilation failed with exit code $LASTEXITCODE."
  }

  $performanceExecutable = Join-Path $OutputDirectory 'project-dictionary-performance-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic -pthread `
    "-I$includeDirectory" `
    $dictionarySource $activeSnapshotSource $performanceTest `
    -o $performanceExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Dictionary Performance compilation failed with exit code $LASTEXITCODE."
  }
}

& $dictionaryExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Project Dictionary tests failed with exit code $LASTEXITCODE."
}
& $activeSnapshotExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Active Project Snapshot tests failed with exit code $LASTEXITCODE."
}
& $protocolExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Project Indexer Protocol tests failed with exit code $LASTEXITCODE."
}
& $managementProtocolExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Project Management Protocol tests failed with exit code $LASTEXITCODE."
}
& $candidateProtocolExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Project Candidate Protocol tests failed with exit code $LASTEXITCODE."
}
& $performanceExecutable
if ($LASTEXITCODE -ne 0) {
  throw "Project Dictionary Performance tests failed with exit code $LASTEXITCODE."
}
if ($env:OS -eq 'Windows_NT') {
  & $windowsExecutable
  if ($LASTEXITCODE -ne 0) {
    throw "Project Indexer Windows tests failed with exit code $LASTEXITCODE."
  }
  Write-Host "Project Indexer Windows tests passed: $windowsExecutable"
  Write-Host "Project Dictionary Manager built: $managerExecutable"
}
Write-Host "Project Dictionary tests passed: $dictionaryExecutable"
Write-Host "Active Project Snapshot tests passed: $activeSnapshotExecutable"
Write-Host "Project Indexer Protocol tests passed: $protocolExecutable"
Write-Host "Project Management Protocol tests passed: $managementProtocolExecutable"
Write-Host "Project Candidate Protocol tests passed: $candidateProtocolExecutable"
Write-Host "Project Dictionary Performance tests passed: $performanceExecutable"
if ($env:OS -eq 'Windows_NT') {
  Write-Host "Installed Project Dictionary pressure tool compiled: $installedPressureExecutable"
}
