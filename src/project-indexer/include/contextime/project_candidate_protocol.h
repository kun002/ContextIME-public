#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "contextime/active_project_snapshot.h"

namespace contextime::candidate_protocol {

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kQueryFrameSize = 32;
constexpr std::size_t kResponseFrameSize = 32768;
constexpr std::size_t kProjectIdBytes = 16;
constexpr std::uint8_t kResponseAcknowledgement = 0x06;

using QueryFrame = std::array<std::uint8_t, kQueryFrameSize>;
using ResponseFrame = std::array<std::uint8_t, kResponseFrameSize>;

enum class MessageType : std::uint16_t {
  Query = 1,
  Snapshot = 2,
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

struct ProjectCandidateQuery {
  std::uint32_t request_id = 0;
};

struct ProjectCandidateResponse {
  std::uint32_t request_id = 0;
  std::uint64_t generation = 0;
  bool active = false;
  std::uint32_t ttl_ms = 0;
  std::string project_id;
  std::vector<ProjectCandidateEntry> candidates;
};

CodecStatus EncodeQuery(const ProjectCandidateQuery& query,
                        QueryFrame& frame) noexcept;
CodecStatus DecodeQuery(const QueryFrame& frame,
                        ProjectCandidateQuery& query) noexcept;
CodecStatus EncodeResponse(const ProjectCandidateResponse& response,
                           ResponseFrame& frame) noexcept;
CodecStatus DecodeResponse(const ResponseFrame& frame,
                           ProjectCandidateResponse& response) noexcept;

std::uint32_t ReadRequestId(const QueryFrame& frame) noexcept;
const char* ToString(CodecStatus status) noexcept;

}  // namespace contextime::candidate_protocol
