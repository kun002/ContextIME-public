#include "contextime/editor_context.h"

#include "contextime/application_context.h"

#include <limits>
#include <string_view>

namespace contextime {
namespace {

bool IsVSCode(const ApplicationContext& application) noexcept {
  const std::wstring_view executable(application.executable_name.data());
  return application.available &&
         (executable == L"code.exe" ||
          executable == L"code - insiders.exe");
}

OptionalMode ModeForSyntax(EditorSyntax syntax) noexcept {
  switch (syntax) {
    case EditorSyntax::Code:
    case EditorSyntax::MarkdownCode:
      return OptionalMode::Some(InputMode::English);
    case EditorSyntax::Comment:
    case EditorSyntax::MarkdownText:
      return OptionalMode::Some(InputMode::Chinese);
    case EditorSyntax::Unknown:
    case EditorSyntax::String:
      return OptionalMode::None();
  }
  return OptionalMode::None();
}

std::uint64_t SaturatingDeadline(std::uint64_t now_ms,
                                 std::uint64_t ttl_ms) noexcept {
  const std::uint64_t largest =
      (std::numeric_limits<std::uint64_t>::max)();
  return now_ms > largest - ttl_ms ? largest : now_ms + ttl_ms;
}

}  // namespace

void EditorContextStore::Publish(const EditorContextUpdate& update,
                                 std::uint64_t now_ms,
                                 std::uint64_t ttl_ms) noexcept {
  if (ttl_ms == 0) {
    Invalidate();
    return;
  }
  update_ = update;
  expires_at_ms_ = SaturatingDeadline(now_ms, ttl_ms);
  valid_ = true;
}

void EditorContextStore::Invalidate() noexcept {
  update_ = {};
  expires_at_ms_ = 0;
  valid_ = false;
}

void EditorContextStore::Apply(const ApplicationContext& application,
                               std::uint64_t now_ms,
                               ContextSnapshot& snapshot) const noexcept {
  if (!valid_ || !update_.window_focused || now_ms >= expires_at_ms_ ||
      !IsVSCode(application)) {
    return;
  }

  if (update_.surface == EditorSurface::IntegratedTerminal) {
    if (!snapshot.surface_context.has_value) {
      snapshot.surface_context = OptionalMode::Some(InputMode::English);
    }
    return;
  }
  if (update_.surface != EditorSurface::Editor ||
      snapshot.syntax_context.has_value) {
    return;
  }

  const OptionalMode syntax_mode = ModeForSyntax(update_.syntax);
  if (syntax_mode.has_value) {
    snapshot.syntax_context = syntax_mode;
  }
}

const char* ToString(EditorSurface surface) noexcept {
  switch (surface) {
    case EditorSurface::Unknown:
      return "UNKNOWN";
    case EditorSurface::Editor:
      return "EDITOR";
    case EditorSurface::IntegratedTerminal:
      return "INTEGRATED_TERMINAL";
  }
  return "UNKNOWN";
}

const char* ToString(EditorSyntax syntax) noexcept {
  switch (syntax) {
    case EditorSyntax::Unknown:
      return "UNKNOWN";
    case EditorSyntax::Code:
      return "CODE";
    case EditorSyntax::Comment:
      return "COMMENT";
    case EditorSyntax::String:
      return "STRING";
    case EditorSyntax::MarkdownText:
      return "MARKDOWN_TEXT";
    case EditorSyntax::MarkdownCode:
      return "MARKDOWN_CODE";
  }
  return "UNKNOWN";
}

}  // namespace contextime
