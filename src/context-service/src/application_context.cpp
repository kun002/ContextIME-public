#include "contextime/application_context.h"

#include <algorithm>
#include <initializer_list>

namespace contextime {
namespace {

wchar_t AsciiLower(wchar_t value) noexcept {
  return value >= L'A' && value <= L'Z'
             ? static_cast<wchar_t>(value - L'A' + L'a')
             : value;
}

template <std::size_t Capacity>
bool CopyNormalized(std::wstring_view value,
                    std::array<wchar_t, Capacity>& output) noexcept {
  output.fill(L'\0');
  if (value.empty()) {
    return true;
  }
  if (value.size() >= Capacity) {
    return false;
  }
  std::transform(value.begin(), value.end(), output.begin(), AsciiLower);
  return true;
}

std::wstring_view Basename(std::wstring_view path) noexcept {
  const std::size_t separator = path.find_last_of(L"\\/");
  return separator == std::wstring_view::npos ? path
                                              : path.substr(separator + 1);
}

bool IsOneOf(std::wstring_view value,
             std::initializer_list<std::wstring_view> choices) noexcept {
  return std::find(choices.begin(), choices.end(), value) != choices.end();
}

ApplicationKind Classify(std::wstring_view executable_name,
                         std::wstring_view window_class) noexcept {
  if (IsOneOf(window_class,
              {L"consolewindowclass", L"cascadia_hosting_window_class"}) ||
      IsOneOf(executable_name,
              {L"windowsterminal.exe", L"openconsole.exe", L"conhost.exe",
               L"cmd.exe", L"powershell.exe", L"pwsh.exe", L"wsl.exe",
               L"wezterm-gui.exe", L"alacritty.exe"})) {
    return ApplicationKind::Terminal;
  }
  if (IsOneOf(executable_name,
              {L"code.exe", L"code - insiders.exe", L"devenv.exe",
               L"rider64.exe", L"idea64.exe", L"clion64.exe",
               L"pycharm64.exe", L"webstorm64.exe"})) {
    return ApplicationKind::CodeEditor;
  }
  if (IsOneOf(executable_name,
              {L"msedge.exe", L"chrome.exe", L"firefox.exe"})) {
    return ApplicationKind::Browser;
  }
  if (IsOneOf(executable_name,
              {L"notepad.exe", L"winword.exe", L"wordpad.exe",
               L"obsidian.exe"})) {
    return ApplicationKind::DocumentEditor;
  }
  return ApplicationKind::Unknown;
}

}  // namespace

ApplicationContext BuildApplicationContext(
    std::uint32_t process_id, std::uint32_t thread_id,
    std::wstring_view image_path, std::wstring_view window_class) noexcept {
  ApplicationContext result;
  if (process_id == 0 || thread_id == 0) {
    return result;
  }

  const std::wstring_view basename = Basename(image_path);
  const bool executable_copied =
      CopyNormalized(basename, result.executable_name);
  const bool class_copied =
      CopyNormalized(window_class, result.window_class);
  if (!executable_copied) {
    result.executable_name.fill(L'\0');
  }
  if (!class_copied) {
    result.window_class.fill(L'\0');
  }
  if (result.executable_name[0] == L'\0' &&
      result.window_class[0] == L'\0') {
    return result;
  }

  result.available = true;
  result.process_id = process_id;
  result.thread_id = thread_id;
  result.application_kind =
      Classify(result.executable_name.data(), result.window_class.data());
  result.surface_kind = result.application_kind == ApplicationKind::Terminal
                            ? SurfaceKind::Terminal
                            : SurfaceKind::Unknown;
  return result;
}

void ApplyApplicationContext(const ApplicationContext& application,
                             ContextSnapshot& snapshot) noexcept {
  if (!application.available ||
      application.surface_kind != SurfaceKind::Terminal) {
    return;
  }
  if (!snapshot.surface_context.has_value) {
    snapshot.surface_context = OptionalMode::Some(InputMode::English);
  }
  if (!snapshot.application_default.has_value) {
    snapshot.application_default = OptionalMode::Some(InputMode::English);
  }
}

const char* ToString(ApplicationKind kind) noexcept {
  switch (kind) {
    case ApplicationKind::Unknown:
      return "UNKNOWN";
    case ApplicationKind::Terminal:
      return "TERMINAL";
    case ApplicationKind::CodeEditor:
      return "CODE_EDITOR";
    case ApplicationKind::Browser:
      return "BROWSER";
    case ApplicationKind::DocumentEditor:
      return "DOCUMENT_EDITOR";
  }
  return "UNKNOWN";
}

const char* ToString(SurfaceKind kind) noexcept {
  switch (kind) {
    case SurfaceKind::Unknown:
      return "UNKNOWN";
    case SurfaceKind::Terminal:
      return "TERMINAL";
  }
  return "UNKNOWN";
}

}  // namespace contextime
