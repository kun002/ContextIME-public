#include "contextime/context_service.h"

#if !defined(_WIN32)
#error context_service_client_win.cpp is Windows-only
#endif

#include "contextime/context_protocol.h"

#include "win_pipe_io.h"

namespace contextime {
namespace {

Decision KeepUnavailable(InputMode current_mode) noexcept {
  return {DesiredMode::Keep, current_mode,
          DecisionSource::ContextUnavailable, false};
}

ContextServiceResult Fallback(const ContextSnapshot& snapshot,
                              ContextServiceTransportStatus status,
                              std::uint32_t request_id,
                              DWORD system_error) noexcept {
  return {KeepUnavailable(snapshot.current_mode), status, request_id,
          system_error};
}

ContextServiceResult FromIoFailure(
    const ContextSnapshot& snapshot, win_pipe_detail::IoResult failure,
    std::uint32_t request_id) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return Fallback(snapshot, ContextServiceTransportStatus::Timeout,
                      request_id, failure.system_error);
    case win_pipe_detail::IoStatus::Disconnected:
      return Fallback(snapshot, ContextServiceTransportStatus::Disconnected,
                      request_id, failure.system_error);
    case win_pipe_detail::IoStatus::SystemError:
      return Fallback(snapshot, ContextServiceTransportStatus::SystemError,
                      request_id, failure.system_error);
    case win_pipe_detail::IoStatus::Ok:
      break;
  }
  return Fallback(snapshot, ContextServiceTransportStatus::SystemError,
                  request_id, ERROR_INVALID_STATE);
}

bool IsDecisionValidForSnapshot(const Decision& decision,
                                const ContextSnapshot& snapshot) noexcept {
  if (decision.desired_mode == DesiredMode::Keep) {
    return !decision.should_switch &&
           decision.target_mode == snapshot.current_mode;
  }
  const InputMode expected_target =
      decision.desired_mode == DesiredMode::Chinese ? InputMode::Chinese
                                                    : InputMode::English;
  return decision.target_mode == expected_target &&
         decision.should_switch ==
             (decision.target_mode != snapshot.current_mode);
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

ContextServiceResult EvaluateViaContextService(
    const wchar_t* pipe_name, const ContextSnapshot& snapshot,
    std::uint32_t timeout_ms, std::uint32_t request_id) noexcept {
  if (pipe_name == nullptr || pipe_name[0] == L'\0') {
    return Fallback(snapshot, ContextServiceTransportStatus::SystemError,
                    request_id, ERROR_INVALID_PARAMETER);
  }

  protocol::EvaluateRequest request{request_id, snapshot};
  protocol::EvaluateRequestFrame request_frame;
  if (protocol::EncodeEvaluateRequest(request, request_frame) !=
      protocol::CodecStatus::Ok) {
    return Fallback(snapshot, ContextServiceTransportStatus::ProtocolError,
                    request_id, ERROR_INVALID_DATA);
  }

  win_pipe_detail::Deadline deadline(timeout_ms);
  DWORD open_error = ERROR_SUCCESS;
  HANDLE pipe = OpenPipeUntilDeadline(pipe_name, deadline, open_error);
  if (pipe == INVALID_HANDLE_VALUE) {
    if (open_error == ERROR_FILE_NOT_FOUND) {
      return Fallback(snapshot,
                      ContextServiceTransportStatus::ServiceUnavailable,
                      request_id, open_error);
    }
    if (open_error == ERROR_SEM_TIMEOUT || open_error == ERROR_PIPE_BUSY) {
      return Fallback(snapshot, ContextServiceTransportStatus::Timeout,
                      request_id, open_error);
    }
    return Fallback(snapshot, ContextServiceTransportStatus::SystemError,
                    request_id, open_error);
  }

  auto write_result = win_pipe_detail::TransferExact(
      pipe, request_frame.data(), request_frame.size(), true, deadline);
  if (write_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(snapshot, write_result, request_id);
  }

  protocol::EvaluateResponseFrame response_frame;
  auto read_result = win_pipe_detail::TransferExact(
      pipe, response_frame.data(), response_frame.size(), false, deadline);
  if (read_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(snapshot, read_result, request_id);
  }

  std::uint8_t acknowledgement = protocol::kResponseAcknowledgement;
  // Once the complete bounded response is local, acknowledgement failure does
  // not invalidate it. The ACK only prevents the conforming server from
  // disconnecting before the client has consumed the response bytes.
  (void)win_pipe_detail::TransferExact(pipe, &acknowledgement,
                                       sizeof(acknowledgement), true, deadline);
  CloseHandle(pipe);

  protocol::EvaluateResponse response;
  if (protocol::DecodeEvaluateResponse(response_frame, response) !=
          protocol::CodecStatus::Ok ||
      response.request_id != request_id ||
      response.status != protocol::ResponseStatus::Ok ||
      !IsDecisionValidForSnapshot(response.decision, snapshot)) {
    return Fallback(snapshot, ContextServiceTransportStatus::ProtocolError,
                    request_id, ERROR_INVALID_DATA);
  }

  return {response.decision, ContextServiceTransportStatus::Ok, request_id,
          ERROR_SUCCESS};
}

const char* ToString(ContextServiceTransportStatus status) noexcept {
  switch (status) {
    case ContextServiceTransportStatus::Ok:
      return "OK";
    case ContextServiceTransportStatus::ServiceUnavailable:
      return "SERVICE_UNAVAILABLE";
    case ContextServiceTransportStatus::Timeout:
      return "TIMEOUT";
    case ContextServiceTransportStatus::Disconnected:
      return "DISCONNECTED";
    case ContextServiceTransportStatus::ProtocolError:
      return "PROTOCOL_ERROR";
    case ContextServiceTransportStatus::SystemError:
      return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
