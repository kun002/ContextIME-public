#pragma once

#include <cstdint>

#include "contextime/project_candidate_protocol.h"

namespace contextime {

enum class ProjectCandidateFetchStatus : std::uint8_t {
  Ok = 0,
  Unavailable,
  Timeout,
  Disconnected,
  ProtocolError,
  SystemError,
};

ProjectCandidateFetchStatus FetchProjectCandidateSnapshot(
    const wchar_t* pipe_name, std::uint32_t timeout_ms,
    std::uint32_t request_id,
    candidate_protocol::ProjectCandidateResponse& response) noexcept;

const char* ToString(ProjectCandidateFetchStatus status) noexcept;

}  // namespace contextime
