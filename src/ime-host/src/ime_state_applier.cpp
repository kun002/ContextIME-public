#include "contextime/ime_state_applier.h"

namespace contextime {

ImeModeApplyResult ImeStateApplier::ApplyDecision(
    const Decision& decision, InputMode current_mode,
    ImeModeSink& sink) noexcept {
  if (!decision.should_switch || decision.desired_mode == DesiredMode::Keep ||
      decision.target_mode == current_mode) {
    return {decision, ImeModeApplyStatus::Kept};
  }

  const bool valid_chinese =
      decision.desired_mode == DesiredMode::Chinese &&
      decision.target_mode == InputMode::Chinese;
  const bool valid_english =
      decision.desired_mode == DesiredMode::English &&
      decision.target_mode == InputMode::English;
  if (!valid_chinese && !valid_english) {
    return {decision, ImeModeApplyStatus::Kept};
  }

  return {decision, sink.ApplyMode(decision.target_mode)
                        ? ImeModeApplyStatus::Applied
                        : ImeModeApplyStatus::ApplyFailed};
}

}  // namespace contextime
