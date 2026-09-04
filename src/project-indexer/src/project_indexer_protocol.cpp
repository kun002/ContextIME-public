#include "contextime/project_indexer_protocol.h"

#include <algorithm>
#include <utility>

namespace contextime::project_protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'C', 'I', 'P', 'D'};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kMessageTypeOffset = 6;
constexpr std::size_t kPayloadSizeOffset = 8;
constexpr std::size_t kRequestIdOffset = 12;
constexpr std::size_t kOperationOffset = kHeaderSize;
constexpr std::size_t kRecordCountOffset = kHeaderSize + 1;
constexpr std::size_t kProjectIdOffset = kHeaderSize + 4;
constexpr std::size_t kRecordsOffset = kHeaderSize + 32;
constexpr std::size_t kRecordHeaderSize = 8;

static_assert(static_cast<std::uint8_t>(ProjectSymbolType::Class) == 0);
static_assert(static_cast<std::uint8_t>(ProjectSymbolType::Term) == 9);
static_assert(static_cast<std::uint8_t>(ProjectSymbolSource::LanguageServer) ==
              0);
static_assert(static_cast<std::uint8_t>(ProjectSymbolSource::Manual) == 4);

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

template <std::size_t Size>
void WriteHeader(std::array<std::uint8_t, Size>& frame, MessageType type,
                 std::uint32_t payload_size,
                 std::uint32_t request_id) noexcept {
  std::copy(kMagic.begin(), kMagic.end(), frame.begin());
  WriteU16(frame.data() + kVersionOffset, kProtocolVersion);
  WriteU16(frame.data() + kMessageTypeOffset,
           static_cast<std::uint16_t>(type));
  WriteU32(frame.data() + kPayloadSizeOffset, payload_size);
  WriteU32(frame.data() + kRequestIdOffset, request_id);
}

template <std::size_t Size>
CodecStatus ValidateHeader(const std::array<std::uint8_t, Size>& frame,
                           MessageType expected_type,
                           std::uint32_t expected_payload_size) noexcept {
  if (!std::equal(kMagic.begin(), kMagic.end(), frame.begin())) {
    return CodecStatus::BadMagic;
  }
  if (ReadU16(frame.data() + kVersionOffset) != kProtocolVersion) {
    return CodecStatus::UnsupportedVersion;
  }
  if (ReadU16(frame.data() + kMessageTypeOffset) !=
      static_cast<std::uint16_t>(expected_type)) {
    return CodecStatus::UnexpectedMessageType;
  }
  if (ReadU32(frame.data() + kPayloadSizeOffset) != expected_payload_size) {
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

bool IsKnown(ProjectSymbolSource source) noexcept {
  return source >= ProjectSymbolSource::LanguageServer &&
         source <= ProjectSymbolSource::Manual;
}

char HexDigit(std::uint8_t value) noexcept {
  return static_cast<char>(value < 10u ? '0' + value : 'a' + value - 10u);
}

}  // namespace

CodecStatus EncodeUpsertRequest(const UpsertRequest& request,
                                RequestFrame& frame) noexcept {
  frame.fill(0);
  const bool upsert = request.operation == RequestOperation::Upsert;
  const bool activate =
      request.operation == RequestOperation::ActivateProject;
  const bool deactivate =
      request.operation == RequestOperation::DeactivateProject;
  if ((!upsert && !activate && !deactivate) ||
      (upsert && (request.entries.empty() ||
                  request.entries.size() > kMaximumRecordsPerRequest)) ||
      (!upsert && !request.entries.empty()) ||
      (deactivate && !IsAllZero(request.project_id.data(),
                                request.project_id.data() +
                                    request.project_id.size()))) {
    return CodecStatus::InvalidField;
  }
  WriteHeader(frame, MessageType::UpsertRequest,
              static_cast<std::uint32_t>(kRequestPayloadSize),
              request.request_id);
  frame[kOperationOffset] = static_cast<std::uint8_t>(request.operation);
  frame[kRecordCountOffset] =
      static_cast<std::uint8_t>(request.entries.size());
  std::copy(request.project_id.begin(), request.project_id.end(),
            frame.begin() + kProjectIdOffset);

  std::size_t offset = kRecordsOffset;
  for (const auto& entry : request.entries) {
    if (!IsValidProjectDictionaryEntry(entry) || !IsKnown(entry.symbol_type) ||
        !IsKnown(entry.source) ||
        offset + kRecordHeaderSize + entry.symbol.size() > frame.size()) {
      frame.fill(0);
      return CodecStatus::InvalidField;
    }
    frame[offset] = static_cast<std::uint8_t>(entry.symbol_type);
    frame[offset + 1] = static_cast<std::uint8_t>(entry.source);
    WriteU16(frame.data() + offset + 2,
             static_cast<std::uint16_t>(entry.symbol.size()));
    WriteU32(frame.data() + offset + 4, entry.frequency);
    for (std::size_t index = 0; index < entry.symbol.size(); ++index) {
      frame[offset + kRecordHeaderSize + index] =
          static_cast<std::uint8_t>(entry.symbol[index]);
    }
    offset += kRecordHeaderSize + entry.symbol.size();
  }
  return CodecStatus::Ok;
}

CodecStatus DecodeUpsertRequest(const RequestFrame& frame,
                                UpsertRequest& request) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::UpsertRequest,
      static_cast<std::uint32_t>(kRequestPayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }
  const std::size_t count = frame[kRecordCountOffset];
  const auto operation =
      static_cast<RequestOperation>(frame[kOperationOffset]);
  const bool upsert = operation == RequestOperation::Upsert;
  const bool activate = operation == RequestOperation::ActivateProject;
  const bool deactivate = operation == RequestOperation::DeactivateProject;
  if ((!upsert && !activate && !deactivate) ||
      (upsert && (count == 0 || count > kMaximumRecordsPerRequest)) ||
      (!upsert && count != 0)) {
    return CodecStatus::InvalidField;
  }
  if (!IsAllZero(frame.data() + kHeaderSize + 2,
                 frame.data() + kHeaderSize + 4) ||
      !IsAllZero(frame.data() + kHeaderSize + 20,
                 frame.data() + kHeaderSize + 32)) {
    return CodecStatus::NonZeroReserved;
  }

  try {
    UpsertRequest decoded;
    decoded.request_id = ReadRequestId(frame);
    decoded.operation = operation;
    std::copy(frame.begin() + kProjectIdOffset,
              frame.begin() + kProjectIdOffset + kProjectIdBytes,
              decoded.project_id.begin());
    if (deactivate &&
        !IsAllZero(decoded.project_id.data(),
                   decoded.project_id.data() + decoded.project_id.size())) {
      return CodecStatus::InvalidField;
    }
    decoded.entries.reserve(count);
    std::size_t offset = kRecordsOffset;
    for (std::size_t index = 0; index < count; ++index) {
      if (offset + kRecordHeaderSize > frame.size()) {
        return CodecStatus::InvalidField;
      }
      ProjectDictionaryEntry entry;
      entry.symbol_type = static_cast<ProjectSymbolType>(frame[offset]);
      entry.source = static_cast<ProjectSymbolSource>(frame[offset + 1]);
      const std::size_t symbol_size = ReadU16(frame.data() + offset + 2);
      entry.frequency = ReadU32(frame.data() + offset + 4);
      if (symbol_size == 0 || symbol_size > kMaximumProjectSymbolBytes ||
          offset + kRecordHeaderSize + symbol_size > frame.size()) {
        return CodecStatus::InvalidField;
      }
      const auto* symbol_begin = frame.data() + offset + kRecordHeaderSize;
      entry.symbol.assign(reinterpret_cast<const char*>(symbol_begin),
                          symbol_size);
      if (!IsKnown(entry.symbol_type) || !IsKnown(entry.source) ||
          !IsValidProjectDictionaryEntry(entry)) {
        return CodecStatus::InvalidField;
      }
      decoded.entries.push_back(std::move(entry));
      offset += kRecordHeaderSize + symbol_size;
    }
    if (!IsAllZero(frame.data() + offset, frame.data() + frame.size())) {
      return CodecStatus::NonZeroReserved;
    }
    request = std::move(decoded);
    return CodecStatus::Ok;
  } catch (...) {
    return CodecStatus::AllocationFailure;
  }
}

CodecStatus EncodeUpsertResponse(const UpsertResponse& response,
                                 ResponseFrame& frame) noexcept {
  frame.fill(0);
  const auto status = static_cast<std::uint8_t>(response.status);
  if (status > static_cast<std::uint8_t>(ResponseStatus::InternalError) ||
      ((response.status == ResponseStatus::Ok) != response.accepted) ||
      (response.accepted &&
       (response.status != ResponseStatus::Ok ||
        response.accepted_count > kMaximumRecordsPerRequest)) ||
      (!response.accepted && response.accepted_count != 0)) {
    return CodecStatus::InvalidField;
  }
  WriteHeader(frame, MessageType::UpsertResponse,
              static_cast<std::uint32_t>(kResponsePayloadSize),
              response.request_id);
  frame[kHeaderSize] = status;
  frame[kHeaderSize + 1] = response.accepted ? 1u : 0u;
  WriteU16(frame.data() + kHeaderSize + 2, response.accepted_count);
  return CodecStatus::Ok;
}

CodecStatus DecodeUpsertResponse(const ResponseFrame& frame,
                                 UpsertResponse& response) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::UpsertResponse,
      static_cast<std::uint32_t>(kResponsePayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }
  const auto status = static_cast<ResponseStatus>(frame[kHeaderSize]);
  const std::uint8_t accepted = frame[kHeaderSize + 1];
  const std::uint16_t accepted_count = ReadU16(frame.data() + kHeaderSize + 2);
  if (status > ResponseStatus::InternalError || accepted > 1u ||
      ((status == ResponseStatus::Ok) != (accepted == 1u)) ||
      (accepted == 1u &&
       (status != ResponseStatus::Ok ||
        accepted_count > kMaximumRecordsPerRequest)) ||
      (accepted == 0u && accepted_count != 0) ||
      !IsAllZero(frame.data() + kHeaderSize + 4,
                 frame.data() + frame.size())) {
    return CodecStatus::InvalidField;
  }
  response.request_id = ReadU32(frame.data() + kRequestIdOffset);
  response.status = status;
  response.accepted = accepted == 1u;
  response.accepted_count = accepted_count;
  return CodecStatus::Ok;
}

std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept {
  return ReadU32(frame.data() + kRequestIdOffset);
}

std::string ProjectIdToString(const ProjectId& project_id) {
  std::string value;
  value.reserve(project_id.size() * 2u);
  for (const std::uint8_t byte : project_id) {
    value.push_back(HexDigit(byte >> 4u));
    value.push_back(HexDigit(byte & 0x0fu));
  }
  return value;
}

const char* ToString(ResponseStatus status) noexcept {
  switch (status) {
    case ResponseStatus::Ok: return "OK";
    case ResponseStatus::MalformedRequest: return "MALFORMED_REQUEST";
    case ResponseStatus::StoreError: return "STORE_ERROR";
    case ResponseStatus::InternalError: return "INTERNAL_ERROR";
  }
  return "UNKNOWN";
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

}  // namespace contextime::project_protocol
