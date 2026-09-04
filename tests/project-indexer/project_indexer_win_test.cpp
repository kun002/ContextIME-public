#include "contextime/active_project_snapshot.h"
#include "contextime/project_candidate_client.h"
#include "contextime/project_candidate_server.h"
#include "contextime/project_dictionary.h"
#include "contextime/project_indexer_protocol.h"
#include "contextime/project_indexer_server.h"
#include "contextime/project_management_client.h"
#include "contextime/project_management_protocol.h"

#if !defined(_WIN32)
#error project_indexer_win_test.cpp is Windows-only
#endif

#include "win_pipe_io.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using contextime::ProjectDictionaryEntry;
using contextime::ActiveProjectSnapshotCache;
using contextime::ProjectDictionarySnapshot;
using contextime::ProjectDictionaryStatus;
using contextime::ProjectDictionaryStore;
using contextime::ProjectIndexerServeStatus;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    path_ = std::filesystem::temp_directory_path() /
            ("contextime-project-pipe-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

std::wstring UniquePipe(const wchar_t* fixture) {
  static LONG sequence = 0;
  return L"\\\\.\\pipe\\ContextIME.ProjectIndexer.Test." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(InterlockedIncrement(&sequence)) + L"." + fixture;
}

bool WaitForPipe(const wchar_t* pipe_name) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (WaitNamedPipeW(pipe_name, 20)) return true;
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_SEM_TIMEOUT) {
      return false;
    }
    Sleep(5);
  }
  return false;
}

contextime::project_protocol::UpsertRequest Request() {
  contextime::project_protocol::UpsertRequest request;
  request.request_id = 101;
  for (std::size_t index = 0; index < request.project_id.size(); ++index) {
    request.project_id[index] = static_cast<std::uint8_t>(index);
  }
  ProjectDictionaryEntry first;
  first.symbol = "PlayerController";
  first.symbol_type = ProjectSymbolType::Class;
  first.frequency = 2;
  first.source = ProjectSymbolSource::LanguageServer;
  ProjectDictionaryEntry second;
  second.symbol = "SpawnPlayer";
  second.symbol_type = ProjectSymbolType::Method;
  second.frequency = 1;
  second.source = ProjectSymbolSource::LanguageServer;
  request.entries = {first, second};
  return request;
}

bool Exchange(
    const wchar_t* pipe_name,
    const contextime::project_protocol::RequestFrame& request_frame,
    contextime::project_protocol::UpsertResponse& response) {
  HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                            nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return false;
  contextime::win_pipe_detail::Deadline deadline(1000);
  const auto written = contextime::win_pipe_detail::TransferExact(
      pipe, const_cast<std::uint8_t*>(request_frame.data()),
      request_frame.size(), true, deadline);
  contextime::project_protocol::ResponseFrame response_frame;
  const auto read = written.status == contextime::win_pipe_detail::IoStatus::Ok
      ? contextime::win_pipe_detail::TransferExact(
            pipe, response_frame.data(), response_frame.size(), false, deadline)
      : contextime::win_pipe_detail::IoResult{};
  bool decoded = false;
  if (read.status == contextime::win_pipe_detail::IoStatus::Ok) {
    decoded = contextime::project_protocol::DecodeUpsertResponse(
                  response_frame, response) ==
              contextime::project_protocol::CodecStatus::Ok;
  }
  std::uint8_t acknowledgement =
      contextime::project_protocol::kResponseAcknowledgement;
  const auto acknowledged = decoded
      ? contextime::win_pipe_detail::TransferExact(
            pipe, &acknowledgement, sizeof(acknowledgement), true, deadline)
      : contextime::win_pipe_detail::IoResult{};
  CloseHandle(pipe);
  return written.status == contextime::win_pipe_detail::IoStatus::Ok &&
         read.status == contextime::win_pipe_detail::IoStatus::Ok && decoded &&
         acknowledged.status == contextime::win_pipe_detail::IoStatus::Ok;
}

bool ManagementExchange(
    const std::wstring& pipe_name, ProjectDictionaryStore& store,
    ActiveProjectSnapshotCache& snapshot_cache,
    const contextime::management_protocol::ManagementRequest& request,
    contextime::management_protocol::ManagementResponse& response,
    ProjectIndexerServeStatus& serve_status,
    std::uint32_t server_timeout_ms = 2000,
    std::uint32_t client_timeout_ms = 2000) {
  std::thread server([&] {
    serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), server_timeout_ms, &store, &snapshot_cache);
  });
  const bool ready = WaitForPipe(pipe_name.c_str());
  const auto call_status = ready
      ? contextime::CallProjectManagement(pipe_name.c_str(), client_timeout_ms,
                                          request, response)
      : contextime::ProjectManagementCallStatus::Unavailable;
  server.join();
  return ready &&
         call_status == contextime::ProjectManagementCallStatus::Ok;
}

void TestLargeManagementViewDoesNotSpendResponseIoBudgetOnStoreWork() {
  TemporaryDirectory temporary;
  ActiveProjectSnapshotCache snapshot_cache;
  const auto upsert = Request();
  const std::string project_id =
      contextime::project_protocol::ProjectIdToString(upsert.project_id);
  std::vector<ProjectDictionaryEntry> entries;
  entries.reserve(contextime::kMaximumProjectDictionaryEntries);
  for (std::size_t index = 0;
       index < contextime::kMaximumProjectDictionaryEntries; ++index) {
    ProjectDictionaryEntry entry;
    entry.symbol = "LargeSymbol" + std::to_string(index);
    // Keep the fixture below every persisted limit while making a cold parse
    // materially longer than the server's one-second I/O-only deadline.
    entry.symbol.append(64 - entry.symbol.size(), 'x');
    entry.symbol_type = ProjectSymbolType::Class;
    entry.frequency = 1;
    entry.last_seen_ms = 1;
    entry.source = ProjectSymbolSource::LanguageServer;
    entries.push_back(entry);
  }
  {
    ProjectDictionaryStore seed_store(temporary.path());
    Expect(seed_store.Upsert(project_id, entries) ==
               ProjectDictionaryStatus::Ok,
           "large management fixture persists");
  }
  ProjectDictionaryStore store(temporary.path());

  contextime::management_protocol::ManagementRequest view;
  view.request_id = 207;
  view.operation = contextime::management_protocol::Operation::ViewProject;
  view.project_id = upsert.project_id;
  view.page_size = 100;
  const std::wstring pipe_name = UniquePipe(L"large-management");
  ProjectIndexerServeStatus serve_status =
      ProjectIndexerServeStatus::SystemError;
  contextime::management_protocol::ManagementResponse response;
  const bool exchanged = ManagementExchange(
      pipe_name, store, snapshot_cache, view, response, serve_status, 1000,
      10000);
  Expect(exchanged,
         "large management store work cannot consume response I/O budget");
  Expect(serve_status == ProjectIndexerServeStatus::Served,
         "large management response completes its bounded server exchange");
  Expect(response.status ==
                 contextime::management_protocol::ResponseStatus::Ok &&
             response.total_count ==
                 contextime::kMaximumProjectDictionaryEntries &&
             response.entries.size() == 100 && response.next_cursor == 100,
         "large management view returns one bounded page");
}

void TestSuccessfulUpdatePersistsOnDedicatedPipe() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  ActiveProjectSnapshotCache snapshot_cache;
  const std::wstring pipe_name = UniquePipe(L"success");
  ProjectIndexerServeStatus serve_status =
      ProjectIndexerServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "project indexer pipe becomes ready");

  const auto request = Request();
  contextime::project_protocol::RequestFrame request_frame;
  Expect(contextime::project_protocol::EncodeUpsertRequest(
             request, request_frame) ==
             contextime::project_protocol::CodecStatus::Ok,
         "project indexer request encodes");
  contextime::project_protocol::UpsertResponse response;
  Expect(Exchange(pipe_name.c_str(), request_frame, response),
         "project indexer exchange completes");
  server.join();

  Expect(serve_status == ProjectIndexerServeStatus::Served,
         "project indexer server reports served");
  Expect(response.status == contextime::project_protocol::ResponseStatus::Ok &&
             response.accepted && response.accepted_count == 2,
         "project indexer accepts exact record count");
  ProjectDictionarySnapshot snapshot;
  Expect(store.Load(contextime::project_protocol::ProjectIdToString(
                        request.project_id),
                    snapshot) == ProjectDictionaryStatus::Ok,
         "stored project reload succeeds");
  Expect(snapshot.entries.size() == 2,
         "accepted symbols persist in isolated dictionary");
  Expect(snapshot.entries[0].last_seen_ms > 0 &&
             snapshot.entries[1].last_seen_ms > 0,
         "native server owns observation time");
  Expect(snapshot_cache.Read()->project_id.empty(),
         "upsert alone cannot globally activate project candidates");

  auto activate = request;
  activate.request_id = 102;
  activate.operation =
      contextime::project_protocol::RequestOperation::ActivateProject;
  activate.entries.clear();
  std::thread activation_server([&] {
    serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "activation pipe becomes ready");
  Expect(contextime::project_protocol::EncodeUpsertRequest(
             activate, request_frame) ==
             contextime::project_protocol::CodecStatus::Ok,
         "activation request encodes");
  Expect(Exchange(pipe_name.c_str(), request_frame, response),
         "activation exchange completes");
  activation_server.join();
  const auto active = snapshot_cache.Read();
  Expect(response.accepted && response.accepted_count == 0 && active &&
             active->project_id ==
                 contextime::project_protocol::ProjectIdToString(
                     request.project_id) &&
             active->candidates.size() == 2,
         "activation loads one immutable project candidate snapshot");
  Expect(std::wstring(contextime::kProjectIndexerPipeName) !=
             L"\\\\.\\pipe\\ContextIME.ContextService.v1" &&
             std::wstring(contextime::kProjectIndexerPipeName).find(L"Weasel") ==
                 std::wstring::npos,
         "project pipe is isolated from decision and Weasel pipes");
}

void TestMalformedUpdateCannotReachStore() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  ActiveProjectSnapshotCache snapshot_cache;
  const std::wstring pipe_name = UniquePipe(L"malformed");
  ProjectIndexerServeStatus serve_status =
      ProjectIndexerServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "malformed pipe becomes ready");

  const auto request = Request();
  contextime::project_protocol::RequestFrame request_frame;
  contextime::project_protocol::EncodeUpsertRequest(request, request_frame);
  request_frame[48] = 99;
  contextime::project_protocol::UpsertResponse response;
  Expect(Exchange(pipe_name.c_str(), request_frame, response),
         "malformed update receives bounded response");
  server.join();

  Expect(serve_status == ProjectIndexerServeStatus::ProtocolError,
         "malformed update reports protocol error");
  Expect(response.status ==
             contextime::project_protocol::ResponseStatus::MalformedRequest &&
             !response.accepted && response.accepted_count == 0,
         "malformed update is rejected");
  ProjectDictionarySnapshot snapshot;
  Expect(store.Load(contextime::project_protocol::ProjectIdToString(
                        request.project_id),
                    snapshot) == ProjectDictionaryStatus::Ok &&
             !snapshot.exists,
         "malformed update never reaches storage");
}

void TestManagementUsesIndexerOwnerAndRefreshesActiveSnapshot() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  ActiveProjectSnapshotCache snapshot_cache;
  const std::wstring pipe_name = UniquePipe(L"management");
  const auto upsert = Request();
  const std::string project_id =
      contextime::project_protocol::ProjectIdToString(upsert.project_id);
  auto entries = upsert.entries;
  entries[0].last_seen_ms = 100;
  entries[1].last_seen_ms = 100;
  Expect(store.Upsert(project_id, entries) == ProjectDictionaryStatus::Ok,
         "management fixture persists through the owner store");
  ProjectDictionarySnapshot initial;
  Expect(store.Load(project_id, initial) == ProjectDictionaryStatus::Ok,
         "management fixture loads");
  snapshot_cache.Publish(initial, GetTickCount64());

  ProjectIndexerServeStatus serve_status =
      ProjectIndexerServeStatus::SystemError;
  contextime::management_protocol::ManagementResponse response;
  contextime::management_protocol::ManagementRequest list;
  list.request_id = 201;
  list.operation =
      contextime::management_protocol::Operation::ListProjects;
  list.page_size = 10;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, list, response,
                            serve_status) &&
             serve_status == ProjectIndexerServeStatus::Served &&
             response.status ==
                 contextime::management_protocol::ResponseStatus::Ok &&
             response.total_count == 1 && response.projects.size() == 1 &&
             contextime::project_protocol::ProjectIdToString(
                 response.projects[0]) == project_id,
         "management lists isolated project IDs through the indexer pipe");

  contextime::management_protocol::ManagementRequest view;
  view.request_id = 202;
  view.operation = contextime::management_protocol::Operation::ViewProject;
  view.project_id = upsert.project_id;
  view.page_size = 1;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, view, response,
                            serve_status) &&
             response.status ==
                 contextime::management_protocol::ResponseStatus::Ok &&
             response.exists && response.enabled &&
             response.total_count == 2 && response.entries.size() == 1 &&
             response.next_cursor == 1,
         "management views a bounded first entry page");
  view.request_id = 203;
  view.cursor = response.next_cursor;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, view, response,
                            serve_status) &&
             response.entries.size() == 1 &&
             response.next_cursor ==
                 contextime::management_protocol::kNoNextCursor,
         "management views a bounded final entry page");

  contextime::management_protocol::ManagementRequest disable;
  disable.request_id = 204;
  disable.operation = contextime::management_protocol::Operation::SetEnabled;
  disable.project_id = upsert.project_id;
  disable.enabled = false;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, disable,
                            response, serve_status) &&
             response.exists && !response.enabled &&
             response.total_count == 2,
         "management disables a persisted project");
  auto active = snapshot_cache.Read();
  Expect(active && active->project_id == project_id &&
             active->candidates.empty(),
         "disable immediately republishes an empty active snapshot");

  disable.request_id = 205;
  disable.enabled = true;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, disable,
                            response, serve_status) &&
             response.enabled,
         "management re-enables a persisted project");
  active = snapshot_cache.Read();
  Expect(active && active->project_id == project_id &&
             active->candidates.size() == 2,
         "enable republishes the active immutable candidate snapshot");

  contextime::management_protocol::ManagementRequest remove;
  remove.request_id = 206;
  remove.operation =
      contextime::management_protocol::Operation::RemoveProject;
  remove.project_id = upsert.project_id;
  Expect(ManagementExchange(pipe_name, store, snapshot_cache, remove,
                            response, serve_status) &&
             response.status ==
                 contextime::management_protocol::ResponseStatus::Ok,
         "management removes the selected dictionary");
  ProjectDictionarySnapshot removed;
  active = snapshot_cache.Read();
  Expect(store.Load(project_id, removed) == ProjectDictionaryStatus::Ok &&
             !removed.exists && active && active->project_id.empty(),
         "remove clears storage and the active immutable snapshot");
}

void TestManagementRetriesPipeRecreationWindow() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  ActiveProjectSnapshotCache snapshot_cache;
  const auto upsert = Request();
  const std::string project_id =
      contextime::project_protocol::ProjectIdToString(upsert.project_id);
  auto entries = upsert.entries;
  entries[0].last_seen_ms = 100;
  entries[1].last_seen_ms = 100;
  Expect(store.Upsert(project_id, entries) == ProjectDictionaryStatus::Ok,
         "pipe recreation fixture persists");

  const std::wstring pipe_name = UniquePipe(L"management-recreate");
  ProjectIndexerServeStatus mutation_serve_status =
      ProjectIndexerServeStatus::SystemError;
  ProjectIndexerServeStatus view_serve_status =
      ProjectIndexerServeStatus::SystemError;
  std::thread server([&] {
    mutation_serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
    Sleep(25);
    view_serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()),
         "management mutation pipe becomes ready");

  contextime::management_protocol::ManagementRequest disable;
  disable.request_id = 208;
  disable.operation =
      contextime::management_protocol::Operation::SetEnabled;
  disable.project_id = upsert.project_id;
  disable.enabled = false;
  contextime::management_protocol::ManagementResponse mutation_response;
  const auto mutation_status = contextime::CallProjectManagement(
      pipe_name.c_str(), 1000, disable, mutation_response);

  contextime::management_protocol::ManagementRequest view;
  view.request_id = 209;
  view.operation = contextime::management_protocol::Operation::ViewProject;
  view.project_id = upsert.project_id;
  view.page_size = 100;
  contextime::management_protocol::ManagementResponse view_response;
  const auto view_status = contextime::CallProjectManagement(
      pipe_name.c_str(), 1000, view, view_response);
  server.join();

  Expect(mutation_status == contextime::ProjectManagementCallStatus::Ok &&
             mutation_serve_status == ProjectIndexerServeStatus::Served &&
             mutation_response.status ==
                 contextime::management_protocol::ResponseStatus::Ok &&
             !mutation_response.enabled,
         "management mutation completes before pipe recreation");
  Expect(view_status == contextime::ProjectManagementCallStatus::Ok &&
             view_serve_status == ProjectIndexerServeStatus::Served &&
             view_response.status ==
                 contextime::management_protocol::ResponseStatus::Ok &&
             view_response.exists && !view_response.enabled &&
             view_response.total_count == 2 &&
             view_response.entries.size() == 2,
         "immediate view retries through the pipe recreation window");
}

void TestStoreFailureIsApplicationError() {
  TemporaryDirectory temporary;
  const auto request = Request();
  const std::string project_id =
      contextime::project_protocol::ProjectIdToString(request.project_id);
  {
    std::ofstream corrupt(temporary.path() / (project_id + ".dict"),
                          std::ios::binary);
    corrupt << "corrupt";
  }
  ProjectDictionaryStore store(temporary.path());
  ActiveProjectSnapshotCache snapshot_cache;
  const std::wstring pipe_name = UniquePipe(L"store-error");
  ProjectIndexerServeStatus serve_status =
      ProjectIndexerServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneProjectIndexerConnection(
        pipe_name.c_str(), 1000, &store, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "store error pipe becomes ready");

  contextime::project_protocol::RequestFrame request_frame;
  contextime::project_protocol::EncodeUpsertRequest(request, request_frame);
  contextime::project_protocol::UpsertResponse response;
  Expect(Exchange(pipe_name.c_str(), request_frame, response),
         "store failure receives bounded response");
  server.join();
  Expect(serve_status == ProjectIndexerServeStatus::Served,
         "store failure does not crash the pipe service");
  Expect(response.status ==
             contextime::project_protocol::ResponseStatus::StoreError &&
             !response.accepted,
         "store failure remains an application error");
}

void TestImmutableCandidateSnapshotPipe() {
  ActiveProjectSnapshotCache snapshot_cache;
  ProjectDictionarySnapshot dictionary;
  dictionary.project_id = "00112233445566778899aabbccddeeff";
  ProjectDictionaryEntry entry;
  entry.symbol = "PlayerController";
  entry.symbol_type = ProjectSymbolType::Class;
  entry.frequency = 9;
  entry.last_seen_ms = 100;
  entry.source = ProjectSymbolSource::LanguageServer;
  dictionary.entries = {entry};
  snapshot_cache.Publish(dictionary, GetTickCount64());

  const std::wstring pipe_name = UniquePipe(L"candidate");
  contextime::ProjectCandidateServeStatus serve_status =
      contextime::ProjectCandidateServeStatus::SystemError;
  std::thread server([&] {
    serve_status = contextime::ServeOneProjectCandidateConnection(
        pipe_name.c_str(), 1000, &snapshot_cache);
  });
  Expect(WaitForPipe(pipe_name.c_str()), "candidate snapshot pipe becomes ready");
  contextime::candidate_protocol::ProjectCandidateResponse response;
  const auto fetch_status = contextime::FetchProjectCandidateSnapshot(
      pipe_name.c_str(), 1000, 501, response);
  server.join();

  Expect(fetch_status == contextime::ProjectCandidateFetchStatus::Ok &&
             serve_status ==
                 contextime::ProjectCandidateServeStatus::Served,
         "candidate snapshot exchange completes with ACK");
  Expect(response.request_id == 501 && response.active &&
             response.project_id == dictionary.project_id &&
             response.candidates.size() == 1 &&
             response.candidates[0].symbol == "PlayerController",
         "candidate transport exposes only active immutable symbol data");
  Expect(std::wstring(contextime::kProjectCandidatePipeName).find(L"Weasel") ==
             std::wstring::npos &&
             std::wstring(contextime::kProjectCandidatePipeName) !=
                 std::wstring(contextime::kProjectIndexerPipeName),
         "candidate snapshot pipe has an isolated ContextIME identity");
}

}  // namespace

int main() {
  TestSuccessfulUpdatePersistsOnDedicatedPipe();
  TestMalformedUpdateCannotReachStore();
  TestManagementUsesIndexerOwnerAndRefreshesActiveSnapshot();
  TestManagementRetriesPipeRecreationWindow();
  TestLargeManagementViewDoesNotSpendResponseIoBudgetOnStoreWork();
  TestStoreFailureIsApplicationError();
  TestImmutableCandidateSnapshotPipe();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Indexer Windows: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
