[CmdletBinding()]
param(
  [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$includeRoot = Join-Path $repositoryRoot 'src\context-engine\include'
$sourcePath = Join-Path $repositoryRoot 'src\context-engine\src\context_engine.cpp'
$testPath = Join-Path $repositoryRoot 'tests\context-engine\context_engine_test.cpp'
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repositoryRoot 'artifacts\context-engine-tests'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

foreach ($required in @($sourcePath, $testPath, (Join-Path $includeRoot 'contextime\context_engine.h'))) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Context Engine test input is missing: $required"
  }
}

if ($env:OS -eq 'Windows_NT') {
  $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'cl.exe is required. Run this script from an MSVC developer environment.'
  }
  $executable = Join-Path $OutputDirectory 'context-engine-tests.exe'
  $objectDirectory = Join-Path $OutputDirectory 'obj'
  [System.IO.Directory]::CreateDirectory($objectDirectory) | Out-Null
  & $compiler.Source `
    /nologo /std:c++17 /EHsc /W4 /WX `
    "/I$includeRoot" `
    $sourcePath $testPath `
    "/Fo$objectDirectory\" `
    "/Fe$executable"
} else {
  $compiler = Get-Command g++ -ErrorAction SilentlyContinue
  if (-not $compiler) {
    throw 'g++ is required to compile the Context Engine tests.'
  }
  $executable = Join-Path $OutputDirectory 'context-engine-tests'
  & $compiler.Source `
    -std=c++17 -Wall -Wextra -Werror -pedantic `
    "-I$includeRoot" `
    $sourcePath $testPath `
    -o $executable
}

if ($LASTEXITCODE -ne 0) {
  throw "Context Engine compilation failed with exit code $LASTEXITCODE."
}

& $executable
if ($LASTEXITCODE -ne 0) {
  throw "Context Engine tests failed with exit code $LASTEXITCODE."
}

Write-Host "Context Engine tests passed: $executable"
