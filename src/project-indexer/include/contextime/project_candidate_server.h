#pragma once

#include <cstdint>

namespace contextime {

class ActiveProjectSnapshotCache;

inline constexpr wchar_t kProjectCandidatePipeName[] =
    L"\\\\.\\pipe\\ContextIME.ProjectCandidate.v1";

enum class ProjectCandidateServeStatus : std::uint8_t {
  Served = 0,
  ClientTimeout,
  ClientDisconnected,
  ProtocolError,
  SecurityError,
  SystemError,
};

// Serves one in-memory immutable snapshot. This runs on its own background
// thread and never loads the project dictionary or participates in key input.
ProjectCandidateServeStatus ServeOneProjectCandidateConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    const ActiveProjectSnapshotCache* snapshot_cache) noexcept;

const char* ToString(ProjectCandidateServeStatus status) noexcept;

}  // namespace contextime
