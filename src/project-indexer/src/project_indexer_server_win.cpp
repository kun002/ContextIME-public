#include "contextime/project_indexer_server.h"

#if !defined(_WIN32)
#error project_indexer_server_win.cpp is Windows-only
#endif

#include "contextime/active_project_snapshot.h"
#include "contextime/project_dictionary.h"
#include "contextime/project_indexer_protocol.h"
#include "contextime/project_management_protocol.h"

#include "win_pipe_io.h"

#include <sddl.h>

#include <algorithm>
#include <limits>
#include <optional>
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

  PipeSecurity(const PipeSecurity&) = delete;
  PipeSecurity& operator=(const PipeSecurity&) = delete;

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

ProjectIndexerServeStatus FromIoFailure(
    win_pipe_detail::IoResult failure) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return ProjectIndexerServeStatus::ClientTimeout;
    case win_pipe_detail::IoStatus::Disconnected:
      return ProjectIndexerServeStatus::ClientDisconnected;
    case win_pipe_detail::IoStatus::SystemError:
      return ProjectIndexerServeStatus::SystemError;
    case win_pipe_detail::IoStatus::Ok:
      return ProjectIndexerServeStatus::Served;
  }
  return ProjectIndexerServeStatus::SystemError;
}

std::uint64_t CurrentUnixTimeMs() noexcept {
  FILETIME file_time{};
  GetSystemTimeAsFileTime(&file_time);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  constexpr std::uint64_t kWindowsToUnixEpochTicks =
      116444736000000000ull;
  if (ticks.QuadPart < kWindowsToUnixEpochTicks) return 0;
  return (ticks.QuadPart - kWindowsToUnixEpochTicks) / 10000ull;
}

void SetStoreError(
    ProjectDictionaryStatus store_status,
    management_protocol::ManagementResponse& response) noexcept {
  response.status = management_protocol::ResponseStatus::StoreError;
  response.store_status = store_status;
  response.total_count = 0;
  response.next_cursor = management_protocol::kNoNextCursor;
  response.exists = false;
  response.enabled = false;
  response.projects.clear();
  response.entries.clear();
}

bool SetPage(std::uint32_t cursor, std::uint16_t page_size,
             std::size_t total, std::size_t& begin, std::size_t& end,
             std::uint32_t& next_cursor) noexcept {
  if (total > (std::numeric_limits<std::uint32_t>::max)() ||
      cursor > total) {
    return false;
  }
  begin = cursor;
  end = (std::min)(total, begin + static_cast<std::size_t>(page_size));
  next_cursor = end < total ? static_cast<std::uint32_t>(end)
                            : management_protocol::kNoNextCursor;
  return true;
}

void HandleManagementRequest(
    const management_protocol::ManagementRequest& request,
    ProjectDictionaryStore* store,
    ActiveProjectSnapshotCache* snapshot_cache,
    management_protocol::ManagementResponse& response) {
  response.request_id = request.request_id;
  response.operation = request.operation;
  response.status = management_protocol::ResponseStatus::Ok;
  response.store_status = ProjectDictionaryStatus::Ok;
  response.next_cursor = management_protocol::kNoNextCursor;
  const std::string project_id =
      project_protocol::ProjectIdToString(request.project_id);

  if (request.operation == management_protocol::Operation::ListProjects) {
    std::vector<std::string> project_ids;
    const auto status = store->ListProjects(project_ids);
    if (status != ProjectDictionaryStatus::Ok) {
      SetStoreError(status, response);
      return;
    }
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!SetPage(request.cursor, request.page_size, project_ids.size(), begin,
                 end, response.next_cursor)) {
      response.status = management_protocol::ResponseStatus::MalformedRequest;
      return;
    }
    response.total_count = static_cast<std::uint32_t>(project_ids.size());
    response.projects.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
      management_protocol::ProjectId wire_id{};
      if (!management_protocol::ProjectIdFromString(project_ids[index],
                                                     wire_id)) {
        response = {};
        response.request_id = request.request_id;
        response.operation = request.operation;
        response.status = management_protocol::ResponseStatus::InternalError;
        response.next_cursor = management_protocol::kNoNextCursor;
        return;
      }
      response.projects.push_back(wire_id);
    }
    return;
  }

  if (request.operation == management_protocol::Operation::ViewProject) {
    ProjectDictionarySnapshot dictionary;
    const auto status = store->Load(project_id, dictionary);
    if (status != ProjectDictionaryStatus::Ok) {
      SetStoreError(status, response);
      return;
    }
    if (!dictionary.exists) {
      response.status = management_protocol::ResponseStatus::NotFound;
      return;
    }
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!SetPage(request.cursor, request.page_size,
                 dictionary.entries.size(), begin, end,
                 response.next_cursor)) {
      response.status = management_protocol::ResponseStatus::MalformedRequest;
      return;
    }
    response.exists = true;
    response.enabled = dictionary.enabled;
    response.total_count =
        static_cast<std::uint32_t>(dictionary.entries.size());
    response.entries.assign(dictionary.entries.begin() + begin,
                            dictionary.entries.begin() + end);
    return;
  }

  if (request.operation == management_protocol::Operation::SetEnabled) {
    ProjectDictionarySnapshot dictionary;
    auto status = store->Load(project_id, dictionary);
    if (status != ProjectDictionaryStatus::Ok) {
      SetStoreError(status, response);
      return;
    }
    if (!dictionary.exists) {
      response.status = management_protocol::ResponseStatus::NotFound;
      return;
    }
    status = store->SetEnabled(project_id, request.enabled);
    if (status != ProjectDictionaryStatus::Ok) {
      SetStoreError(status, response);
      return;
    }
    status = store->Load(project_id, dictionary);
    if (status != ProjectDictionaryStatus::Ok) {
      SetStoreError(status, response);
      return;
    }
    response.exists = true;
    response.enabled = dictionary.enabled;
    response.total_count =
        static_cast<std::uint32_t>(dictionary.entries.size());
    const auto active = snapshot_cache->Read();
    const std::uint64_t now_ms = GetTickCount64();
    if (active && active->project_id == project_id &&
        IsActiveProjectSnapshotLive(*active, now_ms)) {
      snapshot_cache->Publish(dictionary, now_ms);
    }
    return;
  }

  const auto status = store->Remove(project_id);
  if (status != ProjectDictionaryStatus::Ok) {
    SetStoreError(status, response);
    return;
  }
  const auto active = snapshot_cache->Read();
  if (active && active->project_id == project_id) {
    snapshot_cache->Clear();
  }
}

}  // namespace

ProjectIndexerServeStatus ServeOneProjectIndexerConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    ProjectDictionaryStore* store,
    ActiveProjectSnapshotCache* snapshot_cache) noexcept {
  if (pipe_name == nullptr || pipe_name[0] == L'\0' || store == nullptr ||
      snapshot_cache == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return ProjectIndexerServeStatus::SystemError;
  }

  try {
    PipeSecurity security;
    if (!security.valid()) {
      SetLastError(security.error());
      return ProjectIndexerServeStatus::SecurityError;
    }
    HANDLE pipe = CreateNamedPipeW(
        pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES,
        static_cast<DWORD>(management_protocol::kResponseFrameSize),
        static_cast<DWORD>(project_protocol::kRequestFrameSize), 0,
        security.attributes());
    if (pipe == INVALID_HANDLE_VALUE) {
      return ProjectIndexerServeStatus::SystemError;
    }
    if (!ConnectClient(pipe)) {
      CloseHandle(pipe);
      return ProjectIndexerServeStatus::SystemError;
    }

    win_pipe_detail::Deadline request_deadline(client_io_timeout_ms);
    project_protocol::RequestFrame request_frame;
    const auto read_result = win_pipe_detail::TransferExact(
        pipe, request_frame.data(), request_frame.size(), false,
        request_deadline);
    if (read_result.status != win_pipe_detail::IoStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return FromIoFailure(read_result);
    }

    const bool is_management =
        management_protocol::HasManagementMagic(request_frame);
    bool protocol_ok = false;
    win_pipe_detail::IoResult write_result;
    std::optional<win_pipe_detail::Deadline> response_deadline;
    if (is_management) {
      management_protocol::ManagementRequest request;
      const auto codec_status =
          management_protocol::DecodeRequest(request_frame, request);
      management_protocol::ManagementResponse response;
      response.request_id = management_protocol::ReadRequestId(request_frame);
      response.next_cursor = management_protocol::kNoNextCursor;
      if (codec_status == management_protocol::CodecStatus::Ok) {
        HandleManagementRequest(request, store, snapshot_cache, response);
        protocol_ok = true;
      } else if (codec_status ==
                 management_protocol::CodecStatus::AllocationFailure) {
        response.status = management_protocol::ResponseStatus::InternalError;
      } else {
        response.status =
            management_protocol::ResponseStatus::MalformedRequest;
      }
      management_protocol::ResponseFrame response_frame;
      if (management_protocol::EncodeResponse(response, response_frame) !=
          management_protocol::CodecStatus::Ok) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return ProjectIndexerServeStatus::SystemError;
      }
      response_deadline.emplace(client_io_timeout_ms);
      write_result = win_pipe_detail::TransferExact(
          pipe, response_frame.data(), response_frame.size(), true,
          *response_deadline);
    } else {
      project_protocol::UpsertRequest request;
      const project_protocol::CodecStatus codec_status =
          project_protocol::DecodeUpsertRequest(request_frame, request);
      project_protocol::UpsertResponse response;
      response.request_id = project_protocol::ReadRequestId(request_frame);
      if (codec_status == project_protocol::CodecStatus::Ok) {
        protocol_ok = true;
        const std::string project_id =
            project_protocol::ProjectIdToString(request.project_id);
        ProjectDictionaryStatus store_status = ProjectDictionaryStatus::Ok;
        if (request.operation == project_protocol::RequestOperation::Upsert) {
          const std::uint64_t observed_at_ms = CurrentUnixTimeMs();
          for (auto& entry : request.entries) {
            entry.last_seen_ms = observed_at_ms;
          }
          std::shared_ptr<const ProjectDictionarySnapshot> dictionary;
          store_status =
              store->Upsert(project_id, request.entries, &dictionary);
          if (store_status == ProjectDictionaryStatus::Ok &&
              snapshot_cache->RefreshLease(project_id, GetTickCount64())) {
            snapshot_cache->Publish(*dictionary, GetTickCount64());
          }
        } else if (request.operation ==
                   project_protocol::RequestOperation::ActivateProject) {
          if (!snapshot_cache->RefreshLease(project_id, GetTickCount64())) {
            ProjectDictionarySnapshot dictionary;
            store_status = store->Load(project_id, dictionary);
            if (store_status == ProjectDictionaryStatus::Ok) {
              snapshot_cache->Publish(dictionary, GetTickCount64());
            } else {
              snapshot_cache->Clear();
            }
          }
        } else {
          snapshot_cache->Clear();
        }
        if (store_status == ProjectDictionaryStatus::Ok) {
          response.status = project_protocol::ResponseStatus::Ok;
          response.accepted = true;
          response.accepted_count =
              static_cast<std::uint16_t>(request.entries.size());
        } else {
          response.status = project_protocol::ResponseStatus::StoreError;
        }
      } else if (codec_status ==
                 project_protocol::CodecStatus::AllocationFailure) {
        response.status = project_protocol::ResponseStatus::InternalError;
      } else {
        response.status = project_protocol::ResponseStatus::MalformedRequest;
      }
      project_protocol::ResponseFrame response_frame;
      if (project_protocol::EncodeUpsertResponse(response, response_frame) !=
          project_protocol::CodecStatus::Ok) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return ProjectIndexerServeStatus::SystemError;
      }
      response_deadline.emplace(client_io_timeout_ms);
      write_result = win_pipe_detail::TransferExact(
          pipe, response_frame.data(), response_frame.size(), true,
          *response_deadline);
    }
    if (write_result.status != win_pipe_detail::IoStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return FromIoFailure(write_result);
    }

    std::uint8_t acknowledgement = 0;
    const auto acknowledgement_result = win_pipe_detail::TransferExact(
        pipe, &acknowledgement, sizeof(acknowledgement), false,
        *response_deadline);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    if (acknowledgement_result.status != win_pipe_detail::IoStatus::Ok) {
      return FromIoFailure(acknowledgement_result);
    }
    if (acknowledgement != project_protocol::kResponseAcknowledgement) {
      return ProjectIndexerServeStatus::ProtocolError;
    }
    return protocol_ok ? ProjectIndexerServeStatus::Served
                       : ProjectIndexerServeStatus::ProtocolError;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return ProjectIndexerServeStatus::SystemError;
  }
}

const char* ToString(ProjectIndexerServeStatus status) noexcept {
  switch (status) {
    case ProjectIndexerServeStatus::Served: return "SERVED";
    case ProjectIndexerServeStatus::ClientTimeout: return "CLIENT_TIMEOUT";
    case ProjectIndexerServeStatus::ClientDisconnected:
      return "CLIENT_DISCONNECTED";
    case ProjectIndexerServeStatus::ProtocolError: return "PROTOCOL_ERROR";
    case ProjectIndexerServeStatus::SecurityError: return "SECURITY_ERROR";
    case ProjectIndexerServeStatus::SystemError: return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
