#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "contextime/context_engine.h"

namespace contextime {

constexpr std::size_t kApplicationNameCapacity = 260;
constexpr std::size_t kWindowClassCapacity = 256;

enum class ApplicationKind : std::uint8_t {
  Unknown = 0,
  Terminal,
  CodeEditor,
  Browser,
  DocumentEditor,
};

enum class SurfaceKind : std::uint8_t {
  Unknown = 0,
  Terminal,
};

struct ApplicationContext {
  bool available = false;
  std::uint32_t process_id = 0;
  std::uint32_t thread_id = 0;
  std::array<wchar_t, kApplicationNameCapacity> executable_name{};
  std::array<wchar_t, kWindowClassCapacity> window_class{};
  ApplicationKind application_kind = ApplicationKind::Unknown;
  SurfaceKind surface_kind = SurfaceKind::Unknown;
};

// image_path may be a full path, but only its normalized basename is copied
// into the result. window titles and document text are deliberately absent.
ApplicationContext BuildApplicationContext(
    std::uint32_t process_id, std::uint32_t thread_id,
    std::wstring_view image_path, std::wstring_view window_class) noexcept;

// Windows implementation. Failure or an inaccessible foreground process
// produces available=false and never guesses an identity.
ApplicationContext CaptureForegroundApplication() noexcept;

// Only reliable built-in defaults are added, and existing caller-provided
// surface/application rules are never overwritten.
void ApplyApplicationContext(const ApplicationContext& application,
                             ContextSnapshot& snapshot) noexcept;

const char* ToString(ApplicationKind kind) noexcept;
const char* ToString(SurfaceKind kind) noexcept;

}  // namespace contextime
