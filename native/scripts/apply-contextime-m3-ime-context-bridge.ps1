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

function Resolve-RequiredFile {
  param([Parameter(Mandatory = $true)][string]$Path)
  $resolved = [System.IO.Path]::GetFullPath($Path)
  if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
    throw "Required file not found: $resolved"
  }
  return $resolved
}

function Resolve-UpstreamPath {
  param([Parameter(Mandatory = $true)][string]$RelativePath)
  return Resolve-RequiredFile (Join-Path $root $RelativePath)
}

function Replace-LiteralRequired {
  param(
    [Parameter(Mandatory = $true)][string]$RelativePath,
    [Parameter(Mandatory = $true)][string]$Old,
    [Parameter(Mandatory = $true)][string]$New
  )

  $path = Resolve-UpstreamPath $RelativePath
  $content = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedOld = $Old.Replace("`r`n", "`n").Replace("`r", "`n")
  $normalizedNew = $New.Replace("`r`n", "`n").Replace("`r", "`n")
  $count = [regex]::Matches($content, [regex]::Escape($normalizedOld)).Count
  if ($count -ne 1) {
    throw "Expected exactly one bridge anchor in ${RelativePath}, found ${count}: $Old"
  }
  [System.IO.File]::WriteAllText(
    $path,
    $content.Replace($normalizedOld, $normalizedNew),
    $utf8NoBom
  )
}

function Copy-RepositoryFile {
  param(
    [Parameter(Mandatory = $true)][string]$SourceRelativePath,
    [Parameter(Mandatory = $true)][string]$DestinationRelativePath
  )

  $source = Resolve-RequiredFile (Join-Path $repositoryRoot $SourceRelativePath)
  $destination = [System.IO.Path]::GetFullPath((Join-Path $root $DestinationRelativePath))
  $parent = Split-Path -Parent $destination
  [System.IO.Directory]::CreateDirectory($parent) | Out-Null
  [System.IO.File]::WriteAllText(
    $destination,
    [System.IO.File]::ReadAllText($source).Replace("`r`n", "`n").Replace("`r", "`n"),
    $utf8NoBom
  )
}

$copies = @(
  @('src/context-engine/include/contextime/context_engine.h', 'include/contextime/context_engine.h'),
  @('src/context-engine/src/context_engine.cpp', 'WeaselTSF/ContextIME/context_engine.cpp'),
  @('src/context-service/include/contextime/context_protocol.h', 'include/contextime/context_protocol.h'),
  @('src/context-service/include/contextime/context_service.h', 'include/contextime/context_service.h'),
  @('src/context-service/include/contextime/decision_cache.h', 'include/contextime/decision_cache.h'),
  @('src/context-service/include/contextime/editor_context.h', 'include/contextime/editor_context.h'),
  @('src/context-service/src/context_protocol.cpp', 'WeaselTSF/ContextIME/context_protocol.cpp'),
  @('src/context-service/src/context_service_client_win.cpp', 'WeaselTSF/ContextIME/context_service_client_win.cpp'),
  @('src/context-service/src/decision_cache.cpp', 'WeaselTSF/ContextIME/decision_cache.cpp'),
  @('src/context-service/src/win_pipe_io.h', 'WeaselTSF/ContextIME/win_pipe_io.h'),
  @('src/ime-host/include/contextime/context_refresh_worker.h', 'include/contextime/context_refresh_worker.h'),
  @('src/ime-host/include/contextime/ime_state_applier.h', 'include/contextime/ime_state_applier.h'),
  @('src/ime-host/src/context_refresh_worker_win.cpp', 'WeaselTSF/ContextIME/context_refresh_worker_win.cpp'),
  @('src/ime-host/src/ime_state_applier.cpp', 'WeaselTSF/ContextIME/ime_state_applier.cpp'),
  @('native/patches/m3-ime-context-bridge/ContextBridge.cpp', 'WeaselTSF/ContextIME/ContextBridge.cpp')
)
foreach ($copy in $copies) {
  Copy-RepositoryFile -SourceRelativePath $copy[0] -DestinationRelativePath $copy[1]
}

Replace-LiteralRequired 'WeaselTSF/WeaselTSF.h' @'
#include "Globals.h"
#include <WeaselIPC.h>
#include <WeaselIPCData.h>
'@ @'
#include "Globals.h"
#include <WeaselIPC.h>
#include <WeaselIPCData.h>

#include "contextime/context_refresh_worker.h"
#include "contextime/ime_state_applier.h"
'@

Replace-LiteralRequired 'WeaselTSF/WeaselTSF.h' @'
  void _Reconnect();
  std::wstring _GetRootDir();
'@ @'
  void _Reconnect();
  std::wstring _GetRootDir();

  class ContextModeSink final : public contextime::ImeModeSink {
   public:
    explicit ContextModeSink(WeaselTSF* owner) noexcept;
    bool ApplyMode(contextime::InputMode target_mode) noexcept override;

   private:
    WeaselTSF* owner_;
  };

  bool _InitContextBridge() noexcept;
  void _UninitContextBridge() noexcept;
  void _ActivateContextBridge() noexcept;
  void _DeactivateContextBridge() noexcept;
  static void _ContextDecisionReadyCallback(void* context) noexcept;
  static LRESULT CALLBACK _ContextBridgeWindowProc(
      HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept;
  void _OnContextDecisionReady() noexcept;
  bool _ApplyContextMode(contextime::InputMode target_mode) noexcept;
  void _MarkContextManualOverride() noexcept;
  void _ObserveContextMode(bool ascii_mode) noexcept;
'@

Replace-LiteralRequired 'WeaselTSF/WeaselTSF.h' @'
  weasel::Client m_client;
  DWORD _activateFlags;

  /* IME status */
'@ @'
  weasel::Client m_client;
  DWORD _activateFlags;

  contextime::ContextRefreshWorker _contextRefreshWorker;
  ContextModeSink _contextModeSink;
  HWND _contextBridgeWindow = nullptr;
  DWORD _contextOwnerThreadId = 0;
  std::uint64_t _contextManualOverrideUntilMs = 0;
  std::uint64_t _contextPendingAutomaticUntilMs = 0;
  contextime::OptionalMode _contextExplicitUserLock =
      contextime::OptionalMode::None();
  bool _contextForeground = false;
  bool _contextAutomationEnabled = true;
  bool _contextAutomaticApply = false;
  bool _contextPendingAutomaticMode = false;
  bool _contextPendingAutomaticAsciiMode = false;
  bool _contextObservedModeInitialized = false;
  bool _contextObservedAsciiMode = false;

  /* IME status */
'@

Replace-LiteralRequired 'WeaselTSF/WeaselTSF.cpp' 'WeaselTSF::WeaselTSF() {' 'WeaselTSF::WeaselTSF() : _contextModeSink(this) {'
Replace-LiteralRequired 'WeaselTSF/WeaselTSF.cpp' @'
STDAPI WeaselTSF::Deactivate() {
  m_client.EndSession();
'@ @'
STDAPI WeaselTSF::Deactivate() {
  _UninitContextBridge();
  m_client.EndSession();
'@
Replace-LiteralRequired 'WeaselTSF/WeaselTSF.cpp' @'
  _EnsureServerConnected();

  return S_OK;
'@ @'
  _EnsureServerConnected();

  // Context automation is optional. Failure leaves the mature Weasel/librime
  // input path active and never fails TSF activation.
  (void)_InitContextBridge();
  return S_OK;
'@

Replace-LiteralRequired 'WeaselTSF/KeyEventSink.cpp' @'
STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground)
    m_client.FocusIn();
  else {
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}
'@ @'
STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground) {
    _contextForeground = true;
    m_client.FocusIn();
    _ActivateContextBridge();
  } else {
    _DeactivateContextBridge();
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}
'@

Replace-LiteralRequired 'WeaselTSF/CandidateList.h' @'
  bool GetIsReposition() {
    if (_ui)
      return _ui->GetIsReposition();
    else
      return false;
  }

  weasel::UIStyle& style();
'@ @'
  bool GetIsReposition() {
    if (_ui)
      return _ui->GetIsReposition();
    else
      return false;
  }

  bool IsContextCandidateVisible() {
    return _ui != nullptr && _ui->IsShown();
  }

  weasel::UIStyle& style();
'@

# Weasel's EndUI path destroys the candidate window without clearing UIImpl's
# cached `shown` bit. ContextIME reads that bit for candidate protection, so a
# completed composition would otherwise remain protected forever and block the
# next automatic context decision.
Replace-LiteralRequired 'WeaselUI/WeaselUI.cpp' @'
void UI::Destroy(bool full) {
  if (pimpl_) {
    // destroy panel
'@ @'
void UI::Destroy(bool full) {
  if (pimpl_) {
    // Keep IsShown() consistent after TSF EndUI destroys the candidate window.
    pimpl_->shown = false;
    // destroy panel
'@

Replace-LiteralRequired 'WeaselTSF/LanguageBar.cpp' @'
void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
  std::wstring dir{};
'@ @'
void WeaselTSF::_HandleLangBarMenuSelect(UINT wID) {
  if (wID == ID_WEASELTRAY_ENABLE_ASCII ||
      wID == ID_WEASELTRAY_DISABLE_ASCII) {
    _MarkContextManualOverride();
  }
  std::wstring dir{};
'@
Replace-LiteralRequired 'WeaselTSF/LanguageBar.cpp' @'
void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  if (!_pLangBarButton)
'@ @'
void WeaselTSF::_UpdateLanguageBar(weasel::Status stat) {
  _ObserveContextMode(stat.ascii_mode);
  if (!_pLangBarButton)
'@

$contextCompileItems = @'
    <ClCompile Include="ContextIME\ContextBridge.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\context_engine.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\context_protocol.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\context_service_client_win.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\decision_cache.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\context_refresh_worker_win.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="ContextIME\ime_state_applier.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
'@
Replace-LiteralRequired 'WeaselTSF/WeaselTSF.vcxproj' @'
    <ClCompile Include="CandidateList.cpp" />
'@ ("    <ClCompile Include=`"CandidateList.cpp`" />`n" + $contextCompileItems)

Replace-LiteralRequired 'WeaselTSF/xmake.lua' @'
  add_files("./*.cpp", "WeaselTSF.def")
'@ @'
  add_files("./*.cpp", "./ContextIME/*.cpp", "WeaselTSF.def")
'@

$keyPath = [System.IO.File]::ReadAllText((Resolve-UpstreamPath 'WeaselTSF/KeyEventSink.cpp'))
if ($keyPath.Contains('EvaluateViaContextService') -or
    $keyPath.Contains('ContextServicePipe')) {
  throw 'Context Service IPC entered the TSF key-event path.'
}

$bridgeSource = [System.IO.File]::ReadAllText((Resolve-UpstreamPath 'WeaselTSF/ContextIME/ContextBridge.cpp'))
foreach ($required in @(
  'PostMessageW(window, kContextDecisionReadyMessage',
  '_contextRefreshWorker.Stop();',
  '_status.composing || _pComposition != nullptr',
  'IsContextCandidateVisible()',
  '_contextManualOverrideUntilMs',
  'ImeStateApplier::ApplyDecision',
  'ID_WEASELTRAY_ENABLE_ASCII',
  'm_client.GetResponseData',
  'GetCurrentThreadId() != _contextOwnerThreadId'
)) {
  if (-not $bridgeSource.Contains($required)) {
    throw "Generated TSF bridge is missing required boundary: $required"
  }
}

$tsfSource = [System.IO.File]::ReadAllText((Resolve-UpstreamPath 'WeaselTSF/WeaselTSF.cpp'))
if ($tsfSource.IndexOf('_UninitContextBridge();') -gt $tsfSource.IndexOf('m_client.EndSession();')) {
  throw 'Context worker teardown must precede the Weasel session and COM teardown.'
}

$uiSource = [System.IO.File]::ReadAllText((Resolve-UpstreamPath 'WeaselUI/WeaselUI.cpp'))
if (-not $uiSource.Contains('pimpl_->shown = false;')) {
  throw 'Candidate UI destruction must clear the visibility state used by context protection.'
}

[PSCustomObject]@{
  route = 'M3 Weasel TSF context bridge patch'
  upstream = 'Weasel 0.17.4 / 9cc96e20dc71b80876b12f689bb5863c76c2a7ed'
  contextServiceOnKeyPath = $false
  notification = 'ContextIME.TSF.ContextDecisionWindow.v1 / PostMessageW'
  manualOverrideMs = 5000
  automaticOriginGuardMs = 1000
  candidateVisibilityResetOnDestroy = $true
  copiedContextSources = $copies.Count
  runtimeInstallChanged = $false
  realWindowsVerificationRequired = $true
} | ConvertTo-Json -Depth 4 | Set-Content 'contextime-m3-ime-context-bridge-report.json' -Encoding utf8

Get-Content 'contextime-m3-ime-context-bridge-report.json'
