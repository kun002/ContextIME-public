#include "contextime/project_candidate_refresh_worker.h"

#if !defined(_WIN32)
#error project_candidate_refresh_worker_win.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "contextime/project_candidate_client.h"

#include <atomic>
#include <limits>
#include <memory>
#include <utility>

namespace contextime {
namespace {

std::uint64_t Deadline(std::uint64_t now_ms,
                       std::uint32_t duration_ms) noexcept {
  const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  return now_ms > maximum - duration_ms ? maximum : now_ms + duration_ms;
}

std::shared_ptr<const ProjectCandidateRuntimeSnapshot> EmptyRuntimeSnapshot(
    std::uint64_t revision, std::uint64_t server_generation = 0) {
  auto snapshot = std::make_shared<ProjectCandidateRuntimeSnapshot>();
  snapshot->revision = revision;
  snapshot->server_generation = server_generation;
  return snapshot;
}

}  // namespace

ProjectCandidateRefreshWorker::ProjectCandidateRefreshWorker()
    : snapshot_(EmptyRuntimeSnapshot(next_revision_++)) {}

ProjectCandidateRefreshWorker::~ProjectCandidateRefreshWorker() {
  Stop();
}

bool ProjectCandidateRefreshWorker::Start(
    const ProjectCandidateRefreshOptions& options) noexcept {
  if (running() || options.pipe_name == nullptr ||
      options.pipe_name[0] == L'\0' || options.poll_interval_ms == 0 ||
      options.io_timeout_ms == 0) {
    return false;
  }
  HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event == nullptr) return false;
  options_ = options;
  stop_event_ = stop_event;
  valid_until_ms_ = 0;
  try {
    thread_ = std::thread(&ProjectCandidateRefreshWorker::Run, this);
    return true;
  } catch (...) {
    CloseHandle(stop_event);
    stop_event_ = nullptr;
    return false;
  }
}

void ProjectCandidateRefreshWorker::Stop() noexcept {
  HANDLE stop_event = static_cast<HANDLE>(stop_event_);
  if (stop_event != nullptr) SetEvent(stop_event);
  if (thread_.joinable()) thread_.join();
  if (stop_event != nullptr) CloseHandle(stop_event);
  stop_event_ = nullptr;
  valid_until_ms_ = 0;
  PublishEmpty(0);
}

bool ProjectCandidateRefreshWorker::running() const noexcept {
  return stop_event_ != nullptr;
}

std::shared_ptr<const ProjectCandidateRuntimeSnapshot>
ProjectCandidateRefreshWorker::Read() const noexcept {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

void ProjectCandidateRefreshWorker::Run() noexcept {
  HANDLE stop_event = static_cast<HANDLE>(stop_event_);
  while (stop_event != nullptr &&
         WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0) {
    candidate_protocol::ProjectCandidateResponse response;
    const std::uint32_t request_id = next_request_id_++;
    if (next_request_id_ == 0) next_request_id_ = 1;
    const auto status = FetchProjectCandidateSnapshot(
        options_.pipe_name, options_.io_timeout_ms, request_id, response);
    const std::uint64_t now_ms = GetTickCount64();
    if (status == ProjectCandidateFetchStatus::Ok) {
      if (response.active) {
        valid_until_ms_ = Deadline(now_ms, response.ttl_ms);
        Publish(response);
      } else {
        valid_until_ms_ = 0;
        PublishEmpty(response.generation);
      }
    } else if (valid_until_ms_ == 0 || now_ms > valid_until_ms_) {
      PublishEmpty(0);
    }

    if (WaitForSingleObject(stop_event, options_.poll_interval_ms) ==
        WAIT_OBJECT_0) {
      break;
    }
  }
}

void ProjectCandidateRefreshWorker::Publish(
    const candidate_protocol::ProjectCandidateResponse& response) noexcept {
  try {
    const auto current = Read();
    if (current && current->active &&
        current->server_generation == response.generation &&
        current->project_id == response.project_id) {
      return;
    }
    auto published = std::make_shared<ProjectCandidateRuntimeSnapshot>();
    published->revision = next_revision_++;
    published->server_generation = response.generation;
    published->active = true;
    published->project_id = response.project_id;
    published->property_value = EncodeProjectCandidateProperty(response);
    std::atomic_store_explicit(
        &snapshot_,
        std::shared_ptr<const ProjectCandidateRuntimeSnapshot>(published),
        std::memory_order_release);
  } catch (...) {
    PublishEmpty(0);
  }
}

void ProjectCandidateRefreshWorker::PublishEmpty(
    std::uint64_t server_generation) noexcept {
  try {
    const auto current = Read();
    if (current && !current->active &&
        current->server_generation == server_generation) {
      return;
    }
    std::atomic_store_explicit(
        &snapshot_, EmptyRuntimeSnapshot(next_revision_++, server_generation),
        std::memory_order_release);
  } catch (...) {
    // The last snapshot remains bounded and will be replaced on a later poll.
  }
}

std::string EncodeProjectCandidateProperty(
    const candidate_protocol::ProjectCandidateResponse& response) {
  if (!response.active || response.candidates.empty()) return {};
  std::string encoded = "1\n";
  encoded.reserve(kMaximumActiveProjectSymbolBytes +
                  response.candidates.size() * 16);
  for (const auto& candidate : response.candidates) {
    encoded += std::to_string(static_cast<unsigned>(candidate.symbol_type));
    encoded.push_back('\t');
    encoded += std::to_string(candidate.frequency);
    encoded.push_back('\t');
    encoded += candidate.symbol;
    encoded.push_back('\n');
  }
  return encoded;
}

}  // namespace contextime
