#include "contextime/active_project_snapshot.h"
#include "contextime/project_candidate_refresh_worker.h"
#include "contextime/project_candidate_server.h"

#if !defined(_WIN32)
#error project_candidate_refresh_worker_win_test.cpp is Windows-only
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

std::wstring UniquePipe(const wchar_t* fixture) {
  static LONG sequence = 0;
  return L"\\\\.\\pipe\\ContextIME.ProjectCandidate.WorkerTest." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(InterlockedIncrement(&sequence)) + L"." + fixture;
}

bool WaitUntil(const std::function<bool()>& predicate,
               std::uint32_t timeout_ms = 3000) {
  const std::uint64_t deadline = GetTickCount64() + timeout_ms;
  while (!predicate()) {
    if (GetTickCount64() >= deadline) return false;
    Sleep(10);
  }
  return true;
}

void Wake(const wchar_t* pipe_name) {
  if (!WaitNamedPipeW(pipe_name, 1000)) return;
  const HANDLE pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
}

contextime::ProjectDictionarySnapshot Dictionary() {
  contextime::ProjectDictionarySnapshot dictionary;
  dictionary.project_id = "00112233445566778899aabbccddeeff";
  contextime::ProjectDictionaryEntry entry;
  entry.symbol = "PlayerController";
  entry.symbol_type = contextime::ProjectSymbolType::Class;
  entry.frequency = 9;
  entry.last_seen_ms = 100;
  entry.source = contextime::ProjectSymbolSource::LanguageServer;
  dictionary.entries = {entry};
  return dictionary;
}

void TestPropertyEncoding() {
  contextime::candidate_protocol::ProjectCandidateResponse response;
  response.active = true;
  response.project_id = "00112233445566778899aabbccddeeff";
  contextime::ProjectCandidateEntry candidate;
  candidate.symbol = "PlayerController";
  candidate.symbol_type = contextime::ProjectSymbolType::Class;
  candidate.frequency = 9;
  response.candidates = {candidate};
  Expect(contextime::EncodeProjectCandidateProperty(response) ==
             "1\n0\t9\tPlayerController\n",
         "worker pre-encodes a bounded in-memory librime property");
  response.active = false;
  Expect(contextime::EncodeProjectCandidateProperty(response).empty(),
         "inactive project encodes as an empty fail-open property");
}

void TestBackgroundRefreshAndClear() {
  contextime::ActiveProjectSnapshotCache cache;
  cache.Publish(Dictionary(), GetTickCount64());
  const std::wstring pipe_name = UniquePipe(L"refresh");
  std::atomic<bool> stop_server{false};
  std::thread server([&] {
    while (!stop_server.load()) {
      (void)contextime::ServeOneProjectCandidateConnection(
          pipe_name.c_str(), 500, &cache);
    }
  });

  contextime::ProjectCandidateRefreshWorker worker;
  contextime::ProjectCandidateRefreshOptions options;
  options.pipe_name = pipe_name.c_str();
  options.poll_interval_ms = 20;
  options.io_timeout_ms = 300;
  Expect(worker.Start(options), "candidate refresh worker starts");
  Expect(WaitUntil([&] {
           const auto snapshot = worker.Read();
           return snapshot && snapshot->active &&
                  snapshot->property_value.find("PlayerController") !=
                      std::string::npos;
         }),
         "background worker publishes active project property");

  cache.Clear();
  Expect(WaitUntil([&] {
           const auto snapshot = worker.Read();
           return snapshot && !snapshot->active &&
                  snapshot->property_value.empty();
         }),
         "cleared service snapshot removes project property fail-open");

  worker.Stop();
  stop_server.store(true);
  Wake(pipe_name.c_str());
  server.join();
  Expect(!worker.running(), "candidate refresh worker joins cleanly");
}

void TestUnavailableServiceIsFailOpen() {
  const std::wstring pipe_name = UniquePipe(L"missing");
  contextime::ProjectCandidateRefreshWorker worker;
  contextime::ProjectCandidateRefreshOptions options;
  options.pipe_name = pipe_name.c_str();
  options.poll_interval_ms = 20;
  options.io_timeout_ms = 30;
  Expect(worker.Start(options), "worker starts without candidate service");
  Sleep(80);
  const auto snapshot = worker.Read();
  Expect(snapshot && !snapshot->active && snapshot->property_value.empty(),
         "missing candidate bridge keeps ordinary librime translators active");
  worker.Stop();
}

}  // namespace

int main() {
  TestPropertyEncoding();
  TestBackgroundRefreshAndClear();
  TestUnavailableServiceIsFailOpen();
  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Candidate Refresh Worker: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
