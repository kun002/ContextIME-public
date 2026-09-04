#pragma once

#include <atomic>
#include <cstdint>

#include "contextime/context_engine.h"

namespace contextime {

struct DecisionCacheReadState {
  InputMode current_mode = InputMode::Chinese;
  bool composition_active = false;
  bool candidate_visible = false;
  OptionalMode explicit_user_lock = OptionalMode::None();
  std::uint64_t now_ms = 0;
  std::uint64_t manual_override_until_ms = 0;
  bool automation_enabled = true;
};

// Evaluates only the caller's fresh IME safety state. This preserves
// composition, explicit lock, manual override and automation-disabled
// behavior while a new cache generation is still unavailable.
Decision EvaluateDecisionCacheSafety(
    const DecisionCacheReadState& state) noexcept;

// Single-background-writer, multi-reader cache for x64 ContextIME processes.
// Readers perform only bounded lock-free atomic loads and pure Context Engine
// evaluation. They never call IPC, query a clock, allocate, lock, or wait.
class DecisionCache final {
 public:
  DecisionCache() noexcept = default;

  // Only stable automatic decisions may be cached. Transient safety decisions
  // (composition, lock, manual protection, automation disabled) are evaluated
  // from current IME state by Read(). Rejected input atomically invalidates the
  // previous value so a bad refresh cannot leave a stale switch request live.
  bool Publish(const Decision& decision,
               std::uint64_t expires_at_ms) noexcept;

  // The background worker must invalidate on every service transport or
  // protocol failure. The next reader immediately gets CONTEXT_UNAVAILABLE.
  void Invalidate() noexcept;

  Decision Read(const DecisionCacheReadState& state) const noexcept;

 private:
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "ContextIME decision cache requires lock-free x64 atomics");

  void Store(std::uint64_t word, std::uint64_t expires_at_ms) noexcept;
  bool Load(std::uint64_t& word,
            std::uint64_t& expires_at_ms) const noexcept;

  // Publish/Invalidate have one background owner. sequence_ makes readers
  // reject an in-progress two-word update instead of observing torn state.
  std::atomic<std::uint64_t> sequence_{0};
  std::atomic<std::uint64_t> decision_word_{0};
  std::atomic<std::uint64_t> expires_at_ms_{0};
};

}  // namespace contextime
