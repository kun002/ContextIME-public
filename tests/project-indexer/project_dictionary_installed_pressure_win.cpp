#include "contextime/project_dictionary.h"
#include "contextime/project_indexer_protocol.h"
#include "contextime/project_indexer_server.h"

#if !defined(_WIN32)
#error project_dictionary_installed_pressure_win.cpp is Windows-only
#endif

#include "win_pipe_io.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using contextime::ProjectDictionaryEntry;
using contextime::ProjectDictionarySnapshot;
using contextime::ProjectDictionaryStatus;
using contextime::ProjectDictionaryStore;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;
using contextime::project_protocol::RequestOperation;
using contextime::project_protocol::UpsertRequest;
using contextime::project_protocol::UpsertResponse;

constexpr char kProjectId[] = "11223344556677889900aabbccddeeff";
constexpr std::size_t kEntryCount =
    contextime::kMaximumProjectDictionaryEntries;
constexpr std::size_t kBatchSize =
    contextime::project_protocol::kMaximumRecordsPerRequest;

contextime::project_protocol::ProjectId ProjectId() {
  return {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
          0x99, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
}

std::string Symbol(std::size_t index) {
  std::ostringstream output;
  output << "PressureSymbol" << std::setfill('0') << std::setw(6) << index;
  return output.str();
}

ProjectDictionaryEntry Entry(std::size_t index, std::uint32_t frequency) {
  ProjectDictionaryEntry entry;
  entry.symbol = Symbol(index);
  entry.symbol_type = ProjectSymbolType::Class;
  entry.frequency = frequency;
  entry.last_seen_ms = 1;
  entry.source = ProjectSymbolSource::LanguageServer;
  return entry;
}

bool Exchange(const contextime::project_protocol::RequestFrame& request_frame,
              UpsertResponse& response) {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  const ULONGLONG expires_at = GetTickCount64() + 5000u;
  do {
    if (WaitNamedPipeW(contextime::kProjectIndexerPipeName, 250u)) {
      pipe = CreateFileW(contextime::kProjectIndexerPipeName,
                         GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
      if (pipe != INVALID_HANDLE_VALUE) break;
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY &&
        error != ERROR_SEM_TIMEOUT) {
      return false;
    }
    Sleep(10u);
  } while (GetTickCount64() < expires_at);
  if (pipe == INVALID_HANDLE_VALUE) return false;

  contextime::win_pipe_detail::Deadline deadline(5000u);
  const auto written = contextime::win_pipe_detail::TransferExact(
      pipe, const_cast<std::uint8_t*>(request_frame.data()),
      request_frame.size(), true, deadline);
  contextime::project_protocol::ResponseFrame response_frame;
  const auto read = written.status == contextime::win_pipe_detail::IoStatus::Ok
      ? contextime::win_pipe_detail::TransferExact(
            pipe, response_frame.data(), response_frame.size(), false, deadline)
      : contextime::win_pipe_detail::IoResult{};
  const bool decoded =
      read.status == contextime::win_pipe_detail::IoStatus::Ok &&
      contextime::project_protocol::DecodeUpsertResponse(
          response_frame, response) ==
          contextime::project_protocol::CodecStatus::Ok;
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

bool Send(UpsertRequest request, std::uint16_t expected_count) {
  contextime::project_protocol::RequestFrame frame;
  if (contextime::project_protocol::EncodeUpsertRequest(request, frame) !=
      contextime::project_protocol::CodecStatus::Ok) {
    return false;
  }
  UpsertResponse response;
  return Exchange(frame, response) && response.request_id == request.request_id &&
         response.status == contextime::project_protocol::ResponseStatus::Ok &&
         response.accepted && response.accepted_count == expected_count;
}

int Seed(const std::filesystem::path& root) {
  ProjectDictionaryStore store(root);
  std::vector<ProjectDictionaryEntry> entries;
  entries.reserve(kEntryCount);
  for (std::size_t index = 0; index < kEntryCount; ++index) {
    entries.push_back(Entry(index,
                            static_cast<std::uint32_t>((index % 100u) + 1u)));
  }
  const auto started = Clock::now();
  const ProjectDictionaryStatus status = store.Upsert(kProjectId, entries);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now() - started).count();
  const std::filesystem::path dictionary =
      root / (std::string(kProjectId) + ".dict");
  std::error_code error;
  const auto bytes = std::filesystem::file_size(dictionary, error);
  std::cout << "{\"mode\":\"seed\",\"status\":\""
            << contextime::ToString(status) << "\",\"entries\":"
            << kEntryCount << ",\"bytes\":" << (error ? 0u : bytes)
            << ",\"elapsed_ms\":" << elapsed << "}\n";
  return status == ProjectDictionaryStatus::Ok && !error
      ? EXIT_SUCCESS
      : EXIT_FAILURE;
}

int Pressure(std::uint32_t iterations) {
  std::uint32_t request_id = 1000u;
  std::uint64_t maximum_update_ms = 0;
  const auto all_started = Clock::now();
  for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
    UpsertRequest activate;
    activate.request_id = request_id++;
    activate.operation = RequestOperation::ActivateProject;
    activate.project_id = ProjectId();
    if (!Send(std::move(activate), 0u)) return EXIT_FAILURE;

    UpsertRequest update;
    update.request_id = request_id++;
    update.project_id = ProjectId();
    update.entries.reserve(kBatchSize);
    for (std::size_t offset = 0; offset < kBatchSize; ++offset) {
      update.entries.push_back(Entry(
          offset, static_cast<std::uint32_t>(1000u + iteration)));
    }
    const auto update_started = Clock::now();
    if (!Send(std::move(update), static_cast<std::uint16_t>(kBatchSize))) {
      return EXIT_FAILURE;
    }
    const auto update_ms =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - update_started).count());
    maximum_update_ms = (std::max)(maximum_update_ms, update_ms);
  }

  UpsertRequest deactivate;
  deactivate.request_id = request_id;
  deactivate.operation = RequestOperation::DeactivateProject;
  if (!Send(std::move(deactivate), 0u)) return EXIT_FAILURE;
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      Clock::now() - all_started).count();
  std::cout << "{\"mode\":\"pressure\",\"status\":\"OK\","
               "\"project_id\":\"" << kProjectId
            << "\",\"entries\":" << kEntryCount
            << ",\"batch_size\":" << kBatchSize
            << ",\"iterations\":" << iterations
            << ",\"elapsed_ms\":" << elapsed
            << ",\"maximum_update_ms\":" << maximum_update_ms << "}\n";
  return EXIT_SUCCESS;
}

int Cleanup(const std::filesystem::path& root) {
  ProjectDictionaryStore store(root);
  const ProjectDictionaryStatus status = store.Remove(kProjectId);
  std::cout << "{\"mode\":\"cleanup\",\"status\":\""
            << contextime::ToString(status) << "\"}\n";
  return status == ProjectDictionaryStatus::Ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  if (argc == 3 && std::wstring(argv[1]) == L"seed") {
    return Seed(std::filesystem::path(argv[2]));
  }
  if (argc == 3 && std::wstring(argv[1]) == L"pressure") {
    try {
      const unsigned long parsed = std::stoul(argv[2]);
      if (parsed == 0u || parsed > 1000u) return EXIT_FAILURE;
      return Pressure(static_cast<std::uint32_t>(parsed));
    } catch (...) {
      return EXIT_FAILURE;
    }
  }
  if (argc == 3 && std::wstring(argv[1]) == L"cleanup") {
    return Cleanup(std::filesystem::path(argv[2]));
  }
  std::cerr << "usage: project-dictionary-installed-pressure-tests "
               "seed|cleanup <dictionary-root> | pressure <iterations>\n";
  return EXIT_FAILURE;
}
