#include "contextime/context_engine.h"

namespace contextime {
namespace {

Decision Keep(InputMode current_mode, DecisionSource source) noexcept {
  return {DesiredMode::Keep, current_mode, source, false};
}

Decision Select(InputMode current_mode, OptionalMode selected,
                DecisionSource source) noexcept {
  const DesiredMode desired = selected.value == InputMode::Chinese
                                  ? DesiredMode::Chinese
                                  : DesiredMode::English;
  return {desired, selected.value, source, selected.value != current_mode};
}

}  // namespace

Decision ContextEngine::Evaluate(const ContextSnapshot& snapshot) noexcept {
  // Composition protection is absolute: neither an explicit lock nor any
  // automatic context update may disrupt active composition or candidates.
  if (snapshot.composition_active || snapshot.candidate_visible) {
    return Keep(snapshot.current_mode,
                DecisionSource::CompositionProtection);
  }

  if (snapshot.explicit_user_lock.has_value) {
    return Select(snapshot.current_mode, snapshot.explicit_user_lock,
                  DecisionSource::ExplicitUserLock);
  }

  if (snapshot.now_ms < snapshot.manual_override_until_ms) {
    return Keep(snapshot.current_mode,
                DecisionSource::ManualOverrideProtection);
  }

  if (!snapshot.automation_enabled) {
    return Keep(snapshot.current_mode, DecisionSource::AutomationDisabled);
  }

  if (!snapshot.context_available) {
    return Keep(snapshot.current_mode, DecisionSource::ContextUnavailable);
  }

  if (snapshot.user_rule.has_value) {
    return Select(snapshot.current_mode, snapshot.user_rule,
                  DecisionSource::UserRule);
  }

  if (snapshot.project_rule.has_value) {
    return Select(snapshot.current_mode, snapshot.project_rule,
                  DecisionSource::ProjectRule);
  }

  if (snapshot.syntax_context.has_value) {
    return Select(snapshot.current_mode, snapshot.syntax_context,
                  DecisionSource::SyntaxContext);
  }

  if (snapshot.surface_context.has_value) {
    return Select(snapshot.current_mode, snapshot.surface_context,
                  DecisionSource::SurfaceContext);
  }

  if (snapshot.application_default.has_value) {
    return Select(snapshot.current_mode, snapshot.application_default,
                  DecisionSource::ApplicationDefault);
  }

  return Keep(snapshot.current_mode, DecisionSource::FallbackKeep);
}

const char* ToString(InputMode mode) noexcept {
  switch (mode) {
    case InputMode::Chinese:
      return "CHINESE";
    case InputMode::English:
      return "ENGLISH";
  }
  return "UNKNOWN";
}

const char* ToString(DesiredMode mode) noexcept {
  switch (mode) {
    case DesiredMode::Keep:
      return "KEEP";
    case DesiredMode::Chinese:
      return "CHINESE";
    case DesiredMode::English:
      return "ENGLISH";
  }
  return "UNKNOWN";
}

const char* ToString(DecisionSource source) noexcept {
  switch (source) {
    case DecisionSource::CompositionProtection:
      return "COMPOSITION_PROTECTION";
    case DecisionSource::ExplicitUserLock:
      return "EXPLICIT_USER_LOCK";
    case DecisionSource::ManualOverrideProtection:
      return "MANUAL_OVERRIDE_PROTECTION";
    case DecisionSource::AutomationDisabled:
      return "AUTOMATION_DISABLED";
    case DecisionSource::ContextUnavailable:
      return "CONTEXT_UNAVAILABLE";
    case DecisionSource::UserRule:
      return "USER_RULE";
    case DecisionSource::ProjectRule:
      return "PROJECT_RULE";
    case DecisionSource::SyntaxContext:
      return "SYNTAX_CONTEXT";
    case DecisionSource::SurfaceContext:
      return "SURFACE_CONTEXT";
    case DecisionSource::ApplicationDefault:
      return "APPLICATION_DEFAULT";
    case DecisionSource::FallbackKeep:
      return "FALLBACK_KEEP";
  }
  return "UNKNOWN";
}

}  // namespace contextime
