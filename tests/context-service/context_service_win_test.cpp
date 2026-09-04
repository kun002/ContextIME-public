#include "contextime/application_context.h"
#include "contextime/context_protocol.h"
#include "contextime/context_service.h"
#include "contextime/editor_context.h"

#if !defined(_WIN32)
#error context_service_win_test.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

using contextime::ContextServiceResult;
using contextime::ContextServiceServeStatus;
using contextime::ContextServiceTransportStatus;
using contextime::ContextSnapshot;
using contextime::DecisionSource;
using contextime::DesiredMode;
using contextime::EditorContextStore;
using contextime::EditorSurface;
using contextime::EditorSyntax;
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
  return std::wstring(L"\\\\.\\pipe\\ContextIME.ContextService.v1.Test.") +
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

bool WriteAll(HANDLE pipe, const std::uint8_t* data, std::size_t size) {
  std::size_t transferred = 0;
  while (transferred < size) {
    DWORD chunk = 0;
    const DWORD remaining =
        static_cast<DWORD>(size - transferred);
    const BOOL ok = WriteFile(pipe, data + transferred, remaining,
                              &chunk, nullptr);
    if (!ok || chunk == 0) {
      return false;
    }
    transferred += chunk;
  }
  return true;
}

bool ReadAll(HANDLE pipe, std::uint8_t* data, std::size_t size) {
  std::size_t transferred = 0;
  while (transferred < size) {
    DWORD chunk = 0;
    const DWORD remaining =
        static_cast<DWORD>(size - transferred);
    const BOOL ok = ReadFile(pipe, data + transferred, remaining,
                             &chunk, nullptr);
    if (!ok || chunk == 0) {
      return false;
    }
    transferred += chunk;
  }
  return true;
}

bool SendEditorContextUpdate(
    const wchar_t* pipe_name,
    const contextime::protocol::EditorContextRequestFrame& request_frame,
    contextime::protocol::EditorContextResponse& response) {
  HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return false;
  }
  contextime::protocol::EditorContextResponseFrame response_frame;
  const bool wrote =
      WriteAll(pipe, request_frame.data(), request_frame.size());
  const bool read =
      wrote && ReadAll(pipe, response_frame.data(), response_frame.size());
  const bool decoded =
      read && contextime::protocol::DecodeEditorContextResponse(
                  response_frame, response) ==
                  contextime::protocol::CodecStatus::Ok;
  std::uint8_t acknowledgement =
      contextime::protocol::kResponseAcknowledgement;
  const bool acknowledged =
      decoded && WriteAll(pipe, &acknowledgement,
                          sizeof(acknowledgement));
  CloseHandle(pipe);
  return acknowledged;
}

bool SendEditorContextUpdate(
    const wchar_t* pipe_name,
    const contextime::protocol::EditorContextRequest& request,
    contextime::protocol::EditorContextResponse& response) {
  contextime::protocol::EditorContextRequestFrame request_frame;
  if (contextime::protocol::EncodeEditorContextRequest(request,
                                                       request_frame) !=
      contextime::protocol::CodecStatus::Ok) {
    return false;
  }
  return SendEditorContextUpdate(pipe_name, request_frame, response);
}

void ExpectUnavailableFallback(const ContextServiceResult& result,
                               InputMode current_mode,
                               ContextServiceTransportStatus status,
                               const char* fixture) {
  Expect(result.transport_status == status,
         (std::string(fixture) + ": transport status").c_str());
  Expect(result.decision.desired_mode == DesiredMode::Keep,
         (std::string(fixture) + ": desired KEEP").c_str());
  Expect(result.decision.target_mode == current_mode,
         (std::string(fixture) + ": current target preserved").c_str());
  Expect(result.decision.source == DecisionSource::ContextUnavailable,
         (std::string(fixture) + ": unavailable source").c_str());
  Expect(!result.decision.should_switch,
         (std::string(fixture) + ": no switch").c_str());
}

void TestEndpointIsolation() {
  const std::wstring endpoint = contextime::kContextServicePipeName;
  Expect(endpoint.find(L"ContextIME.ContextService.v1") !=
             std::wstring::npos,
         "versioned Context Service endpoint");
  Expect(endpoint.find(L"ContextIMENamedPipe") == std::wstring::npos,
         "Context Service does not reuse input engine pipe");
}

void TestForegroundCaptureIsBounded() {
  const auto context = contextime::CaptureForegroundApplication();
  const std::wstring_view executable(context.executable_name.data());
  const std::wstring_view window_class(context.window_class.data());
  Expect(!context.available || context.process_id != 0,
         "captured application has a process id");
  Expect(executable.find(L'\\') == std::wstring_view::npos &&
             executable.find(L'/') == std::wstring_view::npos,
         "capture never retains full image path");
  Expect(context.executable_name.back() == L'\0',
         "captured executable is bounded");
  Expect(context.window_class.back() == L'\0',
         "captured window class is bounded");
  Expect(!context.available || !executable.empty() || !window_class.empty(),
         "available capture has a normalized identity");
}

void TestMissingServiceFallsBackImmediately() {
  const std::wstring pipe_name = UniquePipe(L"missing");
  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::English;
  snapshot.syntax_context = OptionalMode::Some(InputMode::Chinese);
  const auto started = std::chrono::steady_clock::now();
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 25, 101);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  ExpectUnavailableFallback(result, InputMode::English,
                            ContextServiceTransportStatus::ServiceUnavailable,
                            "missing service");
  Expect(elapsed.count() < 150,
         "missing service fallback does not consume timeout budget");
}

void TestServiceDecision() {
  const std::wstring pipe_name = UniquePipe(L"decision");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "decision server ready");

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.syntax_context = OptionalMode::Some(InputMode::English);
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 1000, 202);
  server.join();

  Expect(serve_status == ContextServiceServeStatus::Served,
         "decision server served request");
  Expect(result.transport_status == ContextServiceTransportStatus::Ok,
         "decision transport OK");
  Expect(result.request_id == 202, "decision request id preserved");
  Expect(result.decision.desired_mode == DesiredMode::English,
         "syntax decision is English");
  Expect(result.decision.target_mode == InputMode::English,
         "syntax target is English");
  Expect(result.decision.source == DecisionSource::SyntaxContext,
         "syntax source preserved");
  Expect(result.decision.should_switch, "syntax decision requests switch");
}

void TestServiceAppearanceWithinDeadline() {
  const std::wstring pipe_name = UniquePipe(L"delayed-appearance");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    Sleep(10);
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.syntax_context = OptionalMode::Some(InputMode::English);
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 250, 203);
  if (result.transport_status != ContextServiceTransportStatus::Ok &&
      WaitForPipe(pipe_name.c_str())) {
    HANDLE wake = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
      CloseHandle(wake);
    }
  }
  server.join();

  Expect(serve_status == ContextServiceServeStatus::Served,
         "delayed service served retried request");
  Expect(result.transport_status == ContextServiceTransportStatus::Ok,
         "client retries a pipe instance appearing within its deadline");
  Expect(result.decision.desired_mode == DesiredMode::English,
         "retried request preserves decision");
}

void TestCompositionProtectionAcrossPipe() {
  const std::wstring pipe_name = UniquePipe(L"composition");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "composition server ready");

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.composition_active = true;
  snapshot.explicit_user_lock = OptionalMode::Some(InputMode::English);
  snapshot.user_rule = OptionalMode::Some(InputMode::English);
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 1000, 303);
  server.join();

  Expect(serve_status == ContextServiceServeStatus::Served,
         "composition server served request");
  Expect(result.transport_status == ContextServiceTransportStatus::Ok,
         "composition transport OK");
  Expect(result.decision.desired_mode == DesiredMode::Keep,
         "composition decision keeps mode");
  Expect(result.decision.target_mode == InputMode::Chinese,
         "composition preserves current mode");
  Expect(result.decision.source == DecisionSource::CompositionProtection,
         "composition source crosses protocol");
  Expect(!result.decision.should_switch,
         "composition does not request a switch");
}

void TestEditorContextUpdateAcrossPipe() {
  const std::wstring pipe_name = UniquePipe(L"editor-context");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  EditorContextStore store;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000, &store);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "editor context server ready");

  contextime::protocol::EditorContextRequest request;
  request.request_id = 707;
  request.update.window_focused = true;
  request.update.surface = EditorSurface::Editor;
  request.update.syntax = EditorSyntax::Comment;
  const std::string language = "typescript";
  std::copy(language.begin(), language.end(),
            request.update.language_id.begin());
  contextime::protocol::EditorContextResponse response;
  const bool sent =
      SendEditorContextUpdate(pipe_name.c_str(), request, response);
  server.join();

  Expect(sent, "editor context request completed");
  Expect(serve_status == ContextServiceServeStatus::Served,
         "editor context server served request");
  Expect(response.request_id == request.request_id,
         "editor context response id preserved");
  Expect(response.status == contextime::protocol::ResponseStatus::Ok,
         "editor context response status OK");
  Expect(response.accepted, "editor context update accepted");

  const auto vscode = contextime::BuildApplicationContext(
      31, 32, L"C:\\Users\\test\\AppData\\Local\\Programs\\Code.exe",
      L"Chrome_WidgetWin_1");
  ContextSnapshot snapshot;
  store.Apply(vscode, GetTickCount64(), snapshot);
  Expect(snapshot.syntax_context.has_value &&
             snapshot.syntax_context.value == InputMode::Chinese,
         "accepted editor update reaches shared Context Engine snapshot");
}

void TestMalformedEditorContextIsRejectedAcrossPipe() {
  const std::wstring pipe_name = UniquePipe(L"editor-malformed");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  EditorContextStore store;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000, &store);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "malformed editor server ready");

  contextime::protocol::EditorContextRequest request;
  request.request_id = 708;
  request.update.window_focused = true;
  request.update.surface = EditorSurface::Editor;
  request.update.syntax = EditorSyntax::Code;
  request.update.language_id[0] = 'c';
  contextime::protocol::EditorContextRequestFrame request_frame;
  Expect(contextime::protocol::EncodeEditorContextRequest(request,
                                                           request_frame) ==
             contextime::protocol::CodecStatus::Ok,
         "malformed editor fixture encoded before mutation");
  request_frame[20] = 'C';

  contextime::protocol::EditorContextResponse response;
  const bool sent =
      SendEditorContextUpdate(pipe_name.c_str(), request_frame, response);
  server.join();

  Expect(sent, "malformed editor response completed");
  Expect(serve_status == ContextServiceServeStatus::ProtocolError,
         "malformed editor request reports protocol error");
  Expect(response.request_id == request.request_id,
         "malformed editor response id preserved");
  Expect(response.status ==
             contextime::protocol::ResponseStatus::MalformedRequest,
         "malformed editor response status");
  Expect(!response.accepted, "malformed editor update is not accepted");

  ContextSnapshot snapshot;
  store.Apply(contextime::BuildApplicationContext(
                  41, 42, L"C:\\Program Files\\Microsoft VS Code\\Code.exe",
                  L"Chrome_WidgetWin_1"),
              GetTickCount64(), snapshot);
  Expect(!snapshot.syntax_context.has_value,
         "malformed editor update never reaches store");
}

void TestEditorContextWithoutStoreReportsInternalError() {
  const std::wstring pipe_name = UniquePipe(L"editor-no-store");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "editor no-store server ready");

  contextime::protocol::EditorContextRequest request;
  request.request_id = 709;
  request.update.window_focused = true;
  request.update.surface = EditorSurface::Editor;
  request.update.syntax = EditorSyntax::Comment;
  contextime::protocol::EditorContextResponse response;
  const bool sent =
      SendEditorContextUpdate(pipe_name.c_str(), request, response);
  server.join();

  Expect(sent, "editor no-store response completed");
  Expect(serve_status == ContextServiceServeStatus::Served,
         "valid editor request receives an application-level response");
  Expect(response.status ==
             contextime::protocol::ResponseStatus::InternalError,
         "missing editor store reports internal error");
  Expect(!response.accepted, "missing editor store cannot accept update");
}

void RunFakeServer(const wchar_t* pipe_name, DWORD stall_ms) {
  HANDLE pipe = CreateNamedPipeW(
      pipe_name, PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return;
  }
  const BOOL connected = ConnectNamedPipe(pipe, nullptr)
                             ? TRUE
                             : GetLastError() == ERROR_PIPE_CONNECTED;
  if (connected && stall_ms != 0) {
    Sleep(stall_ms);
  }
  if (connected) {
    DisconnectNamedPipe(pipe);
  }
  CloseHandle(pipe);
}

void TestStalledServiceTimesOut() {
  const std::wstring pipe_name = UniquePipe(L"timeout");
  std::thread server(
      [&] { RunFakeServer(pipe_name.c_str(), 250); });
  Expect(WaitForPipe(pipe_name.c_str()), "stall server ready");

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.application_default = OptionalMode::Some(InputMode::English);
  const auto started = std::chrono::steady_clock::now();
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 25, 404);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);
  server.join();

  ExpectUnavailableFallback(result, InputMode::Chinese,
                            ContextServiceTransportStatus::Timeout,
                            "stalled service");
  Expect(elapsed.count() < 150,
         "stalled service returns before fake server wakes");
}

void TestDisconnectedServiceFallsBack() {
  const std::wstring pipe_name = UniquePipe(L"disconnect");
  std::thread server([&] { RunFakeServer(pipe_name.c_str(), 0); });
  Expect(WaitForPipe(pipe_name.c_str()), "disconnect server ready");

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::English;
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 500, 505);
  server.join();
  ExpectUnavailableFallback(result, InputMode::English,
                            ContextServiceTransportStatus::Disconnected,
                            "disconnected service");
}

void TestServiceReportedUnavailableContext() {
  const std::wstring pipe_name = UniquePipe(L"unavailable-context");
  ContextServiceServeStatus serve_status =
      ContextServiceServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneContextServiceConnection(
        pipe_name.c_str(), 1000);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "unavailable context server ready");

  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.context_available = false;
  snapshot.syntax_context = OptionalMode::Some(InputMode::English);
  const auto result = contextime::EvaluateViaContextService(
      pipe_name.c_str(), snapshot, 1000, 606);
  server.join();

  Expect(serve_status == ContextServiceServeStatus::Served,
         "unavailable context server served request");
  Expect(result.transport_status == ContextServiceTransportStatus::Ok,
         "unavailable context response is valid transport");
  Expect(result.decision.desired_mode == DesiredMode::Keep,
         "unavailable context keeps mode");
  Expect(result.decision.source == DecisionSource::ContextUnavailable,
         "unavailable context engine source");
  Expect(!result.decision.should_switch,
         "unavailable context does not switch");
}

}  // namespace

int main() {
  TestEndpointIsolation();
  TestForegroundCaptureIsBounded();
  TestMissingServiceFallsBackImmediately();
  TestServiceDecision();
  TestServiceAppearanceWithinDeadline();
  TestCompositionProtectionAcrossPipe();
  TestEditorContextUpdateAcrossPipe();
  TestMalformedEditorContextIsRejectedAcrossPipe();
  TestEditorContextWithoutStoreReportsInternalError();
  TestStalledServiceTimesOut();
  TestDisconnectedServiceFallsBack();
  TestServiceReportedUnavailableContext();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Context Service Windows: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
