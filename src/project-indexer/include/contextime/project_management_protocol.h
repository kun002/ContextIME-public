#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contextime/project_dictionary.h"
#include "contextime/project_indexer_protocol.h"

namespace contextime::management_protocol {

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kRequestFrameSize = 4096;
constexpr std::size_t kResponseFrameSize = 32768;
constexpr std::size_t kRequestPayloadSize = kRequestFrameSize - kHeaderSize;
constexpr std::size_t kResponsePayloadSize = kResponseFrameSize - kHeaderSize;
constexpr std::size_t kMaximumProjectsPerPage = 512;
constexpr std::size_t kMaximumEntriesPerPage = 128;
constexpr std::uint32_t kNoNextCursor = 0xffffffffu;
constexpr std::uint8_t kResponseAcknowledgement = 0x06;

using RequestFrame = std::array<std::uint8_t, kRequestFrameSize>;
using ResponseFrame = std::array<std::uint8_t, kResponseFrameSize>;
using ProjectId = project_protocol::ProjectId;

enum class MessageType : std::uint16_t {
  Request = 1,
  Response = 2,
};

enum class Operation : std::uint8_t {
  ListProjects = 1,
  ViewProject = 2,
  SetEnabled = 3,
  RemoveProject = 4,
  UpsertTerm = 5,
  RemoveEntry = 6,
};

enum class ResponseStatus : std::uint8_t {
  Ok = 0,
  MalformedRequest,
  NotFound,
  StoreError,
  InternalError,
};

enum class CodecStatus : std::uint8_t {
  Ok = 0,
  BadMagic,
  UnsupportedVersion,
  UnexpectedMessageType,
  InvalidPayloadSize,
  InvalidField,
  NonZeroReserved,
  AllocationFailure,
};

struct ManagementRequest {
  std::uint32_t request_id = 0;
  Operation operation = Operation::ListProjects;
  ProjectId project_id{};
  bool enabled = false;
  std::uint16_t page_size = 0;
  std::uint32_t cursor = 0;
  // Only UPSERT_TERM and REMOVE_ENTRY carry an entry on the wire. UPSERT_TERM
  // must target symbol_type=term/source=manual; the server owns frequency and
  // last_seen_ms, so the request values are ignored.
  ProjectDictionaryEntry entry{};
};

struct ManagementResponse {
  std::uint32_t request_id = 0;
  ResponseStatus status = ResponseStatus::Ok;
  Operation operation = Operation::ListProjects;
  ProjectDictionaryStatus store_status = ProjectDictionaryStatus::Ok;
  std::uint32_t total_count = 0;
  std::uint32_t next_cursor = kNoNextCursor;
  bool exists = false;
  bool enabled = false;
  std::vector<ProjectId> projects;
  std::vector<ProjectDictionaryEntry> entries;
};

CodecStatus EncodeRequest(const ManagementRequest& request,
                          RequestFrame& frame) noexcept;
CodecStatus DecodeRequest(const RequestFrame& frame,
                          ManagementRequest& request) noexcept;
CodecStatus EncodeResponse(const ManagementResponse& response,
                           ResponseFrame& frame) noexcept;
CodecStatus DecodeResponse(const ResponseFrame& frame,
                           ManagementResponse& response) noexcept;

bool HasManagementMagic(const RequestFrame& frame) noexcept;
std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept;
bool ProjectIdFromString(const std::string& value,
                         ProjectId& project_id) noexcept;
const char* ToString(Operation operation) noexcept;
const char* ToString(ResponseStatus status) noexcept;
const char* ToString(CodecStatus status) noexcept;

}  // namespace contextime::management_protocol
