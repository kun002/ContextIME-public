#include "contextime/context_service.h"

#if !defined(_WIN32)
#error context_service_server_win.cpp is Windows-only
#endif

#include "contextime/application_context.h"
#include "contextime/context_protocol.h"
#include "contextime/editor_context.h"

#include "win_pipe_io.h"

#include <sddl.h>

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

    const auto* user =
        reinterpret_cast<const TOKEN_USER*>(token_user.data());
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
    if (descriptor_ != nullptr) {
      LocalFree(descriptor_);
    }
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

Decision KeepUnavailable(InputMode current_mode) noexcept {
  return {DesiredMode::Keep, current_mode,
          DecisionSource::ContextUnavailable, false};
}

ContextServiceServeStatus FromIoFailure(
    win_pipe_detail::IoResult failure) noexcept {
  switch (failure.status) {
    case win_pipe_detail::IoStatus::Timeout:
      return ContextServiceServeStatus::ClientTimeout;
    case win_pipe_detail::IoStatus::Disconnected:
      return ContextServiceServeStatus::ClientDisconnected;
    case win_pipe_detail::IoStatus::SystemError:
      return ContextServiceServeStatus::SystemError;
    case win_pipe_detail::IoStatus::Ok:
      return ContextServiceServeStatus::Served;
  }
  return ContextServiceServeStatus::SystemError;
}

bool ConnectClient(HANDLE pipe) noexcept {
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (event == nullptr) {
    return false;
  }
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

protocol::ResponseStatus ResponseStatusFor(
    protocol::CodecStatus codec_status) noexcept {
  return codec_status == protocol::CodecStatus::UnsupportedVersion
             ? protocol::ResponseStatus::UnsupportedVersion
             : protocol::ResponseStatus::MalformedRequest;
}

}  // namespace

ContextServiceServeStatus ServeOneContextServiceConnection(
    const wchar_t* pipe_name, std::uint32_t client_io_timeout_ms,
    EditorContextStore* editor_context_store) noexcept {
  if (pipe_name == nullptr || pipe_name[0] == L'\0') {
    SetLastError(ERROR_INVALID_PARAMETER);
    return ContextServiceServeStatus::SystemError;
  }

  try {
    PipeSecurity security;
    if (!security.valid()) {
      SetLastError(security.error());
      return ContextServiceServeStatus::SecurityError;
    }

    HANDLE pipe = CreateNamedPipeW(
        pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, security.attributes());
    if (pipe == INVALID_HANDLE_VALUE) {
      return ContextServiceServeStatus::SystemError;
    }

    if (!ConnectClient(pipe)) {
      CloseHandle(pipe);
      return ContextServiceServeStatus::SystemError;
    }

    win_pipe_detail::Deadline deadline(client_io_timeout_ms);
    protocol::RequestFrame request_frame;
    const auto read_result = win_pipe_detail::TransferExact(
        pipe, request_frame.data(), request_frame.size(), false, deadline);
    if (read_result.status != win_pipe_detail::IoStatus::Ok) {
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
      return FromIoFailure(read_result);
    }

    protocol::CodecStatus codec_status = protocol::CodecStatus::Ok;
    protocol::ResponseFrame response_frame;
    const std::uint16_t message_type =
        protocol::ReadMessageType(request_frame);
    if (message_type == static_cast<std::uint16_t>(
                            protocol::MessageType::EditorContextRequest)) {
      protocol::EditorContextRequest request;
      codec_status =
          protocol::DecodeEditorContextRequest(request_frame, request);
      protocol::EditorContextResponse response;
      response.request_id = protocol::ReadRequestId(request_frame);
      if (codec_status == protocol::CodecStatus::Ok &&
          editor_context_store != nullptr) {
        editor_context_store->Publish(request.update, GetTickCount64());
        response.status = protocol::ResponseStatus::Ok;
        response.accepted = true;
      } else if (codec_status == protocol::CodecStatus::Ok) {
        response.status = protocol::ResponseStatus::InternalError;
      } else {
        response.status = ResponseStatusFor(codec_status);
      }
      if (protocol::EncodeEditorContextResponse(response, response_frame) !=
          protocol::CodecStatus::Ok) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return ContextServiceServeStatus::SystemError;
      }
    } else {
      protocol::EvaluateRequest request;
      codec_status = protocol::DecodeEvaluateRequest(request_frame, request);
      protocol::EvaluateResponse response;
      response.request_id = protocol::ReadRequestId(request_frame);
      if (codec_status == protocol::CodecStatus::Ok) {
        response.status = protocol::ResponseStatus::Ok;
        ContextSnapshot snapshot = request.snapshot;
        if (snapshot.context_available) {
          const ApplicationContext application =
              CaptureForegroundApplication();
          ApplyApplicationContext(application, snapshot);
          if (editor_context_store != nullptr) {
            editor_context_store->Apply(application, GetTickCount64(),
                                        snapshot);
          }
        }
        response.decision = ContextEngine::Evaluate(snapshot);
      } else {
        response.status = ResponseStatusFor(codec_status);
        response.decision = KeepUnavailable(InputMode::Chinese);
      }
      if (protocol::EncodeEvaluateResponse(response, response_frame) !=
          protocol::CodecStatus::Ok) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return ContextServiceServeStatus::SystemError;
      }
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
    if (acknowledgement != protocol::kResponseAcknowledgement) {
      return ContextServiceServeStatus::ProtocolError;
    }
    return codec_status == protocol::CodecStatus::Ok
               ? ContextServiceServeStatus::Served
               : ContextServiceServeStatus::ProtocolError;
  } catch (...) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return ContextServiceServeStatus::SystemError;
  }
}

const char* ToString(ContextServiceServeStatus status) noexcept {
  switch (status) {
    case ContextServiceServeStatus::Served:
      return "SERVED";
    case ContextServiceServeStatus::ClientTimeout:
      return "CLIENT_TIMEOUT";
    case ContextServiceServeStatus::ClientDisconnected:
      return "CLIENT_DISCONNECTED";
    case ContextServiceServeStatus::ProtocolError:
      return "PROTOCOL_ERROR";
    case ContextServiceServeStatus::SecurityError:
      return "SECURITY_ERROR";
    case ContextServiceServeStatus::SystemError:
      return "SYSTEM_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
