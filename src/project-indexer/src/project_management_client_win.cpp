#include "contextime/project_management_client.h"

#if !defined(_WIN32)
#error project_management_client_win.cpp is Windows-only
#endif

#include "win_pipe_io.h"

#include <utility>

namespace contextime {
namespace {

ProjectManagementCallStatus FromIoFailure(
    win_pipe_detail::IoResult failure) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return ProjectManagementCallStatus::Timeout;
    case win_pipe_detail::IoStatus::Disconnected:
      return ProjectManagementCallStatus::Disconnected;
    case win_pipe_detail::IoStatus::SystemError:
      return ProjectManagementCallStatus::SystemError;
    case win_pipe_detail::IoStatus::Ok:
      return ProjectManagementCallStatus::Ok;
  }
  return ProjectManagementCallStatus::SystemError;
}

HANDLE OpenPipeUntilDeadline(const wchar_t* pipe_name,
                             win_pipe_detail::Deadline& deadline,
                             DWORD& open_error) noexcept {
  open_error = ERROR_FILE_NOT_FOUND;
  while (deadline.RemainingMs() > 0) {
    if (!WaitNamedPipeW(pipe_name, deadline.RemainingMs())) {
      open_error = GetLastError();
      if (open_error == ERROR_SEM_TIMEOUT) {
        break;
      }
      if (open_error != ERROR_FILE_NOT_FOUND &&
          open_error != ERROR_PIPE_BUSY) {
        break;
      }
      Sleep(1);
      continue;
    }

    HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) {
      return pipe;
    }
    open_error = GetLastError();
    if (open_error != ERROR_FILE_NOT_FOUND &&
        open_error != ERROR_PIPE_BUSY) {
      break;
    }
    Sleep(1);
  }
  SetLastError(open_error);
  return INVALID_HANDLE_VALUE;
}

}  // namespace

ProjectManagementCallStatus CallProjectManagement(
    const wchar_t* pipe_name, std::uint32_t timeout_ms,
    const management_protocol::ManagementRequest& request,
    management_protocol::ManagementResponse& response) noexcept {
  response = {};
  if (pipe_name == nullptr || pipe_name[0] == L'\0' || timeout_ms == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return ProjectManagementCallStatus::SystemError;
  }

  management_protocol::RequestFrame request_frame;
  if (management_protocol::EncodeRequest(request, request_frame) !=
      management_protocol::CodecStatus::Ok) {
    return ProjectManagementCallStatus::ProtocolError;
  }
  win_pipe_detail::Deadline deadline(timeout_ms);
  DWORD open_error = ERROR_SUCCESS;
  HANDLE pipe = OpenPipeUntilDeadline(pipe_name, deadline, open_error);
  if (pipe == INVALID_HANDLE_VALUE) {
    if (open_error == ERROR_FILE_NOT_FOUND) {
      return ProjectManagementCallStatus::Unavailable;
    }
    if (open_error == ERROR_SEM_TIMEOUT || open_error == ERROR_PIPE_BUSY) {
      return ProjectManagementCallStatus::Timeout;
    }
    return ProjectManagementCallStatus::SystemError;
  }

  const auto write_result = win_pipe_detail::TransferExact(
      pipe, request_frame.data(), request_frame.size(), true, deadline);
  if (write_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(write_result);
  }

  management_protocol::ResponseFrame response_frame;
  const auto read_result = win_pipe_detail::TransferExact(
      pipe, response_frame.data(), response_frame.size(), false, deadline);
  if (read_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(read_result);
  }
  management_protocol::ManagementResponse decoded;
  if (management_protocol::DecodeResponse(response_frame, decoded) !=
          management_protocol::CodecStatus::Ok ||
      decoded.request_id != request.request_id ||
      decoded.operation != request.operation) {
    CloseHandle(pipe);
    return ProjectManagementCallStatus::ProtocolError;
  }

  std::uint8_t acknowledgement =
      management_protocol::kResponseAcknowledgement;
  const auto acknowledgement_result = win_pipe_detail::TransferExact(
      pipe, &acknowledgement, sizeof(acknowledgement), true, deadline);
  CloseHandle(pipe);
  if (acknowledgement_result.status != win_pipe_detail::IoStatus::Ok) {
    return FromIoFailure(acknowledgement_result);
  }
  response = std::move(decoded);
  return ProjectManagementCallStatus::Ok;
}

const char* ToString(ProjectManagementCallStatus status) noexcept {
  switch (status) {
    case ProjectManagementCallStatus::Ok: return "OK";
    case ProjectManagementCallStatus::Unavailable: return "UNAVAILABLE";
    case ProjectManagementCallStatus::Timeout: return "TIMEOUT";
    case ProjectManagementCallStatus::Disconnected: return "DISCONNECTED";
    case ProjectManagementCallStatus::ProtocolError: return "PROTOCOL_ERROR";
    case ProjectManagementCallStatus::SystemError: return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
