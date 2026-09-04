[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
  [string]$NodeRoot = (Join-Path $env:ProgramData 'ContextIME\LanTestNode'),
  [string]$ShareName = 'ContextIMELanTest$',
  [string]$TaskPath = '\ContextIME\',
  [string]$TaskName = 'LAN Test Node',
  [switch]$RemoveNodeData
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') { throw 'The ContextIME LAN node uninstaller requires Windows.' }
$principal = [System.Security.Principal.WindowsPrincipal]::new([System.Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'Run this uninstaller from an elevated Windows PowerShell session on the LAN test laptop.'
}

$NodeRoot = [System.IO.Path]::GetFullPath($NodeRoot)
$expectedNodeRoot = [System.IO.Path]::GetFullPath((Join-Path $env:ProgramData 'ContextIME\LanTestNode')).TrimEnd('\')
if (-not $NodeRoot.TrimEnd('\').Equals($expectedNodeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Refusing to uninstall an unexpected node root: $NodeRoot"
}

$task = Get-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName -ErrorAction SilentlyContinue
if ($null -ne $task -and $PSCmdlet.ShouldProcess("${TaskPath}${TaskName}", 'Unregister scheduled task')) {
  Stop-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName -ErrorAction SilentlyContinue
  Unregister-ScheduledTask -TaskPath $TaskPath -TaskName $TaskName -Confirm:$false
}

$share = Get-SmbShare -Name $ShareName -ErrorAction SilentlyContinue
if ($null -ne $share) {
  $sharePath = [System.IO.Path]::GetFullPath([string]$share.Path)
  $expectedExchange = [System.IO.Path]::GetFullPath((Join-Path $NodeRoot 'exchange'))
  if ($sharePath -ne $expectedExchange) {
    throw "Refusing to remove SMB share $ShareName because it points outside this node: $sharePath"
  }
  if ($PSCmdlet.ShouldProcess($ShareName, 'Remove SMB share')) {
    Remove-SmbShare -Name $ShareName -Force
  }
}

if ($RemoveNodeData) {
  if ($PSCmdlet.ShouldProcess($NodeRoot, 'Recursively remove LAN node packages, requests and evidence')) {
    Remove-Item -LiteralPath $NodeRoot -Recurse -Force
  }
} else {
  Write-Host "Preserved LAN node data: $NodeRoot"
}
