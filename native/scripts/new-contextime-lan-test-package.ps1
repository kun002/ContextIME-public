[CmdletBinding()]
param(
  [string]$OutputDirectory,
  [switch]$AllowDirtyHarness
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
  throw 'The ContextIME LAN test package builder requires Windows.'
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path $repositoryRoot 'artifacts\lan-test\packages'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$packageFiles = @(
  'native\scripts\run-contextime-app-smoke.ps1',
  'native\scripts\run-contextime-stability.ps1',
  'native\tests\app-smoke\ContextIMEAppSmoke.cs',
  'native\contextime.identity.json',
  'native\upstream.lock.json'
)

foreach ($relativePath in $packageFiles) {
  $sourcePath = Join-Path $repositoryRoot $relativePath
  if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Required LAN package file is missing: $relativePath"
  }
}

$repositoryCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $repositoryCommit -notmatch '^[0-9a-f]{40}$') {
  throw 'Could not resolve the repository commit for the LAN test package.'
}
$dirtyHarness = @(& git -C $repositoryRoot status --porcelain -- @($packageFiles | ForEach-Object { $_.Replace('\', '/') }))
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the LAN harness working tree.' }
if ($dirtyHarness.Count -gt 0 -and -not $AllowDirtyHarness) {
  throw "The LAN harness has uncommitted changes. Commit them or pass -AllowDirtyHarness for a non-acceptance development package.`n$($dirtyHarness -join "`n")"
}

$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$packageId = 'notepad-{0}-{1}' -f $repositoryCommit.Substring(0, 12), $stamp
$packagePath = Join-Path $OutputDirectory "$packageId.zip"
$sidecarManifestPath = Join-Path $OutputDirectory "$packageId.manifest.json"
if ((Test-Path -LiteralPath $packagePath) -or (Test-Path -LiteralPath $sidecarManifestPath)) {
  throw "The LAN package already exists: $packageId"
}

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("contextime-lan-package-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
try {
  foreach ($relativePath in $packageFiles) {
    $sourcePath = Join-Path $repositoryRoot $relativePath
    $destinationPath = Join-Path $temporaryRoot $relativePath
    New-Item -ItemType Directory -Path ([System.IO.Path]::GetDirectoryName($destinationPath)) -Force | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
  }

  $manifestFiles = @($packageFiles | Sort-Object | ForEach-Object {
    $item = Get-Item -LiteralPath (Join-Path $temporaryRoot $_)
    [ordered]@{
      path = $_
      length = [long]$item.Length
      sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $item.FullName).Hash.ToLowerInvariant()
    }
  })
  $upstream = [System.IO.File]::ReadAllText((Join-Path $repositoryRoot 'native\upstream.lock.json'), [System.Text.Encoding]::UTF8) | ConvertFrom-Json
  $manifest = [ordered]@{
    schemaVersion = 'contextime.lan-package.v1'
    packageId = $packageId
    createdUtc = (Get-Date).ToUniversalTime().ToString('o')
    testKind = 'notepad_stability'
    entryPoint = 'native\scripts\run-contextime-stability.ps1'
    repositoryCommit = $repositoryCommit
    harnessFilesClean = $dirtyHarness.Count -eq 0
    target = 'windows-x64-interactive-desktop'
    upstream = [ordered]@{
      weaselVersion = [string]$upstream.weasel.version
      weaselCommit = [string]$upstream.weasel.commit
      librimeVersion = [string]$upstream.librime.version
      librimeCommit = [string]$upstream.librime.commit
    }
    files = $manifestFiles
  }
  $manifestJson = $manifest | ConvertTo-Json -Depth 10
  [System.IO.File]::WriteAllText((Join-Path $temporaryRoot 'manifest.json'), $manifestJson, [System.Text.UTF8Encoding]::new($false))
  Compress-Archive -Path (Join-Path $temporaryRoot '*') -DestinationPath $packagePath -CompressionLevel Optimal
  [System.IO.File]::WriteAllText($sidecarManifestPath, $manifestJson, [System.Text.UTF8Encoding]::new($false))

  $result = [ordered]@{
    schemaVersion = 'contextime.lan-package-build.v1'
    packageId = $packageId
    packagePath = $packagePath
    packageLength = [long](Get-Item -LiteralPath $packagePath).Length
    packageSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash.ToLowerInvariant()
    manifestPath = $sidecarManifestPath
    repositoryCommit = $repositoryCommit
    harnessFilesClean = $dirtyHarness.Count -eq 0
  }
  $result | ConvertTo-Json -Depth 8
} finally {
  if (Test-Path -LiteralPath $temporaryRoot) {
    $resolvedTemporaryRoot = [System.IO.Path]::GetFullPath($temporaryRoot)
    $resolvedTempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedTemporaryRoot.StartsWith($resolvedTempBase, [System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetFileName($resolvedTemporaryRoot) -notlike 'contextime-lan-package-*') {
      throw "Refusing to remove an unexpected temporary package path: $resolvedTemporaryRoot"
    }
    Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
  }
}
