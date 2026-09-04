#include "contextime/context_refresh_worker.h"

#if !defined(_WIN32)
#error context_refresh_worker_win_test.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using contextime::ContextRefreshWorker;
using contextime::ContextRefreshWorkerOptions;
using contextime::ContextServiceServeStatus;
using contextime::ContextSnapshot;
using contextime::DecisionCacheReadState;
using contextime::DecisionSource;
using contextime::DesiredMode;
using contextime::InputMode;
using contextime::OptionalMode;

int failures = 0;
int assertions = 0;
unsigned int pipe_sequence = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::wstring UniquePipe(const wchar_t* fixture) {
  return std::wstring(L"\\\\.\\pipe\\ContextIME.ContextService.v1.HostTest.") +
         std::to_wstring(GetCurrentProcessId()) + L"." + fixture + L"." +
         std::to_wstring(++pipe_sequence);
}

bool WaitForPipe(const wchar_t* pipe_name) {
  const ULONGLONG deadline = GetTickCount64() + 2000;
  while (GetTickCount64() < deadline) {
    if (WaitNamedPipeW(pipe_name, 0)) {
      return true;
    }
    Sleep(1);
  }
  return false;
}

void SignalReady(void* context) noexcept {
  SetEvent(static_cast<HANDLE>(context));
}

void TestRefreshSnapshotRemovesTransientSafety() {
  ContextSnapshot live;
  live.current_mode = InputMode::Chinese;
  live.composition_active = true;
  live.candidate_visible = true;
  live.explicit_user_lock = OptionalMode::Some(InputMode::Chinese);
  live.now_ms = 1;
  live.manual_override_until_ms = 999;
  live.automation_enabled = false;
  live.context_available = false;
  live.project_rule = OptionalMode::Some(InputMode::English);

  const ContextSnapshot request =
      contextime::PrepareContextRefreshSnapshot(live, 1234);
  Expect(!request.composition_active,
         "refresh request removes composition state");
  Expect(!request.candidate_visible, "refresh request removes candidate state");
  Expect(!request.explicit_user_lock.has_value,
         "refresh request removes explicit lock");
  Expect(request.manual_override_until_ms == 0,
         "refresh request removes manual deadline");
  Expect(request.automation_enabled,
         "refresh request evaluates stable automatic policy");
  Expect(request.context_available,
         "refresh request enables available context evaluation");
  Expect(request.now_ms == 1234, "refresh request uses worker clock");
  Expect(request.project_rule.has_value &&
             request.project_rule.value == InputMode::English,
         "refresh request preserves stable project rule");
}

void TestWorkerPublishesAndReaderRechecksSafety() {
  const std::wstring pipe_name = UniquePipe(L"publish");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "worker service pipe ready");

  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Expect(ready != nullptr, "worker callback event created");

  ContextRefreshWorker worker;
  ContextRefreshWorkerOptions options;
  options.pipe_name = pipe_name.c_str();
  options.service_timeout_ms = 100;
  options.refresh_interval_ms = 5000;
  options.decision_ttl_ms = 5000;
  Expect(worker.Start(options, &SignalReady, ready), "worker starts");
  Expect(worker.running(), "worker reports running");

  ContextSnapshot live;
  live.current_mode = InputMode::Chinese;
  live.composition_active = true;
  live.manual_override_until_ms = GetTickCount64() + 5000;
  live.syntax_context = OptionalMode::Some(InputMode::English);
  worker.Activate(live);

  Expect(WaitForSingleObject(ready, 2000) == WAIT_OBJECT_0,
         "worker publishes and notifies without TSF-thread IPC");
  server.join();
  Expect(serve_status == ContextServiceServeStatus::Served,
         "worker request served");

  DecisionCacheReadState state;
  state.current_mode = InputMode::Chinese;
  state.now_ms = GetTickCount64();
  const auto automatic = worker.Read(state);
  Expect(automatic.desired_mode == DesiredMode::English,
         "worker cache exposes stable English decision");
  Expect(automatic.source == DecisionSource::SyntaxContext,
         "worker cache preserves stable decision source");
  Expect(automatic.should_switch, "worker cache requests mode switch");

  state.composition_active = true;
  const auto protected_decision = worker.Read(state);
  Expect(protected_decision.desired_mode == DesiredMode::Keep,
         "reader rechecks live composition before apply");
  Expect(protected_decision.source == DecisionSource::CompositionProtection,
         "reader reports composition protection");

  state.composition_active = false;
  state.manual_override_until_ms = state.now_ms + 1;
  const auto manual = worker.Read(state);
  Expect(manual.desired_mode == DesiredMode::Keep,
         "reader rechecks live manual override before apply");
  Expect(manual.source == DecisionSource::ManualOverrideProtection,
         "reader reports manual protection");

  worker.Deactivate();
  const auto inactive = worker.Read(state);
  Expect(inactive.desired_mode == DesiredMode::Keep,
         "inactive worker never exposes stale decision");
  Expect(inactive.source == DecisionSource::ContextUnavailable,
         "inactive worker reports unavailable context");

  worker.Stop();
  Expect(!worker.running(), "worker stops and joins");
  CloseHandle(ready);
}

void TestMissingServiceKeepsOrdinaryInput() {
  const std::wstring pipe_name = UniquePipe(L"missing");
  ContextRefreshWorker worker;
  ContextRefreshWorkerOptions options;
  options.pipe_name = pipe_name.c_str();
  options.service_timeout_ms = 5;
  options.refresh_interval_ms = 5000;
  options.decision_ttl_ms = 100;
  Expect(worker.Start(options, nullptr, nullptr),
         "worker starts without service");

  ContextSnapshot live;
  live.current_mode = InputMode::English;
  live.syntax_context = OptionalMode::Some(InputMode::Chinese);
  worker.Activate(live);

  DecisionCacheReadState state;
  state.current_mode = InputMode::English;
  state.now_ms = GetTickCount64();
  state.explicit_user_lock = OptionalMode::Some(InputMode::Chinese);
  const auto lock_before_refresh = worker.Read(state);
  Expect(lock_before_refresh.desired_mode == DesiredMode::Chinese,
         "explicit lock works before a cache generation arrives");
  Expect(lock_before_refresh.source == DecisionSource::ExplicitUserLock,
         "pre-refresh decision preserves explicit lock priority");

  Sleep(50);

  state.explicit_user_lock = OptionalMode::None();
  state.now_ms = GetTickCount64();
  const auto result = worker.Read(state);
  Expect(result.desired_mode == DesiredMode::Keep,
         "missing service keeps current mode");
  Expect(result.target_mode == InputMode::English,
         "missing service preserves ordinary English input");
  Expect(result.source == DecisionSource::ContextUnavailable,
         "missing service reports unavailable context");
  Expect(!result.should_switch, "missing service never switches mode");
  worker.Stop();
}

}  // namespace

int main() {
  TestRefreshSnapshotRemovesTransientSafety();
  TestWorkerPublishesAndReaderRechecksSafety();
  TestMissingServiceKeepsOrdinaryInput();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Context Refresh Worker Windows: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
