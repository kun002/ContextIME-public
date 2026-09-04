#include "contextime/decision_cache.h"

namespace contextime {
namespace {

constexpr std::uint64_t kValidBit = 1ull << 0u;
constexpr unsigned int kDesiredShift = 1;
constexpr unsigned int kSourceShift = 3;
constexpr std::uint64_t kTwoBitMask = 0x3ull;
constexpr std::uint64_t kFourBitMask = 0xfull;
constexpr int kMaximumReadAttempts = 3;

bool IsStableAutomaticDecision(const Decision& decision) noexcept {
  switch (decision.source) {
    case DecisionSource::UserRule:
    case DecisionSource::ProjectRule:
    case DecisionSource::SyntaxContext:
    case DecisionSource::SurfaceContext:
    case DecisionSource::ApplicationDefault:
      return decision.desired_mode == DesiredMode::Chinese ||
             decision.desired_mode == DesiredMode::English;
    case DecisionSource::FallbackKeep:
      return decision.desired_mode == DesiredMode::Keep &&
             !decision.should_switch;
    case DecisionSource::CompositionProtection:
    case DecisionSource::ExplicitUserLock:
    case DecisionSource::ManualOverrideProtection:
    case DecisionSource::AutomationDisabled:
    case DecisionSource::ContextUnavailable:
      return false;
  }
  return false;
}

bool IsDecisionShapeValid(const Decision& decision) noexcept {
  if (decision.target_mode != InputMode::Chinese &&
      decision.target_mode != InputMode::English) {
    return false;
  }
  switch (decision.desired_mode) {
    case DesiredMode::Keep:
      return !decision.should_switch;
    case DesiredMode::Chinese:
      return decision.target_mode == InputMode::Chinese;
    case DesiredMode::English:
      return decision.target_mode == InputMode::English;
  }
  return false;
}

std::uint64_t Pack(const Decision& decision) noexcept {
  return kValidBit |
         (static_cast<std::uint64_t>(decision.desired_mode)
          << kDesiredShift) |
         (static_cast<std::uint64_t>(decision.source) << kSourceShift);
}

Decision Rebase(std::uint64_t word, InputMode current_mode) noexcept {
  const auto desired = static_cast<DesiredMode>(
      (word >> kDesiredShift) & kTwoBitMask);
  const auto source = static_cast<DecisionSource>(
      (word >> kSourceShift) & kFourBitMask);
  if (desired == DesiredMode::Keep) {
    return {DesiredMode::Keep, current_mode, source, false};
  }
  const InputMode target = desired == DesiredMode::Chinese
                               ? InputMode::Chinese
                               : InputMode::English;
  return {desired, target, source, target != current_mode};
}

ContextSnapshot SafetySnapshot(
    const DecisionCacheReadState& state) noexcept {
  ContextSnapshot snapshot;
  snapshot.current_mode = state.current_mode;
  snapshot.composition_active = state.composition_active;
  snapshot.candidate_visible = state.candidate_visible;
  snapshot.explicit_user_lock = state.explicit_user_lock;
  snapshot.now_ms = state.now_ms;
  snapshot.manual_override_until_ms = state.manual_override_until_ms;
  snapshot.automation_enabled = state.automation_enabled;
  // CONTEXT_UNAVAILABLE is the sentinel meaning no higher local safety rule
  // fired. Cached automatic context is considered only after this evaluation.
  snapshot.context_available = false;
  return snapshot;
}

}  // namespace

Decision EvaluateDecisionCacheSafety(
    const DecisionCacheReadState& state) noexcept {
  return ContextEngine::Evaluate(SafetySnapshot(state));
}

bool DecisionCache::Publish(const Decision& decision,
                            std::uint64_t expires_at_ms) noexcept {
  if (!IsDecisionShapeValid(decision) ||
      !IsStableAutomaticDecision(decision)) {
    Invalidate();
    return false;
  }
  Store(Pack(decision), expires_at_ms);
  return true;
}

void DecisionCache::Invalidate() noexcept { Store(0, 0); }

Decision DecisionCache::Read(
    const DecisionCacheReadState& state) const noexcept {
  const Decision safety = EvaluateDecisionCacheSafety(state);
  if (safety.source != DecisionSource::ContextUnavailable) {
    return safety;
  }

  std::uint64_t word = 0;
  std::uint64_t expires_at_ms = 0;
  if (!Load(word, expires_at_ms) || (word & kValidBit) == 0 ||
      state.now_ms >= expires_at_ms) {
    return safety;
  }
  return Rebase(word, state.current_mode);
}

void DecisionCache::Store(std::uint64_t word,
                          std::uint64_t expires_at_ms) noexcept {
  sequence_.fetch_add(1, std::memory_order_acq_rel);
  decision_word_.store(word, std::memory_order_relaxed);
  expires_at_ms_.store(expires_at_ms, std::memory_order_relaxed);
  sequence_.fetch_add(1, std::memory_order_release);
}

bool DecisionCache::Load(std::uint64_t& word,
                         std::uint64_t& expires_at_ms) const noexcept {
  for (int attempt = 0; attempt < kMaximumReadAttempts; ++attempt) {
    const std::uint64_t before = sequence_.load(std::memory_order_acquire);
    if ((before & 1u) != 0) {
      continue;
    }
    const std::uint64_t candidate_word =
        decision_word_.load(std::memory_order_relaxed);
    const std::uint64_t candidate_expiry =
        expires_at_ms_.load(std::memory_order_relaxed);
    const std::uint64_t after = sequence_.load(std::memory_order_acquire);
    if (before == after && (after & 1u) == 0) {
      word = candidate_word;
      expires_at_ms = candidate_expiry;
      return true;
    }
  }
  return false;
}

}  // namespace contextime
