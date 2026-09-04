#include "../stdafx.h"

#include "../WeaselTSF.h"

#include <resource.h>

#include <functional>
#include <limits>

#include "../CandidateList.h"
#include <ResponseParser.h>

namespace {

constexpr wchar_t kContextBridgeWindowClass[] =
    L"ContextIME.TSF.ContextDecisionWindow.v1";
constexpr UINT kContextDecisionReadyMessage = WM_APP + 0x31A;
constexpr std::uint64_t kManualOverrideDurationMs = 5000;
constexpr std::uint64_t kAutomaticOriginDurationMs = 1000;

std::uint64_t SaturatingDeadline(std::uint64_t now,
                                 std::uint64_t duration) noexcept {
  const std::uint64_t largest =
      (std::numeric_limits<std::uint64_t>::max)();
  return now > largest - duration ? largest : now + duration;
}

contextime::InputMode ToContextMode(bool ascii_mode) noexcept {
  return ascii_mode ? contextime::InputMode::English
                    : contextime::InputMode::Chinese;
}

}  // namespace

WeaselTSF::ContextModeSink::ContextModeSink(WeaselTSF* owner) noexcept
    : owner_(owner) {}

bool WeaselTSF::ContextModeSink::ApplyMode(
    contextime::InputMode target_mode) noexcept {
  return owner_ != nullptr && owner_->_ApplyContextMode(target_mode);
}

bool WeaselTSF::_InitContextBridge() noexcept {
  if (_contextBridgeWindow != nullptr) {
    return true;
  }

  _contextOwnerThreadId = GetCurrentThreadId();
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &WeaselTSF::_ContextBridgeWindowProc;
  window_class.hInstance = g_hInst;
  window_class.lpszClassName = kContextBridgeWindowClass;
  if (RegisterClassExW(&window_class) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    _contextOwnerThreadId = 0;
    return false;
  }

  _contextBridgeWindow = CreateWindowExW(
      0, kContextBridgeWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
      nullptr, g_hInst, this);
  if (_contextBridgeWindow == nullptr) {
    _contextOwnerThreadId = 0;
    return false;
  }

  contextime::ContextRefreshWorkerOptions options;
  if (!_contextRefreshWorker.Start(options, &_ContextDecisionReadyCallback,
                                   this)) {
    DestroyWindow(_contextBridgeWindow);
    _contextBridgeWindow = nullptr;
    _contextOwnerThreadId = 0;
    return false;
  }

  if (_contextForeground) {
    _ActivateContextBridge();
  }
  return true;
}

void WeaselTSF::_UninitContextBridge() noexcept {
  _contextForeground = false;
  // Stop joins the worker before the notification window or TSF/COM owners
  // are destroyed. A callback that is already running can therefore only
  // post to a still-live window.
  _contextRefreshWorker.Stop();
  if (_contextBridgeWindow != nullptr) {
    DestroyWindow(_contextBridgeWindow);
    _contextBridgeWindow = nullptr;
  }
  _contextOwnerThreadId = 0;
  _contextPendingAutomaticMode = false;
}

void WeaselTSF::_ActivateContextBridge() noexcept {
  if (!_contextForeground || !_contextRefreshWorker.running()) {
    return;
  }

  contextime::ContextSnapshot snapshot;
  snapshot.current_mode = ToContextMode(_status.ascii_mode);
  snapshot.composition_active =
      _status.composing || _pComposition != nullptr;
  snapshot.candidate_visible =
      _cand != nullptr && _cand->IsContextCandidateVisible();
  snapshot.explicit_user_lock = _contextExplicitUserLock;
  snapshot.now_ms = GetTickCount64();
  snapshot.manual_override_until_ms = _contextManualOverrideUntilMs;
  snapshot.automation_enabled = _contextAutomationEnabled;
  snapshot.context_available = true;
  _contextRefreshWorker.Activate(snapshot);
}

void WeaselTSF::_DeactivateContextBridge() noexcept {
  _contextForeground = false;
  _contextRefreshWorker.Deactivate();
}

void WeaselTSF::_ContextDecisionReadyCallback(void* context) noexcept {
  auto* owner = static_cast<WeaselTSF*>(context);
  if (owner == nullptr) {
    return;
  }
  const HWND window = owner->_contextBridgeWindow;
  if (window != nullptr) {
    (void)PostMessageW(window, kContextDecisionReadyMessage, 0, 0);
  }
}

LRESULT CALLBACK WeaselTSF::_ContextBridgeWindowProc(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }

  auto* owner = reinterpret_cast<WeaselTSF*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == kContextDecisionReadyMessage && owner != nullptr) {
    owner->_OnContextDecisionReady();
    return 0;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

void WeaselTSF::_OnContextDecisionReady() noexcept {
  if (!_contextForeground || !_contextRefreshWorker.running() ||
      GetCurrentThreadId() != _contextOwnerThreadId) {
    return;
  }

  contextime::DecisionCacheReadState state;
  state.current_mode = ToContextMode(_status.ascii_mode);
  state.composition_active =
      _status.composing || _pComposition != nullptr;
  state.candidate_visible =
      _cand != nullptr && _cand->IsContextCandidateVisible();
  state.explicit_user_lock = _contextExplicitUserLock;
  state.now_ms = GetTickCount64();
  state.manual_override_until_ms = _contextManualOverrideUntilMs;
  state.automation_enabled = _contextAutomationEnabled;

  const contextime::Decision decision = _contextRefreshWorker.Read(state);
  (void)contextime::ImeStateApplier::ApplyDecision(
      decision, state.current_mode, _contextModeSink);
}

bool WeaselTSF::_ApplyContextMode(
    contextime::InputMode target_mode) noexcept {
  if (!_contextForeground || GetCurrentThreadId() != _contextOwnerThreadId ||
      _status.composing || _pComposition != nullptr ||
      (_cand != nullptr && _cand->IsContextCandidateVisible())) {
    return false;
  }

  const bool target_ascii = target_mode == contextime::InputMode::English;
  if (_status.ascii_mode == target_ascii) {
    return true;
  }

  const std::uint64_t now = GetTickCount64();
  _contextPendingAutomaticMode = true;
  _contextPendingAutomaticAsciiMode = target_ascii;
  _contextPendingAutomaticUntilMs =
      SaturatingDeadline(now, kAutomaticOriginDurationMs);
  _contextAutomaticApply = true;

  bool confirmed = false;
  try {
    m_client.TrayCommand(target_ascii ? ID_WEASELTRAY_ENABLE_ASCII
                                     : ID_WEASELTRAY_DISABLE_ASCII);
    // Weasel returns the authoritative session status with the next ordinary
    // response. Key code 0 is the existing non-text status refresh used by
    // OnSetThreadFocus; it does not enter the Context Service worker.
    (void)m_client.ProcessKeyEvent(0);
    weasel::ResponseParser parser(nullptr, nullptr, &_status, nullptr,
                                  &_cand->style());
    if (m_client.GetResponseData(std::ref(parser))) {
      _UpdateLanguageBar(_status);
      confirmed = _status.ascii_mode == target_ascii;
    }
  } catch (...) {
    confirmed = false;
  }

  _contextAutomaticApply = false;
  if (confirmed) {
    _contextPendingAutomaticMode = false;
  }
  return confirmed;
}

void WeaselTSF::_MarkContextManualOverride() noexcept {
  const std::uint64_t now = GetTickCount64();
  _contextManualOverrideUntilMs =
      SaturatingDeadline(now, kManualOverrideDurationMs);
  _contextPendingAutomaticMode = false;
}

void WeaselTSF::_ObserveContextMode(bool ascii_mode) noexcept {
  if (!_contextObservedModeInitialized) {
    _contextObservedModeInitialized = true;
    _contextObservedAsciiMode = ascii_mode;
    if (_contextPendingAutomaticMode &&
        _contextPendingAutomaticAsciiMode == ascii_mode) {
      _contextPendingAutomaticMode = false;
    }
    return;
  }
  if (_contextObservedAsciiMode == ascii_mode) {
    return;
  }

  const std::uint64_t now = GetTickCount64();
  bool automatic = _contextAutomaticApply;
  if (!automatic && _contextPendingAutomaticMode) {
    automatic = now <= _contextPendingAutomaticUntilMs &&
                ascii_mode == _contextPendingAutomaticAsciiMode;
  }

  _contextObservedAsciiMode = ascii_mode;
  if (automatic) {
    _contextPendingAutomaticMode = false;
  } else {
    _MarkContextManualOverride();
  }
}
