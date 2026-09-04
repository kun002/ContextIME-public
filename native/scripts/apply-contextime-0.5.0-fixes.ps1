[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$WeaselRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path $WeaselRoot).Path
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
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

function Copy-RepositoryFile {
  param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
  )
  $sourcePath = Join-Path $repositoryRoot $Source
  if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "ContextIME candidate bridge source is missing: $Source"
  }
  $destinationPath = Join-Path $root $Destination
  $destinationDirectory = Split-Path -Parent $destinationPath
  [System.IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
  Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
}

Replace-LiteralRequired 'output/install.nsi' '0.4.0 Preview' '0.5.0 Preview'
Replace-LiteralRequired 'output/install.nsi' 'contextime-0.4.0-preview-installer.exe' 'contextime-0.5.0-preview-installer.exe'
Replace-LiteralRequired 'output/install.nsi' '0.4.0-preview' '0.5.0-preview' 2
Replace-LiteralRequired 'output/install.nsi' '0.4.0.0' '0.5.0.0'
Replace-LiteralRequired 'output/README.txt' '0.4.0 Preview' '0.5.0 Preview'

$copies = [ordered]@{
  'src/project-indexer/include/contextime/project_dictionary.h' = 'include/contextime/project_dictionary.h'
  'src/project-indexer/include/contextime/active_project_snapshot.h' = 'include/contextime/active_project_snapshot.h'
  'src/project-indexer/include/contextime/project_candidate_protocol.h' = 'include/contextime/project_candidate_protocol.h'
  'src/project-indexer/include/contextime/project_candidate_client.h' = 'include/contextime/project_candidate_client.h'
  'src/ime-host/include/contextime/project_candidate_refresh_worker.h' = 'include/contextime/project_candidate_refresh_worker.h'
  'src/project-indexer/src/project_dictionary.cpp' = 'RimeWithWeasel/ContextIME/project_dictionary.cpp'
  'src/project-indexer/src/project_candidate_protocol.cpp' = 'RimeWithWeasel/ContextIME/project_candidate_protocol.cpp'
  'src/project-indexer/src/project_candidate_client_win.cpp' = 'RimeWithWeasel/ContextIME/project_candidate_client_win.cpp'
  'src/ime-host/src/project_candidate_refresh_worker_win.cpp' = 'RimeWithWeasel/ContextIME/project_candidate_refresh_worker_win.cpp'
  'src/context-service/src/win_pipe_io.h' = 'RimeWithWeasel/ContextIME/win_pipe_io.h'
}
foreach ($entry in $copies.GetEnumerator()) {
  Copy-RepositoryFile -Source $entry.Key -Destination $entry.Value
}

Replace-LiteralRequired 'include/RimeWithWeasel.h' @'
#include <rime_api.h>
'@ @'
#include <rime_api.h>

#include "contextime/project_candidate_refresh_worker.h"
'@

Replace-LiteralRequired 'include/RimeWithWeasel.h' @'
  SessionStatus() : style(weasel::UIStyle()), __synced(false), session_id(0) {
'@ @'
  SessionStatus()
      : style(weasel::UIStyle()),
        __synced(false),
        session_id(0),
        project_candidate_revision(0) {
'@

Replace-LiteralRequired 'include/RimeWithWeasel.h' @'
  RimeSessionId session_id;
};
'@ @'
  RimeSessionId session_id;
  std::uint64_t project_candidate_revision;
};
'@

Replace-LiteralRequired 'include/RimeWithWeasel.h' @'
  void _UpdateInlinePreeditStatus(WeaselSessionId ipc_id);
'@ @'
  void _UpdateInlinePreeditStatus(WeaselSessionId ipc_id);
  void _RefreshProjectCandidates(WeaselSessionId ipc_id);
'@

Replace-LiteralRequired 'include/RimeWithWeasel.h' @'
  DWORD m_pid;
};
'@ @'
  DWORD m_pid;
  contextime::ProjectCandidateRefreshWorker m_project_candidate_worker;
};
'@

Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.cpp' @'
  m_last_schema_id.clear();
}

void RimeWithWeaselHandler::Finalize() {
'@ @'
  m_last_schema_id.clear();
  contextime::ProjectCandidateRefreshOptions candidate_options;
  (void)m_project_candidate_worker.Start(candidate_options);
}

void RimeWithWeaselHandler::Finalize() {
  m_project_candidate_worker.Stop();
'@

Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.cpp' @'
BOOL RimeWithWeaselHandler::ProcessKeyEvent(KeyEvent keyEvent,
                                            WeaselSessionId ipc_id,
                                            EatLine eat) {
'@ @'
void RimeWithWeaselHandler::_RefreshProjectCandidates(
    WeaselSessionId ipc_id) {
  SessionStatus& session_status = get_session_status(ipc_id);
  if (session_status.status.is_composing) {
    return;
  }
  const auto snapshot = m_project_candidate_worker.Read();
  if (!snapshot ||
      session_status.project_candidate_revision == snapshot->revision) {
    return;
  }
  rime_api->set_property(session_status.session_id,
                         contextime::kProjectCandidatePropertyName,
                         snapshot->property_value.c_str());
  session_status.project_candidate_revision = snapshot->revision;
}

BOOL RimeWithWeaselHandler::ProcessKeyEvent(KeyEvent keyEvent,
                                            WeaselSessionId ipc_id,
                                            EatLine eat) {
'@

Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.cpp' @'
  RimeSessionId session_id = to_session_id(ipc_id);
  Bool handled = rime_api->process_key(session_id, keyEvent.keycode,
'@ @'
  RimeSessionId session_id = to_session_id(ipc_id);
  _RefreshProjectCandidates(ipc_id);
  Bool handled = rime_api->process_key(session_id, keyEvent.keycode,
'@

Replace-LiteralRequired 'RimeWithWeasel/xmake.lua' @'
  add_files("./*.cpp")
'@ @'
  add_includedirs("./ContextIME")
  add_files("./*.cpp", "./ContextIME/*.cpp")
'@

Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.vcxproj' @'
$(SolutionDir)\include;$(BOOST_ROOT);$(SolutionDir)\librime\include;%(AdditionalIncludeDirectories)
'@ @'
$(ProjectDir)ContextIME;$(SolutionDir)\include;$(BOOST_ROOT);$(SolutionDir)\librime\include;%(AdditionalIncludeDirectories)
'@ 8

$compileItems = @'
    <ClCompile Include="ContextIME\project_dictionary.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\project_candidate_protocol.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\project_candidate_client_win.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\project_candidate_refresh_worker_win.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
'@
Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.vcxproj' @'
    <ClCompile Include="RimeWithWeasel.cpp" />
'@ ($compileItems + "`n    <ClCompile Include=`"RimeWithWeasel.cpp`" />")

$headerItems = @'
    <ClInclude Include="..\include\contextime\project_dictionary.h" />
    <ClInclude Include="..\include\contextime\active_project_snapshot.h" />
    <ClInclude Include="..\include\contextime\project_candidate_protocol.h" />
    <ClInclude Include="..\include\contextime\project_candidate_client.h" />
    <ClInclude Include="..\include\contextime\project_candidate_refresh_worker.h" />
    <ClInclude Include="ContextIME\win_pipe_io.h" />
'@
Replace-LiteralRequired 'RimeWithWeasel/RimeWithWeasel.vcxproj' @'
    <ClInclude Include="..\include\RimeWithWeasel.h" />
'@ ($headerItems + "`n    <ClInclude Include=`"..\include\RimeWithWeasel.h`" />")

$installerPath = Resolve-UpstreamPath 'output/install.nsi'
$installer = [System.IO.File]::ReadAllText($installerPath)
foreach ($required in @(
  'ContextIME 0.5.0 Preview',
  'contextime-0.5.0-preview-installer.exe',
  'File "contextime-context-service.exe"',
  'ContextIMEContextService'
)) {
  if (-not $installer.Contains($required)) {
    throw "ContextIME 0.5.0 installer audit missing: $required"
  }
}
[System.IO.File]::WriteAllText($installerPath, $installer, $utf8WithBom)

$handler = [System.IO.File]::ReadAllText(
  (Resolve-UpstreamPath 'RimeWithWeasel/RimeWithWeasel.cpp'))
foreach ($required in @(
  'm_project_candidate_worker.Start(candidate_options)',
  'm_project_candidate_worker.Stop()',
  '_RefreshProjectCandidates(ipc_id)',
  'session_status.status.is_composing',
  'contextime::kProjectCandidatePropertyName',
  'rime_api->process_key(session_id'
)) {
  if (-not $handler.Contains($required)) {
    throw "Weasel candidate bridge audit missing: $required"
  }
}
if ($handler.IndexOf('_RefreshProjectCandidates(ipc_id);') -gt
    $handler.IndexOf('rime_api->process_key(session_id')) {
  throw 'Project candidate property must refresh before librime processes a key.'
}
$worker = [System.IO.File]::ReadAllText(
  (Resolve-UpstreamPath 'RimeWithWeasel/ContextIME/project_candidate_refresh_worker_win.cpp'))
if ($worker.Contains('ProjectDictionaryStore') -or
    $worker.Contains('CreateProcess') -or
    $worker.Contains('node')) {
  throw 'Weasel project candidate worker crossed the storage/process boundary.'
}

[PSCustomObject]@{
  version = '0.5.0-preview'
  route = 'M5.3 active-project candidate bridge'
  projectCandidatePipe = 'ContextIME.ProjectCandidate.v1'
  activeProjectLeaseMs = 3000
  snapshotMaximumCandidates = 256
  snapshotMaximumSymbolBytes = 24576
  inputPath = 'atomic immutable snapshot read; property update only on revision change'
  diskIoOnKeyPath = $false
  ipcOnKeyPath = $false
  nodeOnKeyPath = $false
  compositionRefreshProtected = $true
  runtimeFallback = 'Missing or invalid snapshot yields no project candidates; ordinary librime translators remain active.'
  copiedBridgeSources = $copies.Count
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-0.5.0-fix-report.json' -Encoding utf8

Get-Content 'contextime-0.5.0-fix-report.json'
