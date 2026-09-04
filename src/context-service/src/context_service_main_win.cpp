#include "contextime/context_service.h"
#include "contextime/editor_context.h"
#include "contextime/active_project_snapshot.h"
#include "contextime/project_candidate_server.h"
#include "contextime/project_dictionary.h"
#include "contextime/project_indexer_server.h"

#if !defined(_WIN32)
#error context_service_main_win.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
  const wchar_t* pipe_name = contextime::kContextServicePipeName;
  const wchar_t* mutex_name = contextime::kContextServiceMutexName;
  const wchar_t* stop_event_name = contextime::kContextServiceStopEventName;
  const wchar_t* project_pipe_name = contextime::kProjectIndexerPipeName;
  const wchar_t* project_candidate_pipe_name =
      contextime::kProjectCandidatePipeName;
  const wchar_t* project_dictionary_root = nullptr;
  std::uint32_t max_requests = 0;
  std::uint32_t client_timeout_ms = 1000;
  bool project_indexer_enabled = true;
  bool quit = false;
};

bool ParseUnsigned(const wchar_t* text, std::uint32_t& value) {
  if (text == nullptr || text[0] == L'\0' || text[0] == L'-') {
    return false;
  }
  wchar_t* end = nullptr;
  errno = 0;
  const unsigned long long parsed = std::wcstoull(text, &end, 10);
  if (errno != 0 || end == text || *end != L'\0' ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool WakePipe(const wchar_t* pipe_name) noexcept {
  const HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe);
    return true;
  }
  return GetLastError() == ERROR_PIPE_BUSY;
}

bool StopRequested(HANDLE stop_event) noexcept {
  return WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0;
}

void WakePipeWithRetry(const wchar_t* pipe_name) noexcept {
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (WakePipe(pipe_name)) return;
    Sleep(10);
  }
}

std::wstring DefaultProjectDictionaryRoot() {
  const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
  if (required == 0) return {};
  std::vector<wchar_t> buffer(required);
  const DWORD copied =
      GetEnvironmentVariableW(L"APPDATA", buffer.data(), required);
  if (copied == 0 || copied >= required) return {};
  std::wstring root(buffer.data(), copied);
  root += L"\\ContextIME\\project-dictionaries";
  return root;
}

int SignalQuit(const Options& options) noexcept {
  const HANDLE mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                                  options.mutex_name);
  if (mutex == nullptr) {
    return GetLastError() == ERROR_FILE_NOT_FOUND ? EXIT_SUCCESS
                                                  : EXIT_FAILURE;
  }

  HANDLE stop_event = nullptr;
  for (int attempt = 0; attempt < 50 && stop_event == nullptr; ++attempt) {
    stop_event =
        OpenEventW(EVENT_MODIFY_STATE, FALSE, options.stop_event_name);
    if (stop_event == nullptr && GetLastError() != ERROR_FILE_NOT_FOUND) {
      CloseHandle(mutex);
      return EXIT_FAILURE;
    }
    if (stop_event == nullptr) {
      Sleep(10);
    }
  }
  if (stop_event == nullptr) {
    CloseHandle(mutex);
    return EXIT_FAILURE;
  }

  const bool signalled = SetEvent(stop_event) != FALSE;
  CloseHandle(stop_event);
  if (!signalled) {
    CloseHandle(mutex);
    return EXIT_FAILURE;
  }
  // Wake a server blocked in ConnectNamedPipe. If it is already serving a
  // client, its bounded client timeout will return it to the stop check.
  WakePipeWithRetry(options.pipe_name);
  if (options.project_indexer_enabled) {
    WakePipeWithRetry(options.project_pipe_name);
    WakePipeWithRetry(options.project_candidate_pipe_name);
  }

  const DWORD wait_result = WaitForSingleObject(mutex, 5000);
  const bool stopped = wait_result == WAIT_OBJECT_0 ||
                       wait_result == WAIT_ABANDONED;
  if (stopped) {
    ReleaseMutex(mutex);
  }
  CloseHandle(mutex);
  return stopped ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Options options;
  std::wstring default_project_dictionary_root;

  for (int index = 1; index < argc; ++index) {
    if (std::wcscmp(argv[index], L"--pipe") == 0 && index + 1 < argc) {
      options.pipe_name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--mutex") == 0 &&
               index + 1 < argc) {
      options.mutex_name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--stop-event") == 0 &&
               index + 1 < argc) {
      options.stop_event_name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--project-pipe") == 0 &&
               index + 1 < argc) {
      options.project_pipe_name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--project-candidate-pipe") == 0 &&
               index + 1 < argc) {
      options.project_candidate_pipe_name = argv[++index];
    } else if (std::wcscmp(argv[index], L"--project-dictionary-root") == 0 &&
               index + 1 < argc) {
      options.project_dictionary_root = argv[++index];
    } else if (std::wcscmp(argv[index], L"--max-requests") == 0 &&
               index + 1 < argc &&
               ParseUnsigned(argv[++index], options.max_requests)) {
      continue;
    } else if (std::wcscmp(argv[index], L"--client-timeout-ms") == 0 &&
               index + 1 < argc &&
               ParseUnsigned(argv[++index], options.client_timeout_ms)) {
      continue;
    } else if (std::wcscmp(argv[index], L"--quit") == 0) {
      options.quit = true;
    } else if (std::wcscmp(argv[index], L"--disable-project-indexer") == 0) {
      options.project_indexer_enabled = false;
    } else {
      std::wcerr << L"Usage: contextime-context-service.exe "
                    L"[--pipe NAME] [--mutex NAME] [--stop-event NAME] "
                    L"[--project-pipe NAME] "
                    L"[--project-candidate-pipe NAME] "
                    L"[--project-dictionary-root PATH] "
                    L"[--disable-project-indexer] [--max-requests N] "
                    L"[--client-timeout-ms N] [--quit]\n";
      return EXIT_FAILURE;
    }
  }

  if (options.project_indexer_enabled &&
      options.project_dictionary_root == nullptr) {
    default_project_dictionary_root = DefaultProjectDictionaryRoot();
    if (default_project_dictionary_root.empty()) {
      options.project_indexer_enabled = false;
    } else {
      options.project_dictionary_root =
          default_project_dictionary_root.c_str();
    }
  }

  if (options.quit) {
    return SignalQuit(options);
  }

  const HANDLE mutex = CreateMutexW(nullptr, TRUE, options.mutex_name);
  if (mutex == nullptr) {
    return EXIT_FAILURE;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return EXIT_SUCCESS;
  }

  const HANDLE stop_event =
      CreateEventW(nullptr, TRUE, FALSE, options.stop_event_name);
  if (stop_event == nullptr || !ResetEvent(stop_event)) {
    if (stop_event != nullptr) {
      CloseHandle(stop_event);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return EXIT_FAILURE;
  }

  int result = EXIT_SUCCESS;
  std::uint32_t served = 0;
  contextime::EditorContextStore editor_context_store;
  std::unique_ptr<contextime::ProjectDictionaryStore> project_store;
  std::unique_ptr<contextime::ActiveProjectSnapshotCache> project_snapshot;
  std::thread project_thread;
  std::thread project_candidate_thread;
  if (options.project_indexer_enabled) {
    try {
      project_store = std::make_unique<contextime::ProjectDictionaryStore>(
          std::filesystem::path(options.project_dictionary_root));
      project_snapshot =
          std::make_unique<contextime::ActiveProjectSnapshotCache>();
    } catch (...) {
      project_store.reset();
      project_snapshot.reset();
    }
  }
  if (project_store && project_snapshot) {
    try {
      project_thread = std::thread([&] {
        while (!StopRequested(stop_event)) {
          const auto status = contextime::ServeOneProjectIndexerConnection(
              options.project_pipe_name, options.client_timeout_ms,
              project_store.get(), project_snapshot.get());
          if (StopRequested(stop_event)) break;
          if (status == contextime::ProjectIndexerServeStatus::SecurityError ||
              status == contextime::ProjectIndexerServeStatus::SystemError) {
            std::cerr << "Project Indexer stopped: "
                      << contextime::ToString(status) << ", Windows error "
                      << GetLastError() << '\n';
            break;
          }
        }
      });
    } catch (...) {
    }
    try {
      project_candidate_thread = std::thread([&] {
        while (!StopRequested(stop_event)) {
          const auto status = contextime::ServeOneProjectCandidateConnection(
              options.project_candidate_pipe_name,
              options.client_timeout_ms, project_snapshot.get());
          if (StopRequested(stop_event)) break;
          if (status ==
                  contextime::ProjectCandidateServeStatus::SecurityError ||
              status ==
                  contextime::ProjectCandidateServeStatus::SystemError) {
            std::cerr << "Project Candidate bridge stopped: "
                      << contextime::ToString(status) << ", Windows error "
                      << GetLastError() << '\n';
            break;
          }
        }
      });
    } catch (...) {
    }
  }
  while (!StopRequested(stop_event) &&
         (options.max_requests == 0 || served < options.max_requests)) {
    const auto status = contextime::ServeOneContextServiceConnection(
        options.pipe_name, options.client_timeout_ms, &editor_context_store);
    if (StopRequested(stop_event)) {
      break;
    }
    ++served;
    if (status == contextime::ContextServiceServeStatus::SecurityError ||
        status == contextime::ContextServiceServeStatus::SystemError) {
      std::cerr << "Context Service stopped: " << contextime::ToString(status)
                << ", Windows error " << GetLastError() << '\n';
      result = EXIT_FAILURE;
      break;
    }
  }

  SetEvent(stop_event);
  if (project_thread.joinable()) {
    WakePipeWithRetry(options.project_pipe_name);
    project_thread.join();
  }
  if (project_candidate_thread.joinable()) {
    WakePipeWithRetry(options.project_candidate_pipe_name);
    project_candidate_thread.join();
  }
  CloseHandle(stop_event);
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return result;
}
