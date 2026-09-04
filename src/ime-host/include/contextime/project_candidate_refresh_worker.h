#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "contextime/project_candidate_protocol.h"

namespace contextime {

inline constexpr char kProjectCandidatePropertyName[] =
    "contextime_project_candidates";

struct ProjectCandidateRefreshOptions {
  const wchar_t* pipe_name =
      L"\\\\.\\pipe\\ContextIME.ProjectCandidate.v1";
  std::uint32_t poll_interval_ms = 500;
  std::uint32_t io_timeout_ms = 250;
};

struct ProjectCandidateRuntimeSnapshot {
  std::uint64_t revision = 0;
  std::uint64_t server_generation = 0;
  bool active = false;
  std::string project_id;
  std::string property_value;
};

// Background-only Pipe consumer. The input thread uses Read() and never
// performs transport, disk access, scanning, or Node work.
class ProjectCandidateRefreshWorker final {
 public:
  ProjectCandidateRefreshWorker();
  ~ProjectCandidateRefreshWorker();

  ProjectCandidateRefreshWorker(const ProjectCandidateRefreshWorker&) =
      delete;
  ProjectCandidateRefreshWorker& operator=(
      const ProjectCandidateRefreshWorker&) = delete;

  bool Start(const ProjectCandidateRefreshOptions& options = {}) noexcept;
  void Stop() noexcept;
  bool running() const noexcept;
  std::shared_ptr<const ProjectCandidateRuntimeSnapshot> Read() const noexcept;

 private:
  void Run() noexcept;
  void Publish(
      const candidate_protocol::ProjectCandidateResponse& response) noexcept;
  void PublishEmpty(std::uint64_t server_generation) noexcept;

  ProjectCandidateRefreshOptions options_{};
  void* stop_event_ = nullptr;
  std::thread thread_;
  std::uint64_t next_revision_ = 1;
  std::shared_ptr<const ProjectCandidateRuntimeSnapshot> snapshot_;
  std::uint64_t valid_until_ms_ = 0;
  std::uint32_t next_request_id_ = 1;
};

std::string EncodeProjectCandidateProperty(
    const candidate_protocol::ProjectCandidateResponse& response);

}  // namespace contextime
