#pragma once

#if !defined(_WIN32)
#error context_refresh_worker.h is Windows-only
#endif

#include <atomic>
#include <cstdint>
#include <memory>

#include "contextime/context_service.h"
#include "contextime/decision_cache.h"

namespace contextime {

struct ContextRefreshWorkerOptions {
  const wchar_t* pipe_name = kContextServicePipeName;
  std::uint32_t service_timeout_ms = kDefaultContextServiceTimeoutMs;
  std::uint32_t refresh_interval_ms = 250;
  std::uint64_t decision_ttl_ms = 1000;
};

using ContextDecisionReadyCallback = void (*)(void* context) noexcept;

// Converts a live IME snapshot into the stable automatic-policy request sent
// by the background worker. Transient safety state is enforced again by
// DecisionCache::Read on the TSF owner thread and is never cached.
ContextSnapshot PrepareContextRefreshSnapshot(
    const ContextSnapshot& live_snapshot, std::uint64_t now_ms) noexcept;

// Owns the only DecisionCache writer. Context Service IPC runs exclusively on
// its worker thread. Read is bounded and lock-free and is the only operation
// intended for a future TSF callback or key path.
class ContextRefreshWorker final {
 public:
  ContextRefreshWorker() noexcept;
  ~ContextRefreshWorker();

  ContextRefreshWorker(const ContextRefreshWorker&) = delete;
  ContextRefreshWorker& operator=(const ContextRefreshWorker&) = delete;

  bool Start(const ContextRefreshWorkerOptions& options,
             ContextDecisionReadyCallback callback,
             void* callback_context) noexcept;
  void Stop() noexcept;

  // Activate/Deactivate are focus-lifecycle calls, not key-path calls. Requests
  // are coalesced and the worker refreshes only while this IME instance owns
  // foreground focus.
  void Activate(const ContextSnapshot& snapshot) noexcept;
  void Deactivate() noexcept;

  Decision Read(const DecisionCacheReadState& state) const noexcept;
  bool running() const noexcept;

 private:
  class Impl;

  static Decision KeepUnavailable(InputMode current_mode) noexcept;

  DecisionCache cache_;
  std::unique_ptr<Impl> impl_;
  std::atomic<std::uint64_t> requested_generation_{0};
  std::atomic<std::uint64_t> published_generation_{0};
  std::atomic<bool> active_{false};
};

}  // namespace contextime
