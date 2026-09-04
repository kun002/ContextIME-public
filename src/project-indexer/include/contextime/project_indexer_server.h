#pragma once

#include <cstdint>

namespace contextime {

class ProjectDictionaryStore;
class ActiveProjectSnapshotCache;

inline constexpr wchar_t kProjectIndexerPipeName[] =
    L"\\\\.\\pipe\\ContextIME.ProjectIndexer.v1";

enum class ProjectIndexerServeStatus : std::uint8_t {
  Served = 0,
  ClientTimeout,
  ClientDisconnected,
  ProtocolError,
  SecurityError,
  SystemError,
};

// This blocking primitive serves one Project Indexer connection. It must run
// on the dedicated project-index thread, never the Context Service decision
// loop or TSF key-event thread.
ProjectIndexerServeStatus ServeOneProjectIndexerConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    ProjectDictionaryStore* store,
    ActiveProjectSnapshotCache* snapshot_cache) noexcept;

const char* ToString(ProjectIndexerServeStatus status) noexcept;

}  // namespace contextime
