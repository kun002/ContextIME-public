#pragma once

#include <cstdint>

namespace contextime {

enum class InputMode : std::uint8_t {
  Chinese,
  English,
};

enum class DesiredMode : std::uint8_t {
  Keep,
  Chinese,
  English,
};

enum class DecisionSource : std::uint8_t {
  CompositionProtection,
  ExplicitUserLock,
  ManualOverrideProtection,
  AutomationDisabled,
  ContextUnavailable,
  UserRule,
  ProjectRule,
  SyntaxContext,
  SurfaceContext,
  ApplicationDefault,
  FallbackKeep,
};

struct OptionalMode {
  bool has_value = false;
  InputMode value = InputMode::Chinese;

  static constexpr OptionalMode None() noexcept { return {}; }

  static constexpr OptionalMode Some(InputMode mode) noexcept {
    return {true, mode};
  }
};

struct ContextSnapshot {
  InputMode current_mode = InputMode::Chinese;
  bool composition_active = false;
  bool candidate_visible = false;

  // Explicit locks remain available when automation or external context is
  // unavailable. Automatic rules below the manual-override boundary do not.
  OptionalMode explicit_user_lock = OptionalMode::None();
  std::uint64_t now_ms = 0;
  std::uint64_t manual_override_until_ms = 0;
  bool automation_enabled = true;
  bool context_available = true;

  OptionalMode user_rule = OptionalMode::None();
  OptionalMode project_rule = OptionalMode::None();
  OptionalMode syntax_context = OptionalMode::None();
  OptionalMode surface_context = OptionalMode::None();
  OptionalMode application_default = OptionalMode::None();
};

struct Decision {
  DesiredMode desired_mode = DesiredMode::Keep;
  InputMode target_mode = InputMode::Chinese;
  DecisionSource source = DecisionSource::FallbackKeep;
  bool should_switch = false;
};

class ContextEngine final {
 public:
  static Decision Evaluate(const ContextSnapshot& snapshot) noexcept;
};

const char* ToString(InputMode mode) noexcept;
const char* ToString(DesiredMode mode) noexcept;
const char* ToString(DecisionSource source) noexcept;

}  // namespace contextime
