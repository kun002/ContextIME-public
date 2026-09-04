#pragma once

#include <cstdint>

#include "contextime/project_management_protocol.h"

namespace contextime {

enum class ProjectManagementCallStatus : std::uint8_t {
  Ok = 0,
  Unavailable,
  Timeout,
  Disconnected,
  ProtocolError,
  SystemError,
};

ProjectManagementCallStatus CallProjectManagement(
    const wchar_t* pipe_name, std::uint32_t timeout_ms,
    const management_protocol::ManagementRequest& request,
    management_protocol::ManagementResponse& response) noexcept;

const char* ToString(ProjectManagementCallStatus status) noexcept;

}  // namespace contextime
