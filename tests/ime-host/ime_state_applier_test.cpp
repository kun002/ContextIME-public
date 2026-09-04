#include "contextime/ime_state_applier.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

using contextime::Decision;
using contextime::DecisionSource;
using contextime::DesiredMode;
using contextime::ImeModeApplyStatus;
using contextime::ImeModeSink;
using contextime::ImeStateApplier;
using contextime::InputMode;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class RecordingSink final : public ImeModeSink {
 public:
  bool ApplyMode(InputMode target_mode) noexcept override {
    ++calls;
    last_mode = target_mode;
    return succeeds;
  }

  bool succeeds = true;
  int calls = 0;
  InputMode last_mode = InputMode::Chinese;
};

void TestKeepAndProtectionNeverReachSink() {
  RecordingSink sink;
  const Decision protected_decision{
      DesiredMode::Keep, InputMode::Chinese,
      DecisionSource::CompositionProtection, false};
  const auto result = ImeStateApplier::ApplyDecision(
      protected_decision, InputMode::Chinese, sink);
  Expect(result.status == ImeModeApplyStatus::Kept,
         "composition protection remains KEEP");
  Expect(sink.calls == 0, "composition protection never calls mode sink");
}

void TestMatchingModeNeverReapplies() {
  RecordingSink sink;
  const Decision english{DesiredMode::English, InputMode::English,
                         DecisionSource::SurfaceContext, false};
  const auto result =
      ImeStateApplier::ApplyDecision(english, InputMode::English, sink);
  Expect(result.status == ImeModeApplyStatus::Kept,
         "matching current mode is kept");
  Expect(sink.calls == 0, "matching current mode does not call sink");
}

void TestValidAutomaticSwitchUsesSinkOnce() {
  RecordingSink sink;
  const Decision english{DesiredMode::English, InputMode::English,
                         DecisionSource::ApplicationDefault, true};
  const auto result =
      ImeStateApplier::ApplyDecision(english, InputMode::Chinese, sink);
  Expect(result.status == ImeModeApplyStatus::Applied,
         "valid automatic decision is applied");
  Expect(sink.calls == 1, "valid automatic decision calls sink once");
  Expect(sink.last_mode == InputMode::English,
         "valid automatic decision applies target mode");
}

void TestSinkFailureIsReportedWithoutRetry() {
  RecordingSink sink;
  sink.succeeds = false;
  const Decision chinese{DesiredMode::Chinese, InputMode::Chinese,
                         DecisionSource::UserRule, true};
  const auto result =
      ImeStateApplier::ApplyDecision(chinese, InputMode::English, sink);
  Expect(result.status == ImeModeApplyStatus::ApplyFailed,
         "mode sink failure is reported");
  Expect(sink.calls == 1, "failed mode apply is attempted once");
}

void TestMalformedDecisionIsRejected() {
  RecordingSink sink;
  const Decision malformed{DesiredMode::English, InputMode::Chinese,
                           DecisionSource::SyntaxContext, true};
  const auto result =
      ImeStateApplier::ApplyDecision(malformed, InputMode::English, sink);
  Expect(result.status == ImeModeApplyStatus::Kept,
         "malformed decision is kept");
  Expect(sink.calls == 0, "malformed decision never reaches sink");
}

}  // namespace

int main() {
  static_assert(noexcept(ImeStateApplier::ApplyDecision(
                    std::declval<const Decision&>(), InputMode::Chinese,
                    std::declval<ImeModeSink&>())),
                "state applier boundary must be noexcept");

  TestKeepAndProtectionNeverReachSink();
  TestMatchingModeNeverReapplies();
  TestValidAutomaticSwitchUsesSinkOnce();
  TestSinkFailureIsReportedWithoutRetry();
  TestMalformedDecisionIsRejected();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "IME State Applier: " << assertions << " assertions passed\n";
  return EXIT_SUCCESS;
}
