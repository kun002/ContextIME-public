#include "contextime/decision_cache.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

using contextime::Decision;
using contextime::DecisionCache;
using contextime::DecisionCacheReadState;
using contextime::DecisionSource;
using contextime::DesiredMode;
using contextime::InputMode;
using contextime::OptionalMode;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void ExpectDecision(const Decision& decision, DesiredMode desired,
                    InputMode target, DecisionSource source,
                    bool should_switch, const char* fixture) {
  Expect(decision.desired_mode == desired,
         (std::string(fixture) + ": desired").c_str());
  Expect(decision.target_mode == target,
         (std::string(fixture) + ": target").c_str());
  Expect(decision.source == source,
         (std::string(fixture) + ": source").c_str());
  Expect(decision.should_switch == should_switch,
         (std::string(fixture) + ": switch").c_str());
}

Decision English(DecisionSource source) {
  return {DesiredMode::English, InputMode::English, source, true};
}

Decision Chinese(DecisionSource source) {
  return {DesiredMode::Chinese, InputMode::Chinese, source, true};
}

DecisionCacheReadState BaseState() {
  DecisionCacheReadState state;
  state.current_mode = InputMode::Chinese;
  state.now_ms = 100;
  return state;
}

void TestPublishReadExpiryAndInvalidation() {
  DecisionCache cache;
  auto state = BaseState();
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::ContextUnavailable, false,
                 "empty cache");

  Expect(cache.Publish(English(DecisionSource::SyntaxContext), 200),
         "publish syntax decision");
  ExpectDecision(cache.Read(state), DesiredMode::English, InputMode::English,
                 DecisionSource::SyntaxContext, true, "fresh cache");

  state.current_mode = InputMode::English;
  ExpectDecision(cache.Read(state), DesiredMode::English, InputMode::English,
                 DecisionSource::SyntaxContext, false,
                 "fresh cache rebases switch flag");

  state.now_ms = 200;
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::English,
                 DecisionSource::ContextUnavailable, false,
                 "expiry boundary");

  state.now_ms = 100;
  cache.Invalidate();
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::English,
                 DecisionSource::ContextUnavailable, false,
                 "transport failure invalidation");
}

void TestCurrentSafetyStateOverridesCache() {
  DecisionCache cache;
  Expect(cache.Publish(English(DecisionSource::ApplicationDefault), 1000),
         "publish application decision");
  auto state = BaseState();

  state.composition_active = true;
  state.explicit_user_lock = OptionalMode::Some(InputMode::English);
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::CompositionProtection, false,
                 "composition overrides cache and lock");

  state.composition_active = false;
  state.candidate_visible = true;
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::CompositionProtection, false,
                 "candidate overrides cache and lock");

  state.candidate_visible = false;
  ExpectDecision(cache.Read(state), DesiredMode::English, InputMode::English,
                 DecisionSource::ExplicitUserLock, true,
                 "explicit lock overrides cache");

  state.automation_enabled = false;
  ExpectDecision(cache.Read(state), DesiredMode::English, InputMode::English,
                 DecisionSource::ExplicitUserLock, true,
                 "lock remains active with automation disabled");

  state.explicit_user_lock = OptionalMode::None();
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::AutomationDisabled, false,
                 "automation disabled overrides cache");

  state.automation_enabled = true;
  state.manual_override_until_ms = 101;
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::ManualOverrideProtection, false,
                 "manual protection overrides cache");

  state.now_ms = 101;
  ExpectDecision(cache.Read(state), DesiredMode::English, InputMode::English,
                 DecisionSource::ApplicationDefault, true,
                 "manual expiry boundary allows cache");
}

void TestSafetyEvaluationDoesNotRequireCachedContext() {
  auto state = BaseState();
  state.explicit_user_lock = OptionalMode::Some(InputMode::English);
  ExpectDecision(contextime::EvaluateDecisionCacheSafety(state),
                 DesiredMode::English, InputMode::English,
                 DecisionSource::ExplicitUserLock, true,
                 "explicit lock without cache");

  state.composition_active = true;
  ExpectDecision(contextime::EvaluateDecisionCacheSafety(state),
                 DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::CompositionProtection, false,
                 "composition safety without cache");
}

void TestOnlyStableAutomaticDecisionsAreAccepted() {
  DecisionCache cache;
  auto state = BaseState();
  Expect(cache.Publish(English(DecisionSource::UserRule), 1000),
         "user rule is cacheable");

  const Decision composition{DesiredMode::Keep, InputMode::Chinese,
                             DecisionSource::CompositionProtection, false};
  Expect(!cache.Publish(composition, 1000),
         "transient composition result is rejected");
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::ContextUnavailable, false,
                 "rejected publish invalidates previous decision");

  const Decision malformed{DesiredMode::English, InputMode::Chinese,
                           DecisionSource::SyntaxContext, true};
  Expect(!cache.Publish(malformed, 1000),
         "inconsistent target is rejected");

  const Decision fallback{DesiredMode::Keep, InputMode::English,
                          DecisionSource::FallbackKeep, false};
  Expect(cache.Publish(fallback, 1000), "stable fallback KEEP is cacheable");
  ExpectDecision(cache.Read(state), DesiredMode::Keep, InputMode::Chinese,
                 DecisionSource::FallbackKeep, false,
                 "fallback KEEP rebases current target");

  const Decision unavailable{DesiredMode::Keep, InputMode::Chinese,
                             DecisionSource::ContextUnavailable, false};
  Expect(!cache.Publish(unavailable, 1000),
         "service-unavailable result must invalidate instead of cache");
}

void TestConcurrentReadsNeverObserveTornDecision() {
  DecisionCache cache;
  const Decision english = English(DecisionSource::UserRule);
  const Decision chinese = Chinese(DecisionSource::ProjectRule);
  std::atomic<bool> writer_done{false};
  std::atomic<int> invalid_reads{0};

  std::thread writer([&] {
    for (int index = 0; index < 100000; ++index) {
      const bool published =
          cache.Publish((index & 1) == 0 ? english : chinese, 1000);
      if (!published) {
        invalid_reads.fetch_add(1, std::memory_order_relaxed);
      }
    }
    writer_done.store(true, std::memory_order_release);
  });

  DecisionCacheReadState state = BaseState();
  int read_iterations = 0;
  do {
    const Decision decision = cache.Read(state);
    if (decision.source == DecisionSource::ContextUnavailable) {
      ++read_iterations;
      continue;
    }
    const bool valid_english =
        decision.source == DecisionSource::UserRule &&
        decision.desired_mode == DesiredMode::English &&
        decision.target_mode == InputMode::English && decision.should_switch;
    const bool valid_chinese =
        decision.source == DecisionSource::ProjectRule &&
        decision.desired_mode == DesiredMode::Chinese &&
        decision.target_mode == InputMode::Chinese && !decision.should_switch;
    if (!valid_english && !valid_chinese) {
      invalid_reads.fetch_add(1, std::memory_order_relaxed);
    }
    ++read_iterations;
  } while (!writer_done.load(std::memory_order_acquire) ||
           read_iterations < 1000);
  writer.join();

  const Decision final_decision = cache.Read(state);
  Expect(final_decision.source == DecisionSource::ProjectRule &&
             final_decision.desired_mode == DesiredMode::Chinese &&
             final_decision.target_mode == InputMode::Chinese &&
             !final_decision.should_switch,
         "final published decision is visible after writer completion");
  Expect(invalid_reads.load(std::memory_order_relaxed) == 0,
         "concurrent reader observed no torn decision");
}

}  // namespace

int main() {
  static_assert(noexcept(std::declval<DecisionCache&>().Publish(
                    std::declval<const Decision&>(), 0)),
                "cache publish must be noexcept");
  static_assert(noexcept(std::declval<const DecisionCache&>().Read(
                    std::declval<const DecisionCacheReadState&>())),
                "cache read must be noexcept");

  TestPublishReadExpiryAndInvalidation();
  TestCurrentSafetyStateOverridesCache();
  TestSafetyEvaluationDoesNotRequireCachedContext();
  TestOnlyStableAutomaticDecisionsAreAccepted();
  TestConcurrentReadsNeverObserveTornDecision();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Decision Cache: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
