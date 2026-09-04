#pragma once

#include <cstdint>

#include "contextime/context_engine.h"

namespace contextime {

class EditorContextStore;

// This pipe is separate from ContextIMENamedPipe, which belongs to the
// existing Weasel/librime input path. The version is part of the endpoint so
// incompatible protocol generations cannot silently share a connection.
inline constexpr wchar_t kContextServicePipeName[] =
    L"\\\\.\\pipe\\ContextIME.ContextService.v1";
inline constexpr wchar_t kContextServiceMutexName[] =
    L"Local\\ContextIME.ContextService.Singleton.v1";
inline constexpr wchar_t kContextServiceStopEventName[] =
    L"Local\\ContextIME.ContextService.Stop.v1";

// Synchronous transport is for a background refresh worker only. The TSF key
// path must consume an already-cached result and must never call this API.
inline constexpr std::uint32_t kDefaultContextServiceTimeoutMs = 25;

enum class ContextServiceTransportStatus : std::uint8_t {
  Ok = 0,
  ServiceUnavailable,
  Timeout,
  Disconnected,
  ProtocolError,
  SystemError,
};

struct ContextServiceResult {
  Decision decision;
  ContextServiceTransportStatus transport_status =
      ContextServiceTransportStatus::ServiceUnavailable;
  std::uint32_t request_id = 0;
  std::uint32_t system_error = 0;
};

ContextServiceResult EvaluateViaContextService(
    const wchar_t* pipe_name, const ContextSnapshot& snapshot,
    std::uint32_t timeout_ms, std::uint32_t request_id) noexcept;

enum class ContextServiceServeStatus : std::uint8_t {
  Served = 0,
  ClientTimeout,
  ClientDisconnected,
  ProtocolError,
  SecurityError,
  SystemError,
};

// Serves exactly one connection and then returns. The executable loops around
// this primitive, which also keeps integration tests deterministic.
ContextServiceServeStatus ServeOneContextServiceConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    EditorContextStore* editor_context_store = nullptr) noexcept;

const char* ToString(ContextServiceTransportStatus status) noexcept;
const char* ToString(ContextServiceServeStatus status) noexcept;

}  // namespace contextime
