#include "contextime/context_engine.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using contextime::ContextEngine;
using contextime::ContextSnapshot;
using contextime::Decision;
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
         (std::string(fixture) + ": desired mode").c_str());
  Expect(decision.target_mode == target,
         (std::string(fixture) + ": target mode").c_str());
  Expect(decision.source == source,
         (std::string(fixture) + ": source").c_str());
  Expect(decision.should_switch == should_switch,
         (std::string(fixture) + ": should_switch").c_str());
}

ContextSnapshot AllRules() {
  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::Chinese;
  snapshot.explicit_user_lock = OptionalMode::Some(InputMode::English);
  snapshot.now_ms = 100;
  snapshot.manual_override_until_ms = 200;
  snapshot.user_rule = OptionalMode::Some(InputMode::English);
  snapshot.project_rule = OptionalMode::Some(InputMode::English);
  snapshot.syntax_context = OptionalMode::Some(InputMode::English);
  snapshot.surface_context = OptionalMode::Some(InputMode::English);
  snapshot.application_default = OptionalMode::Some(InputMode::English);
  return snapshot;
}

void TestCompositionProtection() {
  ContextSnapshot composition = AllRules();
  composition.composition_active = true;
  ExpectDecision(ContextEngine::Evaluate(composition), DesiredMode::Keep,
                 InputMode::Chinese, DecisionSource::CompositionProtection,
                 false, "composition overrides every other input");

  ContextSnapshot candidate = AllRules();
  candidate.candidate_visible = true;
  ExpectDecision(ContextEngine::Evaluate(candidate), DesiredMode::Keep,
                 InputMode::Chinese, DecisionSource::CompositionProtection,
                 false, "candidate overrides every other input");
}

void TestPriorityChain() {
  ContextSnapshot snapshot = AllRules();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::ExplicitUserLock, true,
                 "explicit lock overrides manual protection and rules");

  snapshot.explicit_user_lock = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::Keep,
                 InputMode::Chinese,
                 DecisionSource::ManualOverrideProtection, false,
                 "manual protection overrides automatic rules");

  snapshot.now_ms = snapshot.manual_override_until_ms;
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::UserRule, true,
                 "user rule overrides project and context rules");

  snapshot.user_rule = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::ProjectRule, true,
                 "project rule overrides syntax and lower rules");

  snapshot.project_rule = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::SyntaxContext, true,
                 "syntax overrides surface and application");

  snapshot.syntax_context = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::SurfaceContext, true,
                 "surface overrides application");

  snapshot.surface_context = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::English,
                 InputMode::English, DecisionSource::ApplicationDefault, true,
                 "application default is the final configured rule");

  snapshot.application_default = OptionalMode::None();
  ExpectDecision(ContextEngine::Evaluate(snapshot), DesiredMode::Keep,
                 InputMode::Chinese, DecisionSource::FallbackKeep, false,
                 "unknown context keeps current mode");
}

void TestFailureAndUserBoundaries() {
  ContextSnapshot unavailable;
  unavailable.current_mode = InputMode::Chinese;
  unavailable.context_available = false;
  unavailable.syntax_context = OptionalMode::Some(InputMode::English);
  ExpectDecision(ContextEngine::Evaluate(unavailable), DesiredMode::Keep,
                 InputMode::Chinese, DecisionSource::ContextUnavailable,
                 false, "unavailable context service falls back to keep");

  ContextSnapshot disabled = unavailable;
  disabled.context_available = true;
  disabled.automation_enabled = false;
  ExpectDecision(ContextEngine::Evaluate(disabled), DesiredMode::Keep,
                 InputMode::Chinese, DecisionSource::AutomationDisabled,
                 false, "disabled automation ignores automatic rules");

  disabled.explicit_user_lock = OptionalMode::Some(InputMode::English);
  ExpectDecision(ContextEngine::Evaluate(disabled), DesiredMode::English,
                 InputMode::English, DecisionSource::ExplicitUserLock, true,
                 "explicit lock remains available when automation is off");

  ContextSnapshot same_target;
  same_target.current_mode = InputMode::English;
  same_target.user_rule = OptionalMode::Some(InputMode::English);
  ExpectDecision(ContextEngine::Evaluate(same_target), DesiredMode::English,
                 InputMode::English, DecisionSource::UserRule, false,
                 "matching target does not request a duplicate switch");
}

void TestStableWireNames() {
  Expect(std::string(contextime::ToString(InputMode::Chinese)) == "CHINESE",
         "Chinese wire name");
  Expect(std::string(contextime::ToString(InputMode::English)) == "ENGLISH",
         "English wire name");
  Expect(std::string(contextime::ToString(DesiredMode::Keep)) == "KEEP",
         "Keep wire name");
  Expect(std::string(contextime::ToString(
             DecisionSource::CompositionProtection)) ==
             "COMPOSITION_PROTECTION",
         "composition source wire name");
  Expect(std::string(contextime::ToString(DecisionSource::FallbackKeep)) ==
             "FALLBACK_KEEP",
         "fallback source wire name");
}

}  // namespace

int main() {
  static_assert(noexcept(ContextEngine::Evaluate(ContextSnapshot{})),
                "Context Engine evaluation must remain noexcept");

  TestCompositionProtection();
  TestPriorityChain();
  TestFailureAndUserBoundaries();
  TestStableWireNames();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "Context Engine: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
