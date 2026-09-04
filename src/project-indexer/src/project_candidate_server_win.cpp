#include "contextime/project_candidate_server.h"

#if !defined(_WIN32)
#error project_candidate_server_win.cpp is Windows-only
#endif

#include "contextime/active_project_snapshot.h"
#include "contextime/project_candidate_protocol.h"

#include "win_pipe_io.h"

#include <sddl.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace contextime {
namespace {

class PipeSecurity final {
 public:
  PipeSecurity() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      error_ = GetLastError();
      return;
    }
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0) {
      error_ = GetLastError();
      CloseHandle(token);
      return;
    }
    std::vector<std::uint8_t> token_user(required);
    if (!GetTokenInformation(token, TokenUser, token_user.data(), required,
                             &required)) {
      error_ = GetLastError();
      CloseHandle(token);
      return;
    }
    CloseHandle(token);

    const auto* user = reinterpret_cast<const TOKEN_USER*>(token_user.data());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) {
      error_ = GetLastError();
      return;
    }
    const std::wstring sddl =
        std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr)) {
      error_ = GetLastError();
      descriptor_ = nullptr;
      return;
    }
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_;
    attributes_.bInheritHandle = FALSE;
  }

  ~PipeSecurity() {
    if (descriptor_ != nullptr) LocalFree(descriptor_);
  }

  bool valid() const noexcept { return descriptor_ != nullptr; }
  SECURITY_ATTRIBUTES* attributes() noexcept { return &attributes_; }
  DWORD error() const noexcept { return error_; }

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_{};
  DWORD error_ = ERROR_SUCCESS;
};

bool ConnectClient(HANDLE pipe) noexcept {
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) return false;
  OVERLAPPED operation{};
  operation.hEvent = event;
  BOOL connected = ConnectNamedPipe(pipe, &operation);
  if (!connected) {
    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED) {
      connected = TRUE;
    } else if (error == ERROR_IO_PENDING &&
               WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0) {
      DWORD ignored = 0;
      connected = GetOverlappedResult(pipe, &operation, &ignored, FALSE);
    }
  }
  CloseHandle(event);
  return connected != FALSE;
}

ProjectCandidateServeStatus FromIoFailure(
    win_pipe_detail::IoResult failure) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return ProjectCandidateServeStatus::ClientTimeout;
    case win_pipe_detail::IoStatus::Disconnected:
      return ProjectCandidateServeStatus::ClientDisconnected;
    case win_pipe_detail::IoStatus::SystemError:
      return ProjectCandidateServeStatus::SystemError;
    case win_pipe_detail::IoStatus::Ok:
      return ProjectCandidateServeStatus::Served;
  }
  return ProjectCandidateServeStatus::SystemError;
}

candidate_protocol::ProjectCandidateResponse MakeResponse(
    std::uint32_t request_id,
    const ActiveProjectSnapshotCache& cache) {
  candidate_protocol::ProjectCandidateResponse response;
  response.request_id = request_id;
  const auto snapshot = cache.Read();
  if (!snapshot) return response;
  response.generation = snapshot->generation;
  const std::uint64_t now_ms = GetTickCount64();
  if (!IsActiveProjectSnapshotLive(*snapshot, now_ms)) return response;

  response.active = true;
  response.project_id = snapshot->project_id;
  response.candidates = snapshot->candidates;
  const std::uint64_t remaining = snapshot->expires_at_ms - now_ms;
  response.ttl_ms = static_cast<std::uint32_t>((std::min)(
      remaining,
      static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
  if (response.ttl_ms == 0) response.ttl_ms = 1;
  return response;
}

}  // namespace

ProjectCandidateServeStatus ServeOneProjectCandidateConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    const ActiveProjectSnapshotCache* snapshot_cache) noexcept {
  if (pipe_name == nullptr || pipe_name[0] == L'\0' ||
      snapshot_cache == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return ProjectCandidateServeStatus::SystemError;
  }

  try {
    PipeSecurity security;
    if (!security.valid()) {
      SetLastError(security.error());
      return ProjectCandidateServeStatus::SecurityError;
    }
    HANDLE pipe = CreateNamedPipeW(
        pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        static_cast<DWORD>(candidate_protocol::kResponseFrameSize),
        static_cast<DWORD>(candidate_protocol::kQueryFrameSize), 0,
        security.attributes());
    if (pipe == INVALID_HANDLE_VALUE) {
      return ProjectCandidateServeStatus::SystemError;
    }
    if (!ConnectClient(pipe)) {
      CloseHandle(pipe);
      return ProjectCandidateServeStatus::SystemError;
    }

    win_pipe_detail::Deadline deadline(client_io_timeout_ms);
    candidate_protocol::QueryFrame query_frame;
    const auto read_result = win_pipe_detail::TransferExact(
        pipe, query_frame.data(), query_frame.size(), false, deadline);
    if (read_result.status != win_pipe_detail::IoStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return FromIoFailure(read_result);
    }

    candidate_protocol::ProjectCandidateQuery query;
    if (candidate_protocol::DecodeQuery(query_frame, query) !=
        candidate_protocol::CodecStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return ProjectCandidateServeStatus::ProtocolError;
    }
    const auto response = MakeResponse(query.request_id, *snapshot_cache);
    candidate_protocol::ResponseFrame response_frame;
    if (candidate_protocol::EncodeResponse(response, response_frame) !=
        candidate_protocol::CodecStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return ProjectCandidateServeStatus::SystemError;
    }
    const auto write_result = win_pipe_detail::TransferExact(
        pipe, response_frame.data(), response_frame.size(), true, deadline);
    if (write_result.status != win_pipe_detail::IoStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return FromIoFailure(write_result);
    }

    std::uint8_t acknowledgement = 0;
    const auto acknowledgement_result = win_pipe_detail::TransferExact(
        pipe, &acknowledgement, sizeof(acknowledgement), false, deadline);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    if (acknowledgement_result.status != win_pipe_detail::IoStatus::Ok) {
      return FromIoFailure(acknowledgement_result);
    }
    return acknowledgement == candidate_protocol::kResponseAcknowledgement
               ? ProjectCandidateServeStatus::Served
               : ProjectCandidateServeStatus::ProtocolError;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return ProjectCandidateServeStatus::SystemError;
  }
}

const char* ToString(ProjectCandidateServeStatus status) noexcept {
  switch (status) {
    case ProjectCandidateServeStatus::Served: return "SERVED";
    case ProjectCandidateServeStatus::ClientTimeout: return "CLIENT_TIMEOUT";
    case ProjectCandidateServeStatus::ClientDisconnected:
      return "CLIENT_DISCONNECTED";
    case ProjectCandidateServeStatus::ProtocolError: return "PROTOCOL_ERROR";
    case ProjectCandidateServeStatus::SecurityError: return "SECURITY_ERROR";
    case ProjectCandidateServeStatus::SystemError: return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
