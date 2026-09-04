#include "contextime/application_context.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

using contextime::ApplicationContext;
using contextime::ApplicationKind;
using contextime::ContextSnapshot;
using contextime::InputMode;
using contextime::OptionalMode;
using contextime::SurfaceKind;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::wstring_view Executable(const ApplicationContext& context) {
  return context.executable_name.data();
}

std::wstring_view WindowClass(const ApplicationContext& context) {
  return context.window_class.data();
}

void TestTerminalNormalizationAndSuggestion() {
  const auto context = contextime::BuildApplicationContext(
      101, 202,
      L"C:\\Program Files\\WindowsApps\\WindowsTerminal.EXE",
      L"CASCADIA_HOSTING_WINDOW_CLASS");
  Expect(context.available, "terminal context available");
  Expect(context.process_id == 101, "terminal process id");
  Expect(context.thread_id == 202, "terminal thread id");
  Expect(Executable(context) == L"windowsterminal.exe",
         "only lowercase executable basename retained");
  Expect(Executable(context).find(L'\\') == std::wstring_view::npos &&
             Executable(context).find(L'/') == std::wstring_view::npos,
         "full executable path discarded");
  Expect(WindowClass(context) == L"cascadia_hosting_window_class",
         "window class normalized");
  Expect(context.application_kind == ApplicationKind::Terminal,
         "terminal application classification");
  Expect(context.surface_kind == SurfaceKind::Terminal,
         "terminal surface classification");

  ContextSnapshot snapshot;
  contextime::ApplyApplicationContext(context, snapshot);
  Expect(snapshot.surface_context.has_value &&
             snapshot.surface_context.value == InputMode::English,
         "terminal surface suggests English");
  Expect(snapshot.application_default.has_value &&
             snapshot.application_default.value == InputMode::English,
         "terminal application fallback suggests English");
}

void TestKnownApplicationsDoNotGuessSurface() {
  const auto code = contextime::BuildApplicationContext(
      1, 2, L"C:/Users/test/AppData/Local/Programs/Code.EXE",
      L"Chrome_WidgetWin_1");
  Expect(code.application_kind == ApplicationKind::CodeEditor,
         "VS Code classified as editor");
  Expect(code.surface_kind == SurfaceKind::Unknown,
         "VS Code surface remains unknown");
  ContextSnapshot code_snapshot;
  contextime::ApplyApplicationContext(code, code_snapshot);
  Expect(!code_snapshot.surface_context.has_value &&
             !code_snapshot.application_default.has_value,
         "editor classification does not guess a mode");

  const auto browser = contextime::BuildApplicationContext(
      3, 4, L"C:\\Program Files\\Microsoft\\Edge\\msedge.exe",
      L"Chrome_WidgetWin_1");
  Expect(browser.application_kind == ApplicationKind::Browser,
         "Edge classified as browser");
  Expect(browser.surface_kind == SurfaceKind::Unknown,
         "browser input surface remains unknown");

  const auto document = contextime::BuildApplicationContext(
      5, 6, L"C:\\Windows\\System32\\notepad.exe", L"Notepad");
  Expect(document.application_kind == ApplicationKind::DocumentEditor,
         "Notepad classified as document editor");
  Expect(document.surface_kind == SurfaceKind::Unknown,
         "document surface remains unknown");
}

void TestWindowClassFallbackAndCallerPrecedence() {
  const auto context = contextime::BuildApplicationContext(
      7, 8, {}, L"ConsoleWindowClass");
  Expect(context.available, "class-only context available");
  Expect(Executable(context).empty(), "inaccessible process is not guessed");
  Expect(context.application_kind == ApplicationKind::Terminal,
         "console class identifies terminal");

  ContextSnapshot snapshot;
  snapshot.surface_context = OptionalMode::Some(InputMode::Chinese);
  snapshot.application_default = OptionalMode::Some(InputMode::Chinese);
  contextime::ApplyApplicationContext(context, snapshot);
  Expect(snapshot.surface_context.value == InputMode::Chinese,
         "existing surface rule is preserved");
  Expect(snapshot.application_default.value == InputMode::Chinese,
         "existing application rule is preserved");
}

void TestUnavailableAndOversizedInputs() {
  const auto no_process = contextime::BuildApplicationContext(
      0, 1, L"C:\\Windows\\notepad.exe", L"Notepad");
  Expect(!no_process.available, "zero process id is unavailable");
  Expect(no_process.process_id == 0 && no_process.thread_id == 0,
         "unavailable result does not retain partial ids");

  const auto no_identity =
      contextime::BuildApplicationContext(1, 2, {}, {});
  Expect(!no_identity.available, "missing executable and class unavailable");

  const std::wstring oversized(contextime::kApplicationNameCapacity, L'x');
  const auto class_fallback = contextime::BuildApplicationContext(
      3, 4, oversized, L"Chrome_WidgetWin_1");
  Expect(class_fallback.available, "oversized basename discarded safely");
  Expect(Executable(class_fallback).empty(),
         "oversized basename is not truncated");
  Expect(class_fallback.application_kind == ApplicationKind::Unknown,
         "unknown class does not guess application");
}

void TestStableNames() {
  Expect(std::string(contextime::ToString(ApplicationKind::Terminal)) ==
             "TERMINAL",
         "terminal application stable name");
  Expect(std::string(contextime::ToString(ApplicationKind::CodeEditor)) ==
             "CODE_EDITOR",
         "editor stable name");
  Expect(std::string(contextime::ToString(SurfaceKind::Unknown)) == "UNKNOWN",
         "unknown surface stable name");
}

}  // namespace

int main() {
  static_assert(noexcept(contextime::BuildApplicationContext(
                    0, 0, std::wstring_view{}, std::wstring_view{})),
                "application normalization must remain noexcept");
  static_assert(noexcept(contextime::ApplyApplicationContext(
                    std::declval<const ApplicationContext&>(),
                    std::declval<ContextSnapshot&>())),
                "application context application must remain noexcept");

  TestTerminalNormalizationAndSuggestion();
  TestKnownApplicationsDoNotGuessSurface();
  TestWindowClassFallbackAndCallerPrecedence();
  TestUnavailableAndOversizedInputs();
  TestStableNames();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Application Context: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
