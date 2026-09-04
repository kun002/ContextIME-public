#pragma once

#include <cstdint>

#include "contextime/context_engine.h"

namespace contextime {

enum class ImeModeApplyStatus : std::uint8_t {
  Kept = 0,
  Applied,
  ApplyFailed,
};

struct ImeModeApplyResult {
  Decision decision;
  ImeModeApplyStatus status = ImeModeApplyStatus::Kept;
};

// Implemented by the native IME frontend on its TSF owner thread. The
// background Context Service worker must never call this interface.
class ImeModeSink {
 public:
  virtual ~ImeModeSink() = default;
  virtual bool ApplyMode(InputMode target_mode) noexcept = 0;
};

class ImeStateApplier final {
 public:
  // The caller must obtain decision from DecisionCache::Read (or the guarded
  // ContextRefreshWorker::Read wrapper) using a fresh IME safety snapshot.
  // This method performs no IPC, Win32 call, allocation, clock read or lock.
  static ImeModeApplyResult ApplyDecision(const Decision& decision,
                                          InputMode current_mode,
                                          ImeModeSink& sink) noexcept;
};

}  // namespace contextime
