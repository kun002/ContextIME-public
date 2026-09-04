#include "contextime/project_candidate_protocol.h"

#include <algorithm>
#include <utility>

namespace contextime::candidate_protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'C', 'I', 'P', 'C'};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kMessageTypeOffset = 6;
constexpr std::size_t kPayloadSizeOffset = 8;
constexpr std::size_t kRequestIdOffset = 12;
constexpr std::size_t kGenerationOffset = kHeaderSize;
constexpr std::size_t kActiveOffset = kHeaderSize + 8;
constexpr std::size_t kCountOffset = kHeaderSize + 10;
constexpr std::size_t kTtlOffset = kHeaderSize + 12;
constexpr std::size_t kProjectIdOffset = kHeaderSize + 16;
constexpr std::size_t kRecordsOffset = kHeaderSize + 32;
constexpr std::size_t kRecordHeaderSize = 8;

void WriteU16(std::uint8_t* output, std::uint16_t value) noexcept {
  output[0] = static_cast<std::uint8_t>(value & 0xffu);
  output[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteU32(std::uint8_t* output, std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < 4; ++index) {
    output[index] = static_cast<std::uint8_t>(value & 0xffu);
    value >>= 8u;
  }
}

void WriteU64(std::uint8_t* output, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < 8; ++index) {
    output[index] = static_cast<std::uint8_t>(value & 0xffu);
    value >>= 8u;
  }
}

std::uint16_t ReadU16(const std::uint8_t* input) noexcept {
  return static_cast<std::uint16_t>(input[0]) |
         static_cast<std::uint16_t>(input[1] << 8u);
}

std::uint32_t ReadU32(const std::uint8_t* input) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(input[index]) << (index * 8u);
  }
  return value;
}

std::uint64_t ReadU64(const std::uint8_t* input) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8u);
  }
  return value;
}

template <std::size_t Size>
void WriteHeader(std::array<std::uint8_t, Size>& frame, MessageType type,
                 std::uint32_t request_id) noexcept {
  std::copy(kMagic.begin(), kMagic.end(), frame.begin());
  WriteU16(frame.data() + kVersionOffset, kProtocolVersion);
  WriteU16(frame.data() + kMessageTypeOffset,
           static_cast<std::uint16_t>(type));
  WriteU32(frame.data() + kPayloadSizeOffset,
           static_cast<std::uint32_t>(Size - kHeaderSize));
  WriteU32(frame.data() + kRequestIdOffset, request_id);
}

template <std::size_t Size>
CodecStatus ValidateHeader(const std::array<std::uint8_t, Size>& frame,
                           MessageType expected) noexcept {
  if (!std::equal(kMagic.begin(), kMagic.end(), frame.begin())) {
    return CodecStatus::BadMagic;
  }
  if (ReadU16(frame.data() + kVersionOffset) != kProtocolVersion) {
    return CodecStatus::UnsupportedVersion;
  }
  if (ReadU16(frame.data() + kMessageTypeOffset) !=
      static_cast<std::uint16_t>(expected)) {
    return CodecStatus::UnexpectedMessageType;
  }
  if (ReadU32(frame.data() + kPayloadSizeOffset) != Size - kHeaderSize) {
    return CodecStatus::InvalidPayloadSize;
  }
  return CodecStatus::Ok;
}

bool IsAllZero(const std::uint8_t* begin, const std::uint8_t* end) noexcept {
  return std::all_of(begin, end,
                     [](std::uint8_t value) { return value == 0; });
}

bool IsKnown(ProjectSymbolType type) noexcept {
  return type >= ProjectSymbolType::Class && type <= ProjectSymbolType::Term;
}

bool IsValidCandidate(const ProjectCandidateEntry& candidate) noexcept {
  ProjectDictionaryEntry entry;
  entry.symbol = candidate.symbol;
  entry.symbol_type = candidate.symbol_type;
  entry.frequency = candidate.frequency;
  entry.source = ProjectSymbolSource::LanguageServer;
  return IsKnown(candidate.symbol_type) &&
         IsValidProjectDictionaryEntry(entry);
}

int HexValue(char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

bool WriteProjectId(const std::string& value, std::uint8_t* output) noexcept {
  if (!IsValidProjectId(value)) return false;
  for (std::size_t index = 0; index < kProjectIdBytes; ++index) {
    const int high = HexValue(value[index * 2]);
    const int low = HexValue(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return true;
}

char HexDigit(std::uint8_t value) noexcept {
  return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

std::string ReadProjectId(const std::uint8_t* input) {
  std::string value;
  value.reserve(kProjectIdBytes * 2);
  for (std::size_t index = 0; index < kProjectIdBytes; ++index) {
    value.push_back(HexDigit(input[index] >> 4u));
    value.push_back(HexDigit(input[index] & 0x0fu));
  }
  return value;
}

}  // namespace

CodecStatus EncodeQuery(const ProjectCandidateQuery& query,
                        QueryFrame& frame) noexcept {
  frame.fill(0);
  WriteHeader(frame, MessageType::Query, query.request_id);
  return CodecStatus::Ok;
}

CodecStatus DecodeQuery(const QueryFrame& frame,
                        ProjectCandidateQuery& query) noexcept {
  const CodecStatus header = ValidateHeader(frame, MessageType::Query);
  if (header != CodecStatus::Ok) return header;
  if (!IsAllZero(frame.data() + kHeaderSize, frame.data() + frame.size())) {
    return CodecStatus::NonZeroReserved;
  }
  query.request_id = ReadRequestId(frame);
  return CodecStatus::Ok;
}

CodecStatus EncodeResponse(const ProjectCandidateResponse& response,
                           ResponseFrame& frame) noexcept {
  frame.fill(0);
  if ((!response.active &&
       (response.ttl_ms != 0 || !response.project_id.empty() ||
        !response.candidates.empty())) ||
      (response.active &&
       (response.ttl_ms == 0 || response.ttl_ms > kActiveProjectLeaseMs ||
        !IsValidProjectId(response.project_id))) ||
      response.candidates.size() > kMaximumActiveProjectCandidates) {
    return CodecStatus::InvalidField;
  }

  WriteHeader(frame, MessageType::Snapshot, response.request_id);
  WriteU64(frame.data() + kGenerationOffset, response.generation);
  frame[kActiveOffset] = response.active ? 1u : 0u;
  WriteU16(frame.data() + kCountOffset,
           static_cast<std::uint16_t>(response.candidates.size()));
  WriteU32(frame.data() + kTtlOffset, response.ttl_ms);
  if (response.active &&
      !WriteProjectId(response.project_id,
                      frame.data() + kProjectIdOffset)) {
    frame.fill(0);
    return CodecStatus::InvalidField;
  }

  std::size_t offset = kRecordsOffset;
  std::size_t symbol_bytes = 0;
  for (const auto& candidate : response.candidates) {
    symbol_bytes += candidate.symbol.size();
    if (!IsValidCandidate(candidate) ||
        symbol_bytes > kMaximumActiveProjectSymbolBytes ||
        offset + kRecordHeaderSize + candidate.symbol.size() > frame.size()) {
      frame.fill(0);
      return CodecStatus::InvalidField;
    }
    frame[offset] = static_cast<std::uint8_t>(candidate.symbol_type);
    WriteU16(frame.data() + offset + 2,
             static_cast<std::uint16_t>(candidate.symbol.size()));
    WriteU32(frame.data() + offset + 4, candidate.frequency);
    for (std::size_t index = 0; index < candidate.symbol.size(); ++index) {
      frame[offset + kRecordHeaderSize + index] =
          static_cast<std::uint8_t>(candidate.symbol[index]);
    }
    offset += kRecordHeaderSize + candidate.symbol.size();
  }
  return CodecStatus::Ok;
}

CodecStatus DecodeResponse(const ResponseFrame& frame,
                           ProjectCandidateResponse& response) noexcept {
  const CodecStatus header = ValidateHeader(frame, MessageType::Snapshot);
  if (header != CodecStatus::Ok) return header;
  const std::uint8_t active = frame[kActiveOffset];
  const std::size_t count = ReadU16(frame.data() + kCountOffset);
  const std::uint32_t ttl_ms = ReadU32(frame.data() + kTtlOffset);
  if (active > 1u || count > kMaximumActiveProjectCandidates ||
      frame[kHeaderSize + 9] != 0 ||
      (active == 0u &&
       (count != 0 || ttl_ms != 0 ||
        !IsAllZero(frame.data() + kProjectIdOffset,
                   frame.data() + kProjectIdOffset + kProjectIdBytes))) ||
      (active == 1u &&
       (ttl_ms == 0 || ttl_ms > kActiveProjectLeaseMs))) {
    return CodecStatus::InvalidField;
  }

  try {
    ProjectCandidateResponse decoded;
    decoded.request_id = ReadU32(frame.data() + kRequestIdOffset);
    decoded.generation = ReadU64(frame.data() + kGenerationOffset);
    decoded.active = active == 1u;
    decoded.ttl_ms = ttl_ms;
    if (decoded.active) {
      decoded.project_id = ReadProjectId(frame.data() + kProjectIdOffset);
      if (!IsValidProjectId(decoded.project_id)) {
        return CodecStatus::InvalidField;
      }
    }
    decoded.candidates.reserve(count);
    std::size_t offset = kRecordsOffset;
    std::size_t symbol_bytes = 0;
    for (std::size_t index = 0; index < count; ++index) {
      if (offset + kRecordHeaderSize > frame.size() ||
          frame[offset + 1] != 0) {
        return CodecStatus::InvalidField;
      }
      ProjectCandidateEntry candidate;
      candidate.symbol_type = static_cast<ProjectSymbolType>(frame[offset]);
      const std::size_t symbol_size = ReadU16(frame.data() + offset + 2);
      candidate.frequency = ReadU32(frame.data() + offset + 4);
      symbol_bytes += symbol_size;
      if (symbol_size == 0 || symbol_size > kMaximumProjectSymbolBytes ||
          symbol_bytes > kMaximumActiveProjectSymbolBytes ||
          offset + kRecordHeaderSize + symbol_size > frame.size()) {
        return CodecStatus::InvalidField;
      }
      const auto* begin = frame.data() + offset + kRecordHeaderSize;
      candidate.symbol.assign(reinterpret_cast<const char*>(begin),
                              symbol_size);
      if (!IsValidCandidate(candidate)) return CodecStatus::InvalidField;
      decoded.candidates.push_back(std::move(candidate));
      offset += kRecordHeaderSize + symbol_size;
    }
    if (!IsAllZero(frame.data() + offset, frame.data() + frame.size())) {
      return CodecStatus::NonZeroReserved;
    }
    response = std::move(decoded);
    return CodecStatus::Ok;
  } catch (...) {
    return CodecStatus::AllocationFailure;
  }
}

std::uint32_t ReadRequestId(const QueryFrame& frame) noexcept {
  return ReadU32(frame.data() + kRequestIdOffset);
}

const char* ToString(CodecStatus status) noexcept {
  switch (status) {
    case CodecStatus::Ok: return "OK";
    case CodecStatus::BadMagic: return "BAD_MAGIC";
    case CodecStatus::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case CodecStatus::UnexpectedMessageType:
      return "UNEXPECTED_MESSAGE_TYPE";
    case CodecStatus::InvalidPayloadSize: return "INVALID_PAYLOAD_SIZE";
    case CodecStatus::InvalidField: return "INVALID_FIELD";
    case CodecStatus::NonZeroReserved: return "NON_ZERO_RESERVED";
    case CodecStatus::AllocationFailure: return "ALLOCATION_FAILURE";
  }
  return "UNKNOWN";
}

}  // namespace contextime::candidate_protocol
