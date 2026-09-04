#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contextime/project_dictionary.h"

namespace contextime::project_protocol {

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kRequestFrameSize = 4096;
constexpr std::size_t kResponseFrameSize = 32;
constexpr std::size_t kRequestPayloadSize = kRequestFrameSize - kHeaderSize;
constexpr std::size_t kResponsePayloadSize = kResponseFrameSize - kHeaderSize;
constexpr std::size_t kProjectIdBytes = 16;
constexpr std::size_t kMaximumRecordsPerRequest = 64;
constexpr std::uint8_t kResponseAcknowledgement = 0x06;

using RequestFrame = std::array<std::uint8_t, kRequestFrameSize>;
using ResponseFrame = std::array<std::uint8_t, kResponseFrameSize>;
using ProjectId = std::array<std::uint8_t, kProjectIdBytes>;

enum class MessageType : std::uint16_t {
  UpsertRequest = 1,
  UpsertResponse = 2,
};

enum class RequestOperation : std::uint8_t {
  Upsert = 1,
  ActivateProject = 2,
  DeactivateProject = 3,
};

enum class ResponseStatus : std::uint8_t {
  Ok = 0,
  MalformedRequest,
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

struct UpsertRequest {
  std::uint32_t request_id = 0;
  RequestOperation operation = RequestOperation::Upsert;
  ProjectId project_id{};
  std::vector<ProjectDictionaryEntry> entries;
};

struct UpsertResponse {
  std::uint32_t request_id = 0;
  ResponseStatus status = ResponseStatus::Ok;
  bool accepted = false;
  std::uint16_t accepted_count = 0;
};

CodecStatus EncodeUpsertRequest(const UpsertRequest& request,
                                RequestFrame& frame) noexcept;
CodecStatus DecodeUpsertRequest(const RequestFrame& frame,
                                UpsertRequest& request) noexcept;
CodecStatus EncodeUpsertResponse(const UpsertResponse& response,
                                 ResponseFrame& frame) noexcept;
CodecStatus DecodeUpsertResponse(const ResponseFrame& frame,
                                 UpsertResponse& response) noexcept;

std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept;
std::string ProjectIdToString(const ProjectId& project_id);
const char* ToString(ResponseStatus status) noexcept;
const char* ToString(CodecStatus status) noexcept;

}  // namespace contextime::project_protocol
