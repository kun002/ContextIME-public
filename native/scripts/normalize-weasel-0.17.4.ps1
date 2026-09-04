[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$path = Join-Path $root 'WeaselSetup/imesetup.cpp'
if (-not (Test-Path $path -PathType Leaf)) {
  throw "Required upstream file not found: $path"
}

$utf8 = [System.Text.UTF8Encoding]::new($false)
$content = [System.IO.File]::ReadAllText($path)

# Weasel 0.17.4 splits the TSF profile GUID across adjacent wide-string
# literals in PSZTITLE_HANS and PSZTITLE_HANT. Normalize only that formatting
# so the identity patch can replace and audit complete GUID values.
$pattern = '3D02CAB6-2B8E-4781-BA20-"\s*\\\s*\r?\n\s*L"1C9267529467'
$matches = [regex]::Matches($content, $pattern)
if ($matches.Count -ne 2) {
  throw "Expected exactly two split Weasel profile GUID strings, found $($matches.Count)."
}

$content = [regex]::Replace(
  $content,
  $pattern,
  '3D02CAB6-2B8E-4781-BA20-1C9267529467'
)
[System.IO.File]::WriteAllText($path, $content, $utf8)

$remaining = [regex]::Matches([System.IO.File]::ReadAllText($path), $pattern).Count
if ($remaining -ne 0) {
  throw "Profile GUID normalization did not remove all split forms: $remaining"
}

# The source checkout does not contain the generated packaging README yet.
# Create the expected file so the identity patch can replace it before build.
$outputDir = Join-Path $root 'output'
[System.IO.Directory]::CreateDirectory($outputDir) | Out-Null
$readmePath = Join-Path $outputDir 'README.txt'
if (-not (Test-Path $readmePath -PathType Leaf)) {
  [System.IO.File]::WriteAllText($readmePath, '', $utf8)
}

Write-Host 'Normalized Weasel 0.17.4 source formatting and packaging placeholders.'
