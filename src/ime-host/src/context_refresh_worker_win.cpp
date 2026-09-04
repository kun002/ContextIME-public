#include "contextime/context_refresh_worker.h"

#if !defined(_WIN32)
#error context_refresh_worker_win.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace contextime {

ContextSnapshot PrepareContextRefreshSnapshot(
    const ContextSnapshot& live_snapshot, std::uint64_t now_ms) noexcept {
  ContextSnapshot request = live_snapshot;
  request.composition_active = false;
  request.candidate_visible = false;
  request.explicit_user_lock = OptionalMode::None();
  request.now_ms = now_ms;
  request.manual_override_until_ms = 0;
  request.automation_enabled = true;
  request.context_available = true;
  return request;
}

class ContextRefreshWorker::Impl final {
 public:
  Impl(ContextRefreshWorker* owner, const ContextRefreshWorkerOptions& options,
       ContextDecisionReadyCallback callback, void* callback_context)
      : owner_(owner),
        pipe_name_(options.pipe_name),
        service_timeout_ms_(options.service_timeout_ms),
        refresh_interval_ms_(options.refresh_interval_ms),
        decision_ttl_ms_(options.decision_ttl_ms),
        callback_(callback),
        callback_context_(callback_context) {}

  ~Impl() { Stop(); }

  bool Start() noexcept {
    try {
      thread_ = std::thread(&Impl::Run, this);
      return true;
    } catch (...) {
      return false;
    }
  }

  void Stop() noexcept {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      stopping_ = true;
      active_ = false;
    }
    condition_.notify_one();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void Activate(const ContextSnapshot& snapshot,
                std::uint64_t generation) noexcept {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      snapshot_ = snapshot;
      generation_ = generation;
      active_ = true;
    }
    condition_.notify_one();
  }

  void Deactivate(std::uint64_t generation) noexcept {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      generation_ = generation;
      active_ = false;
    }
    condition_.notify_one();
  }

 private:
  void Run() noexcept {
    std::uint32_t request_id = 0;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
      condition_.wait(lock, [this] { return stopping_ || active_; });
      if (stopping_) {
        break;
      }

      const ContextSnapshot live_snapshot = snapshot_;
      const std::uint64_t generation = generation_;
      lock.unlock();

      const std::uint64_t now_ms = GetTickCount64();
      const ContextSnapshot request =
          PrepareContextRefreshSnapshot(live_snapshot, now_ms);
      ++request_id;
      if (request_id == 0) {
        ++request_id;
      }
      const ContextServiceResult result = EvaluateViaContextService(
          pipe_name_.c_str(), request, service_timeout_ms_, request_id);

      lock.lock();
      const bool still_current =
          !stopping_ && active_ && generation_ == generation;
      lock.unlock();

      if (still_current) {
        bool published = false;
        if (result.transport_status == ContextServiceTransportStatus::Ok) {
          const std::uint64_t largest =
              std::numeric_limits<std::uint64_t>::max();
          const std::uint64_t expires_at =
              now_ms > largest - decision_ttl_ms_
                  ? largest
                  : now_ms + decision_ttl_ms_;
          published = owner_->cache_.Publish(result.decision, expires_at);
        } else {
          owner_->cache_.Invalidate();
        }
        owner_->published_generation_.store(generation,
                                             std::memory_order_release);
        if (published && callback_ != nullptr) {
          callback_(callback_context_);
        }
      }

      lock.lock();
      if (!stopping_ && active_ && generation_ == generation) {
        condition_.wait_for(
            lock, std::chrono::milliseconds(refresh_interval_ms_),
            [this, generation] {
              return stopping_ || !active_ || generation_ != generation;
            });
      }
    }
  }

  ContextRefreshWorker* owner_;
  std::wstring pipe_name_;
  std::uint32_t service_timeout_ms_;
  std::uint32_t refresh_interval_ms_;
  std::uint64_t decision_ttl_ms_;
  ContextDecisionReadyCallback callback_;
  void* callback_context_;

  std::mutex mutex_;
  std::condition_variable condition_;
  std::thread thread_;
  ContextSnapshot snapshot_;
  std::uint64_t generation_ = 0;
  bool active_ = false;
  bool stopping_ = false;
};

ContextRefreshWorker::ContextRefreshWorker() noexcept = default;

ContextRefreshWorker::~ContextRefreshWorker() { Stop(); }

bool ContextRefreshWorker::Start(
    const ContextRefreshWorkerOptions& options,
    ContextDecisionReadyCallback callback, void* callback_context) noexcept {
  if (impl_ != nullptr) {
    return true;
  }
  if (options.pipe_name == nullptr || options.pipe_name[0] == L'\0' ||
      options.service_timeout_ms == 0 || options.refresh_interval_ms == 0 ||
      options.decision_ttl_ms == 0) {
    return false;
  }

  try {
    auto candidate =
        std::make_unique<Impl>(this, options, callback, callback_context);
    if (!candidate->Start()) {
      return false;
    }
    impl_ = std::move(candidate);
    return true;
  } catch (...) {
    return false;
  }
}

void ContextRefreshWorker::Stop() noexcept {
  active_.store(false, std::memory_order_release);
  requested_generation_.fetch_add(1, std::memory_order_acq_rel);
  if (impl_ != nullptr) {
    impl_->Stop();
    impl_.reset();
  }
  cache_.Invalidate();
  published_generation_.store(0, std::memory_order_release);
}

void ContextRefreshWorker::Activate(const ContextSnapshot& snapshot) noexcept {
  if (impl_ == nullptr) {
    active_.store(false, std::memory_order_release);
    return;
  }
  const std::uint64_t generation =
      requested_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  active_.store(true, std::memory_order_release);
  impl_->Activate(snapshot, generation);
}

void ContextRefreshWorker::Deactivate() noexcept {
  active_.store(false, std::memory_order_release);
  const std::uint64_t generation =
      requested_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (impl_ != nullptr) {
    impl_->Deactivate(generation);
  }
}

Decision ContextRefreshWorker::Read(
    const DecisionCacheReadState& state) const noexcept {
  if (!active_.load(std::memory_order_acquire)) {
    return KeepUnavailable(state.current_mode);
  }
  const std::uint64_t requested =
      requested_generation_.load(std::memory_order_acquire);
  const std::uint64_t published =
      published_generation_.load(std::memory_order_acquire);
  if (requested == 0 || published != requested) {
    return EvaluateDecisionCacheSafety(state);
  }
  return cache_.Read(state);
}

bool ContextRefreshWorker::running() const noexcept { return impl_ != nullptr; }

Decision ContextRefreshWorker::KeepUnavailable(
    InputMode current_mode) noexcept {
  return {DesiredMode::Keep, current_mode,
          DecisionSource::ContextUnavailable, false};
}

}  // namespace contextime
