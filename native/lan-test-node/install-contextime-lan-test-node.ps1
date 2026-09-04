[CmdletBinding()]
param(
  [string]$NodeRoot = (Join-Path $env:ProgramData 'ContextIME\LanTestNode'),
  [string]$ShareName = 'ContextIMELanTest$',
  [string]$RunAsAccount = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
  [string]$OperatorAccount = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
  [string]$TaskPath = '\ContextIME\',
  [string]$TaskName = 'LAN Test Node',
  [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'The ContextIME LAN node installer requires Windows.' }
if ($ShareName -notmatch '^[A-Za-z0-9_$-]{3,80}$') { throw "Invalid SMB share name: $ShareName" }
if ([string]::IsNullOrWhiteSpace($RunAsAccount) -or [string]::IsNullOrWhiteSpace($OperatorAccount)) {
  throw 'RunAsAccount and OperatorAccount are required.'
}

$principal = [System.Security.Principal.WindowsPrincipal]::new([System.Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this installer from an elevated Windows PowerShell session on the LAN test laptop.'
}

$NodeRoot = [System.IO.Path]::GetFullPath($NodeRoot)
$programDataRoot = [System.IO.Path]::GetFullPath($env:ProgramData).TrimEnd('\') + '\'
if (-not $NodeRoot.StartsWith($programDataRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $NodeRoot.TrimEnd('\') -eq $env:ProgramData.TrimEnd('\')) {
  throw "NodeRoot must be a dedicated directory below ProgramData: $NodeRoot"
}

$sourceWorker = Join-Path $PSScriptRoot 'ContextIMELanNodeWorker.ps1'
if (-not (Test-Path -LiteralPath $sourceWorker -PathType Leaf)) {
  throw "The LAN node worker was not found next to the installer: $sourceWorker"
}

$existingShare = Get-SmbShare -Name $ShareName -ErrorAction SilentlyContinue
$existingTask = Get-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName -ErrorAction SilentlyContinue
if (($null -ne $existingShare -or $null -ne $existingTask) -and -not $Force) {
  throw 'The LAN node share or scheduled task already exists. Pass -Force to refresh this node.'
}
$expectedExchangeRoot = [System.IO.Path]::GetFullPath((Join-Path $NodeRoot 'exchange'))
if ($null -ne $existingShare -and
    [System.IO.Path]::GetFullPath([string]$existingShare.Path) -ne $expectedExchangeRoot) {
  throw "SMB share $ShareName already points to another path: $($existingShare.Path)"
}

$workerRoot = Join-Path $NodeRoot 'worker'
$exchangeRoot = Join-Path $NodeRoot 'exchange'
$workRoot = Join-Path $NodeRoot 'work'
$installedWorker = Join-Path $workerRoot 'ContextIMELanNodeWorker.ps1'
foreach ($path in @(
  $workerRoot,
  $exchangeRoot,
  (Join-Path $exchangeRoot 'packages'),
  (Join-Path $exchangeRoot 'requests'),
  (Join-Path $exchangeRoot 'requests\archive'),
  (Join-Path $exchangeRoot 'runs'),
  $workRoot
)) {
  New-Item -ItemType Directory -Path $path -Force | Out-Null
}
Copy-Item -LiteralPath $sourceWorker -Destination $installedWorker -Force

$inheritance = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit'
$propagation = [System.Security.AccessControl.PropagationFlags]::None
$allow = [System.Security.AccessControl.AccessControlType]::Allow

function Grant-DirectoryAccess {
  param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string[]]$Accounts,
    [Parameter(Mandatory = $true)][System.Security.AccessControl.FileSystemRights]$Rights
  )

  $acl = Get-Acl -LiteralPath $Path
  foreach ($account in $Accounts | Select-Object -Unique) {
    $rule = [System.Security.AccessControl.FileSystemAccessRule]::new(
      $account,
      $Rights,
      $inheritance,
      $propagation,
      $allow)
    $acl.SetAccessRule($rule)
  }
  Set-Acl -LiteralPath $Path -AclObject $acl
}

Grant-DirectoryAccess -Path $exchangeRoot -Accounts @($RunAsAccount, $OperatorAccount) -Rights Modify
Grant-DirectoryAccess -Path $workRoot -Accounts @($RunAsAccount) -Rights Modify
Grant-DirectoryAccess -Path $workerRoot -Accounts @($RunAsAccount) -Rights ReadAndExecute

if ($null -ne $existingShare) {
  Grant-SmbShareAccess -Name $ShareName -AccountName $OperatorAccount -AccessRight Change -Force | Out-Null
} else {
  $administrators = ([System.Security.Principal.SecurityIdentifier]'S-1-5-32-544').Translate([System.Security.Principal.NTAccount]).Value
  New-SmbShare -Name $ShareName -Path $exchangeRoot -ChangeAccess $OperatorAccount -FullAccess $administrators | Out-Null
}

if ($null -ne $existingTask) {
  Unregister-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName -Confirm:$false
}

$windowsPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$escapedWorker = $installedWorker.Replace('"', '""')
$escapedRoot = $NodeRoot.Replace('"', '""')
$actionArguments = "-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$escapedWorker`" -NodeRoot `"$escapedRoot`""
$action = New-ScheduledTaskAction -Execute $windowsPowerShell -Argument $actionArguments -WorkingDirectory $workerRoot
$taskPrincipal = New-ScheduledTaskPrincipal -UserId $RunAsAccount -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet `
  -AllowStartIfOnBatteries `
  -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit (New-TimeSpan -Hours 9) `
  -MultipleInstances IgnoreNew
Register-ScheduledTask `
  -TaskPath $TaskPath `
  -TaskName $TaskName `
  -Action $action `
  -Principal $taskPrincipal `
  -Settings $settings `
  -Description 'Runs signed-by-hash ContextIME UI verification packages only in the logged-on interactive desktop session.' | Out-Null

$registered = Get-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName
if ([string]$registered.Principal.LogonType -notmatch 'Interactive') {
  throw "The registered task is not InteractiveToken: $($registered.Principal.LogonType)"
}
if ([string]$registered.Principal.RunLevel -ne 'Limited') {
  throw "The registered task does not use the limited run level: $($registered.Principal.RunLevel)"
}
$registeredActions = @($registered.Actions)
if ($registeredActions.Count -ne 1 -or
    [string]$registeredActions[0].Execute -ne $windowsPowerShell -or
    [string]$registeredActions[0].Arguments -ne $actionArguments) {
  throw 'The registered task action does not exactly match the installed LAN node worker.'
}

$nodeInfo = [ordered]@{
  schemaVersion = 'contextime.lan-node-install.v1'
  installedUtc = (Get-Date).ToUniversalTime().ToString('o')
  computerName = $env:COMPUTERNAME
  nodeRoot = $NodeRoot
  exchangeRoot = $exchangeRoot
  shareName = $ShareName
  uncPath = "\\$env:COMPUTERNAME\$ShareName"
  taskPath = $TaskPath
  taskName = $TaskName
  runAsAccount = $RunAsAccount
  runAsSid = ([System.Security.Principal.NTAccount]$RunAsAccount).Translate([System.Security.Principal.SecurityIdentifier]).Value
  logonType = [string]$registered.Principal.LogonType
  runLevel = [string]$registered.Principal.RunLevel
  workerPath = $installedWorker
  taskCommand = $windowsPowerShell
  taskArguments = $actionArguments
  operatorAccount = $OperatorAccount
  workerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $installedWorker).Hash.ToLowerInvariant()
}
$json = $nodeInfo | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText((Join-Path $NodeRoot 'node-install.json'), $json, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path $exchangeRoot 'node-info.json'), $json, [System.Text.UTF8Encoding]::new($false))
$json
