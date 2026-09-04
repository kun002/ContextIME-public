[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$utf8WithBom = [System.Text.UTF8Encoding]::new($true)

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required upstream file not found: $RelativePath"
  }
  return $path
}

function Replace-LiteralRequired {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New,
    [int]$ExpectedCount = 1
  )

  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedOld = $Old.Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedNew = $New.Replace("`r`n", "`n").Replace("`r", "`n")
  $count = [regex]::Matches($content, [regex]::Escape($normalizedOld)).Count
  if ($count -ne $ExpectedCount) {
    throw "Expected exactly $ExpectedCount occurrence(s) in ${RelativePath}, found ${count}: $Old"
  }
  [System.IO.File]::WriteAllText(
    $path,
    $content.Replace($normalizedOld, $normalizedNew),
    $utf8NoBom
  )
}

Replace-LiteralRequired 'output/install.nsi' '0.2.3 Preview' '0.3.0 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.2.3-preview-installer.exe' 'contextime-0.3.0-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.2.3-preview' '0.3.0-preview' 2
Replace-LiteralRequired 'output/install.nsi' '0.2.3.0' '0.3.0.0'
Replace-LiteralRequired 'output/README.txt' '0.2.3 Preview' '0.3.0 Preview'

Replace-LiteralRequired 'output/install.nsi' @'
call_uninstaller:
  ExecWait '"$R1\WeaselServer.exe" /quit'
'@ @'
call_uninstaller:
  IfFileExists "$R1\contextime-context-service.exe" 0 +2
  ExecWait '"$R1\contextime-context-service.exe" --quit'
  ExecWait '"$R1\WeaselServer.exe" /quit'
'@

Replace-LiteralRequired 'output/install.nsi' @'
  File "stop_service.bat"
  File "contextime.dll"
'@ @'
  File "stop_service.bat"
  File "contextime-context-service.exe"
  File "contextime.dll"
'@

Replace-LiteralRequired 'output/install.nsi' @'
  IfFileExists "$INSTDIR\WeaselServer.exe" 0 +2
  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'
'@ @'
  IfFileExists "$INSTDIR\contextime-context-service.exe" 0 +2
  ExecWait '"$INSTDIR\contextime-context-service.exe" --quit'
  IfFileExists "$INSTDIR\WeaselServer.exe" 0 +2
  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'
'@

Replace-LiteralRequired 'output/install.nsi' @'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEServer" "$INSTDIR\WeaselServer.exe"
  ; Start ContextIMEServer and force a Chinese-mode baseline.
'@ @'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEServer" "$INSTDIR\WeaselServer.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEContextService" '"$INSTDIR\contextime-context-service.exe"'
  ; Context Service is an optional companion process. The IME keeps normal
  ; Weasel/librime input when this process is unavailable.
  Exec "$INSTDIR\contextime-context-service.exe"
  ; Start ContextIMEServer and force a Chinese-mode baseline.
'@

Replace-LiteralRequired 'output/install.nsi' @'
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEServer"
'@ @'
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEServer"
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "ContextIMEContextService"
'@ 2

Replace-LiteralRequired 'output/install.nsi' @'
Section "Uninstall"

  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'
'@ @'
Section "Uninstall"

  IfFileExists "$INSTDIR\contextime-context-service.exe" 0 +2
  ExecWait '"$INSTDIR\contextime-context-service.exe" --quit'
  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'
'@

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($required in @(
  'ContextIME 0.3.0 Preview',
  'contextime-0.3.0-preview-installer.exe',
  'File "contextime-context-service.exe"',
  'ContextIMEContextService',
  'contextime-context-service.exe" --quit',
  'Exec "$INSTDIR\contextime-context-service.exe"'
)) {
  if (-not $installer.Contains($required)) {
    throw "Context Service installer lifecycle audit missing: $required"
  }
}
if ([regex]::Matches($installer, 'ContextIMEContextService').Count -ne 3) {
  throw 'Context Service autorun must be written once and removed in upgrade and uninstall paths.'
}
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8WithBom)

[PSCustomObject]@{
  version = '0.3.0-preview'
  route = 'M3 Context Service companion lifecycle'
  executable = 'contextime-context-service.exe'
  autorunValue = 'ContextIMEContextService'
  lifecycle = @('package', 'single-instance start', 'quit before upgrade', 'restart', 'quit before uninstall')
  contextServiceOnKeyPath = $false
  windowsServiceManagerUsed = $false
  runtimeFallback = 'Context Service unavailable leaves ordinary Weasel/librime input active.'
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.3.0-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.3.0-fix-report.json'
