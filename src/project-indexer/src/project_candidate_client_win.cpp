#include "contextime/project_candidate_client.h"

#if !defined(_WIN32)
#error project_candidate_client_win.cpp is Windows-only
#endif

#include "win_pipe_io.h"

#include <utility>

namespace contextime {
namespace {

ProjectCandidateFetchStatus FromIoFailure(
    win_pipe_detail::IoResult failure) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return ProjectCandidateFetchStatus::Timeout;
    case win_pipe_detail::IoStatus::Disconnected:
      return ProjectCandidateFetchStatus::Disconnected;
    case win_pipe_detail::IoStatus::SystemError:
      return ProjectCandidateFetchStatus::SystemError;
    case win_pipe_detail::IoStatus::Ok:
      return ProjectCandidateFetchStatus::Ok;
  }
  return ProjectCandidateFetchStatus::SystemError;
}

}  // namespace

ProjectCandidateFetchStatus FetchProjectCandidateSnapshot(
    const wchar_t* pipe_name, std::uint32_t timeout_ms,
    std::uint32_t request_id,
    candidate_protocol::ProjectCandidateResponse& response) noexcept {
  response = {};
  if (pipe_name == nullptr || pipe_name[0] == L'\0' || timeout_ms == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return ProjectCandidateFetchStatus::SystemError;
  }

  HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                            nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PIPE_BUSY
               ? ProjectCandidateFetchStatus::Unavailable
               : ProjectCandidateFetchStatus::SystemError;
  }

  win_pipe_detail::Deadline deadline(timeout_ms);
  candidate_protocol::QueryFrame query_frame;
  candidate_protocol::ProjectCandidateQuery query;
  query.request_id = request_id;
  if (candidate_protocol::EncodeQuery(query, query_frame) !=
      candidate_protocol::CodecStatus::Ok) {
    CloseHandle(pipe);
    return ProjectCandidateFetchStatus::ProtocolError;
  }
  const auto write_result = win_pipe_detail::TransferExact(
      pipe, query_frame.data(), query_frame.size(), true, deadline);
  if (write_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(write_result);
  }

  candidate_protocol::ResponseFrame response_frame;
  const auto read_result = win_pipe_detail::TransferExact(
      pipe, response_frame.data(), response_frame.size(), false, deadline);
  if (read_result.status != win_pipe_detail::IoStatus::Ok) {
    CloseHandle(pipe);
    return FromIoFailure(read_result);
  }
  candidate_protocol::ProjectCandidateResponse decoded;
  if (candidate_protocol::DecodeResponse(response_frame, decoded) !=
          candidate_protocol::CodecStatus::Ok ||
      decoded.request_id != request_id) {
    CloseHandle(pipe);
    return ProjectCandidateFetchStatus::ProtocolError;
  }

  std::uint8_t acknowledgement = candidate_protocol::kResponseAcknowledgement;
  const auto acknowledgement_result = win_pipe_detail::TransferExact(
      pipe, &acknowledgement, sizeof(acknowledgement), true, deadline);
  CloseHandle(pipe);
  if (acknowledgement_result.status != win_pipe_detail::IoStatus::Ok) {
    return FromIoFailure(acknowledgement_result);
  }
  response = std::move(decoded);
  return ProjectCandidateFetchStatus::Ok;
}

const char* ToString(ProjectCandidateFetchStatus status) noexcept {
  switch (status) {
    case ProjectCandidateFetchStatus::Ok: return "OK";
    case ProjectCandidateFetchStatus::Unavailable: return "UNAVAILABLE";
    case ProjectCandidateFetchStatus::Timeout: return "TIMEOUT";
    case ProjectCandidateFetchStatus::Disconnected: return "DISCONNECTED";
    case ProjectCandidateFetchStatus::ProtocolError: return "PROTOCOL_ERROR";
    case ProjectCandidateFetchStatus::SystemError: return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
