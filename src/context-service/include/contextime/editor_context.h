#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace contextime {

struct ApplicationContext;
struct ContextSnapshot;

constexpr std::size_t kEditorLanguageIdCapacity = 21;
constexpr std::uint64_t kDefaultEditorContextTtlMs = 2000;

enum class EditorSurface : std::uint8_t {
  Unknown = 0,
  Editor,
  IntegratedTerminal,
};

enum class EditorSyntax : std::uint8_t {
  Unknown = 0,
  Code,
  Comment,
  String,
  MarkdownText,
  MarkdownCode,
};

// Adapter updates deliberately contain no document URI, project path, source
// text, selection text, symbol, password, token, or environment value.
struct EditorContextUpdate {
  bool window_focused = false;
  EditorSurface surface = EditorSurface::Unknown;
  EditorSyntax syntax = EditorSyntax::Unknown;
  std::array<char, kEditorLanguageIdCapacity> language_id{};
};

// The Context Service owns this store on its single request thread. Adapter
// state is short lived and only supplies normalized Context Engine rules when
// the real foreground process is VS Code.
class EditorContextStore final {
 public:
  void Publish(const EditorContextUpdate& update, std::uint64_t now_ms,
               std::uint64_t ttl_ms = kDefaultEditorContextTtlMs) noexcept;
  void Invalidate() noexcept;
  void Apply(const ApplicationContext& application, std::uint64_t now_ms,
             ContextSnapshot& snapshot) const noexcept;

 private:
  EditorContextUpdate update_{};
  std::uint64_t expires_at_ms_ = 0;
  bool valid_ = false;
};

const char* ToString(EditorSurface surface) noexcept;
const char* ToString(EditorSyntax syntax) noexcept;

}  // namespace contextime
