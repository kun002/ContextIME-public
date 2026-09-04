#include "contextime/application_context.h"

#if !defined(_WIN32)
#error foreground_application_win.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <string_view>

namespace contextime {

ApplicationContext CaptureForegroundApplication() noexcept {
  const HWND foreground = GetForegroundWindow();
  if (foreground == nullptr) {
    return {};
  }

  DWORD process_id = 0;
  const DWORD thread_id =
      GetWindowThreadProcessId(foreground, &process_id);
  if (process_id == 0 || thread_id == 0) {
    return {};
  }

  std::array<wchar_t, kWindowClassCapacity> window_class{};
  const int class_length = GetClassNameW(
      foreground, window_class.data(), static_cast<int>(window_class.size()));
  const std::wstring_view class_view =
      class_length > 0
          ? std::wstring_view(window_class.data(),
                              static_cast<std::size_t>(class_length))
          : std::wstring_view{};

  constexpr std::size_t kMaximumWindowsImagePath = 32768;
  std::array<wchar_t, kMaximumWindowsImagePath> image_path{};
  DWORD image_length = 0;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               process_id);
  if (process != nullptr) {
    DWORD capacity = static_cast<DWORD>(image_path.size());
    if (QueryFullProcessImageNameW(process, 0, image_path.data(), &capacity)) {
      image_length = capacity;
    }
    CloseHandle(process);
  }
  const std::wstring_view image_view =
      image_length > 0
          ? std::wstring_view(image_path.data(),
                              static_cast<std::size_t>(image_length))
          : std::wstring_view{};

  return BuildApplicationContext(process_id, thread_id, image_view,
                                 class_view);
}

}  // namespace contextime
