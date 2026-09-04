#pragma once

#if !defined(_WIN32)
#error win_pipe_io.h is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace contextime::win_pipe_detail {

enum class IoStatus : std::uint8_t {
  Ok = 0,
  Timeout,
  Disconnected,
  SystemError,
};

struct IoResult {
  IoStatus status = IoStatus::SystemError;
  DWORD system_error = ERROR_SUCCESS;
};

class Deadline final {
 public:
  explicit Deadline(std::uint32_t timeout_ms) noexcept
      : expires_at_(GetTickCount64() + timeout_ms) {}

  DWORD RemainingMs() const noexcept {
    const ULONGLONG now = GetTickCount64();
    if (now >= expires_at_) {
      return 0;
    }
    const ULONGLONG remaining = expires_at_ - now;
    constexpr ULONGLONG kLargestFiniteWait =
        static_cast<ULONGLONG>(std::numeric_limits<DWORD>::max() - 1u);
    return static_cast<DWORD>(remaining > kLargestFiniteWait
                                  ? kLargestFiniteWait
                                  : remaining);
  }

 private:
  ULONGLONG expires_at_;
};

inline bool IsDisconnectedError(DWORD error) noexcept {
  return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ||
         error == ERROR_NO_DATA || error == ERROR_CONNECTION_ABORTED;
}

inline IoResult ClassifyIoError(DWORD error) noexcept {
  if (error == ERROR_SEM_TIMEOUT || error == WAIT_TIMEOUT) {
    return {IoStatus::Timeout, error};
  }
  if (IsDisconnectedError(error)) {
    return {IoStatus::Disconnected, error};
  }
  return {IoStatus::SystemError, error};
}

inline IoResult TransferExact(HANDLE pipe, void* buffer, std::size_t size,
                              bool write, Deadline& deadline) noexcept {
  auto* bytes = static_cast<std::uint8_t*>(buffer);
  std::size_t transferred = 0;
  while (transferred < size) {
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
      return {IoStatus::SystemError, GetLastError()};
    }

    OVERLAPPED operation{};
    operation.hEvent = event;
    DWORD completed = 0;
    const DWORD remaining = static_cast<DWORD>(size - transferred);
    const BOOL started =
        write ? WriteFile(pipe, bytes + transferred, remaining, &completed,
                          &operation)
              : ReadFile(pipe, bytes + transferred, remaining, &completed,
                         &operation);

    if (!started) {
      const DWORD start_error = GetLastError();
      if (start_error != ERROR_IO_PENDING) {
        CloseHandle(event);
        return ClassifyIoError(start_error);
      }

      const DWORD wait = WaitForSingleObject(event, deadline.RemainingMs());
      if (wait == WAIT_TIMEOUT) {
        const DWORD timeout_error = ERROR_SEM_TIMEOUT;
        CancelIoEx(pipe, &operation);
        // OVERLAPPED storage must remain alive until cancellation completes.
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
        return {IoStatus::Timeout, timeout_error};
      }
      if (wait != WAIT_OBJECT_0) {
        const DWORD wait_error = GetLastError();
        CancelIoEx(pipe, &operation);
        WaitForSingleObject(event, INFINITE);
        CloseHandle(event);
        return {IoStatus::SystemError, wait_error};
      }
      if (!GetOverlappedResult(pipe, &operation, &completed, FALSE)) {
        const DWORD completion_error = GetLastError();
        CloseHandle(event);
        return ClassifyIoError(completion_error);
      }
    }

    CloseHandle(event);
    if (completed == 0) {
      return {IoStatus::Disconnected, ERROR_BROKEN_PIPE};
    }
    transferred += completed;
  }
  return {IoStatus::Ok, ERROR_SUCCESS};
}

}  // namespace contextime::win_pipe_detail
