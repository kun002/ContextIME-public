#include "contextime/project_management_protocol.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace contextime::management_protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'C', 'I', 'P', 'M'};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kMessageTypeOffset = 6;
constexpr std::size_t kPayloadSizeOffset = 8;
constexpr std::size_t kRequestIdOffset = 12;
constexpr std::size_t kOperationOffset = 16;
constexpr std::size_t kEnabledOffset = 17;
constexpr std::size_t kPageSizeOffset = 18;
constexpr std::size_t kProjectIdOffset = 20;
constexpr std::size_t kCursorOffset = 36;
constexpr std::size_t kRequestDataOffset = 40;
constexpr std::size_t kResponseCountOffset = 18;
constexpr std::size_t kTotalCountOffset = 20;
constexpr std::size_t kNextCursorOffset = 24;
constexpr std::size_t kExistsOffset = 28;
constexpr std::size_t kResponseEnabledOffset = 29;
constexpr std::size_t kStoreStatusOffset = 30;
constexpr std::size_t kResponseDataOffset = 32;
constexpr std::size_t kEntryHeaderSize = 16;

void WriteU16(std::uint8_t* target, std::uint16_t value) noexcept {
  target[0] = static_cast<std::uint8_t>(value & 0xffu);
  target[1] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void WriteU32(std::uint8_t* target, std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < 4; ++index) {
    target[index] =
        static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}

void WriteU64(std::uint8_t* target, std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < 8; ++index) {
    target[index] =
        static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}

std::uint16_t ReadU16(const std::uint8_t* source) noexcept {
  return static_cast<std::uint16_t>(source[0]) |
         static_cast<std::uint16_t>(source[1]) << 8u;
}

std::uint32_t ReadU32(const std::uint8_t* source) noexcept {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(source[index]) << (index * 8u);
  }
  return value;
}

std::uint64_t ReadU64(const std::uint8_t* source) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(source[index]) << (index * 8u);
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

bool IsKnown(Operation operation) noexcept {
  return operation >= Operation::ListProjects &&
         operation <= Operation::RemoveProject;
}

bool IsKnown(ProjectSymbolType type) noexcept {
  return type >= ProjectSymbolType::Class && type <= ProjectSymbolType::Term;
}

bool IsKnown(ProjectSymbolSource source) noexcept {
  return source >= ProjectSymbolSource::LanguageServer &&
         source <= ProjectSymbolSource::Manual;
}

bool IsKnown(ProjectDictionaryStatus status) noexcept {
  return status >= ProjectDictionaryStatus::Ok &&
         status <= ProjectDictionaryStatus::IoError;
}

bool IsZeroProjectId(const ProjectId& project_id) noexcept {
  return IsAllZero(project_id.data(), project_id.data() + project_id.size());
}

int HexValue(char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

bool ValidateRequestFields(const ManagementRequest& request) noexcept {
  if (!IsKnown(request.operation)) return false;
  switch (request.operation) {
    case Operation::ListProjects:
      return IsZeroProjectId(request.project_id) && !request.enabled &&
             request.page_size > 0 &&
             request.page_size <= kMaximumProjectsPerPage;
    case Operation::ViewProject:
      return !request.enabled && request.page_size > 0 &&
             request.page_size <= kMaximumEntriesPerPage;
    case Operation::SetEnabled:
      return request.page_size == 0 && request.cursor == 0;
    case Operation::RemoveProject:
      return !request.enabled && request.page_size == 0 &&
             request.cursor == 0;
  }
  return false;
}

bool ValidateResponseShape(const ManagementResponse& response) noexcept {
  const auto raw_status = static_cast<std::uint8_t>(response.status);
  if (raw_status >
          static_cast<std::uint8_t>(ResponseStatus::InternalError) ||
      !IsKnown(response.operation) || !IsKnown(response.store_status) ||
      (response.status == ResponseStatus::Ok &&
       response.store_status != ProjectDictionaryStatus::Ok) ||
      (response.status == ResponseStatus::StoreError &&
       response.store_status == ProjectDictionaryStatus::Ok) ||
      (response.status != ResponseStatus::StoreError &&
       response.store_status != ProjectDictionaryStatus::Ok)) {
    return false;
  }
  if (response.status != ResponseStatus::Ok) {
    return response.projects.empty() && response.entries.empty() &&
           response.total_count == 0 &&
           response.next_cursor == kNoNextCursor && !response.exists &&
           !response.enabled;
  }
  if (response.operation == Operation::ListProjects) {
    return response.entries.empty() && !response.exists &&
           !response.enabled &&
           response.projects.size() <= kMaximumProjectsPerPage &&
           response.projects.size() <= response.total_count;
  }
  if (response.operation == Operation::ViewProject) {
    return response.projects.empty() &&
           response.entries.size() <= kMaximumEntriesPerPage &&
           response.entries.size() <= response.total_count &&
           (response.exists ||
            (response.entries.empty() && response.total_count == 0 &&
             !response.enabled));
  }
  return response.projects.empty() && response.entries.empty() &&
         response.next_cursor == kNoNextCursor;
}

}  // namespace

CodecStatus EncodeRequest(const ManagementRequest& request,
                          RequestFrame& frame) noexcept {
  frame.fill(0);
  if (!ValidateRequestFields(request)) return CodecStatus::InvalidField;
  WriteHeader(frame, MessageType::Request,
              static_cast<std::uint32_t>(kRequestPayloadSize),
              request.request_id);
  frame[kOperationOffset] = static_cast<std::uint8_t>(request.operation);
  frame[kEnabledOffset] = request.enabled ? 1u : 0u;
  WriteU16(frame.data() + kPageSizeOffset, request.page_size);
  std::copy(request.project_id.begin(), request.project_id.end(),
            frame.begin() + kProjectIdOffset);
  WriteU32(frame.data() + kCursorOffset, request.cursor);
  return CodecStatus::Ok;
}

CodecStatus DecodeRequest(const RequestFrame& frame,
                          ManagementRequest& request) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::Request,
      static_cast<std::uint32_t>(kRequestPayloadSize));
  if (header != CodecStatus::Ok) return header;
  if (!IsAllZero(frame.data() + kRequestDataOffset,
                 frame.data() + frame.size()) ||
      frame[kEnabledOffset] > 1u) {
    return frame[kEnabledOffset] > 1u ? CodecStatus::InvalidField
                                     : CodecStatus::NonZeroReserved;
  }
  try {
    ManagementRequest decoded;
    decoded.request_id = ReadRequestId(frame);
    decoded.operation = static_cast<Operation>(frame[kOperationOffset]);
    decoded.enabled = frame[kEnabledOffset] == 1u;
    decoded.page_size = ReadU16(frame.data() + kPageSizeOffset);
    std::copy(frame.begin() + kProjectIdOffset,
              frame.begin() + kProjectIdOffset + decoded.project_id.size(),
              decoded.project_id.begin());
    decoded.cursor = ReadU32(frame.data() + kCursorOffset);
    if (!ValidateRequestFields(decoded)) return CodecStatus::InvalidField;
    request = std::move(decoded);
    return CodecStatus::Ok;
  } catch (...) {
    return CodecStatus::AllocationFailure;
  }
}

CodecStatus EncodeResponse(const ManagementResponse& response,
                           ResponseFrame& frame) noexcept {
  frame.fill(0);
  if (!ValidateResponseShape(response)) return CodecStatus::InvalidField;
  const std::size_t count = response.operation == Operation::ListProjects
                                ? response.projects.size()
                                : response.entries.size();
  if (count > (std::numeric_limits<std::uint16_t>::max)()) {
    return CodecStatus::InvalidField;
  }
  WriteHeader(frame, MessageType::Response,
              static_cast<std::uint32_t>(kResponsePayloadSize),
              response.request_id);
  frame[kOperationOffset] = static_cast<std::uint8_t>(response.status);
  frame[kEnabledOffset] = static_cast<std::uint8_t>(response.operation);
  WriteU16(frame.data() + kResponseCountOffset,
           static_cast<std::uint16_t>(count));
  WriteU32(frame.data() + kTotalCountOffset, response.total_count);
  WriteU32(frame.data() + kNextCursorOffset, response.next_cursor);
  frame[kExistsOffset] = response.exists ? 1u : 0u;
  frame[kResponseEnabledOffset] = response.enabled ? 1u : 0u;
  frame[kStoreStatusOffset] =
      static_cast<std::uint8_t>(response.store_status);

  std::size_t offset = kResponseDataOffset;
  if (response.operation == Operation::ListProjects) {
    for (const auto& project_id : response.projects) {
      if (offset + project_id.size() > frame.size()) {
        frame.fill(0);
        return CodecStatus::InvalidField;
      }
      std::copy(project_id.begin(), project_id.end(), frame.begin() + offset);
      offset += project_id.size();
    }
  } else if (response.operation == Operation::ViewProject) {
    for (const auto& entry : response.entries) {
      if (!IsKnown(entry.symbol_type) || !IsKnown(entry.source) ||
          !IsValidProjectDictionaryEntry(entry) ||
          offset + kEntryHeaderSize + entry.symbol.size() > frame.size()) {
        frame.fill(0);
        return CodecStatus::InvalidField;
      }
      frame[offset] = static_cast<std::uint8_t>(entry.symbol_type);
      frame[offset + 1] = static_cast<std::uint8_t>(entry.source);
      WriteU16(frame.data() + offset + 2,
               static_cast<std::uint16_t>(entry.symbol.size()));
      WriteU32(frame.data() + offset + 4, entry.frequency);
      WriteU64(frame.data() + offset + 8, entry.last_seen_ms);
      std::copy(entry.symbol.begin(), entry.symbol.end(),
                frame.begin() + offset + kEntryHeaderSize);
      offset += kEntryHeaderSize + entry.symbol.size();
    }
  }
  return CodecStatus::Ok;
}

CodecStatus DecodeResponse(const ResponseFrame& frame,
                           ManagementResponse& response) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::Response,
      static_cast<std::uint32_t>(kResponsePayloadSize));
  if (header != CodecStatus::Ok) return header;
  if (frame[kExistsOffset] > 1u || frame[kResponseEnabledOffset] > 1u ||
      frame[31] != 0) {
    return CodecStatus::InvalidField;
  }
  try {
    ManagementResponse decoded;
    decoded.request_id = ReadU32(frame.data() + kRequestIdOffset);
    decoded.status = static_cast<ResponseStatus>(frame[kOperationOffset]);
    decoded.operation = static_cast<Operation>(frame[kEnabledOffset]);
    const std::size_t count = ReadU16(frame.data() + kResponseCountOffset);
    decoded.total_count = ReadU32(frame.data() + kTotalCountOffset);
    decoded.next_cursor = ReadU32(frame.data() + kNextCursorOffset);
    decoded.exists = frame[kExistsOffset] == 1u;
    decoded.enabled = frame[kResponseEnabledOffset] == 1u;
    decoded.store_status =
        static_cast<ProjectDictionaryStatus>(frame[kStoreStatusOffset]);

    std::size_t offset = kResponseDataOffset;
    if (decoded.operation == Operation::ListProjects) {
      if (count > kMaximumProjectsPerPage) return CodecStatus::InvalidField;
      decoded.projects.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        if (offset + ProjectId{}.size() > frame.size()) {
          return CodecStatus::InvalidField;
        }
        ProjectId project_id{};
        std::copy(frame.begin() + offset,
                  frame.begin() + offset + project_id.size(),
                  project_id.begin());
        decoded.projects.push_back(project_id);
        offset += project_id.size();
      }
    } else if (decoded.operation == Operation::ViewProject) {
      if (count > kMaximumEntriesPerPage) return CodecStatus::InvalidField;
      decoded.entries.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        if (offset + kEntryHeaderSize > frame.size()) {
          return CodecStatus::InvalidField;
        }
        ProjectDictionaryEntry entry;
        entry.symbol_type = static_cast<ProjectSymbolType>(frame[offset]);
        entry.source = static_cast<ProjectSymbolSource>(frame[offset + 1]);
        const std::size_t symbol_size = ReadU16(frame.data() + offset + 2);
        entry.frequency = ReadU32(frame.data() + offset + 4);
        entry.last_seen_ms = ReadU64(frame.data() + offset + 8);
        if (symbol_size == 0 || symbol_size > kMaximumProjectSymbolBytes ||
            offset + kEntryHeaderSize + symbol_size > frame.size()) {
          return CodecStatus::InvalidField;
        }
        const auto* symbol = frame.data() + offset + kEntryHeaderSize;
        entry.symbol.assign(reinterpret_cast<const char*>(symbol), symbol_size);
        if (!IsKnown(entry.symbol_type) || !IsKnown(entry.source) ||
            !IsValidProjectDictionaryEntry(entry)) {
          return CodecStatus::InvalidField;
        }
        decoded.entries.push_back(std::move(entry));
        offset += kEntryHeaderSize + symbol_size;
      }
    } else if (count != 0) {
      return CodecStatus::InvalidField;
    }
    if (!IsAllZero(frame.data() + offset, frame.data() + frame.size()) ||
        !ValidateResponseShape(decoded)) {
      return CodecStatus::InvalidField;
    }
    response = std::move(decoded);
    return CodecStatus::Ok;
  } catch (...) {
    return CodecStatus::AllocationFailure;
  }
}

bool HasManagementMagic(const RequestFrame& frame) noexcept {
  return std::equal(kMagic.begin(), kMagic.end(), frame.begin());
}

std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept {
  return ReadU32(frame.data() + kRequestIdOffset);
}

bool ProjectIdFromString(const std::string& value,
                         ProjectId& project_id) noexcept {
  project_id.fill(0);
  if (!IsValidProjectId(value)) return false;
  for (std::size_t index = 0; index < project_id.size(); ++index) {
    const int high = HexValue(value[index * 2]);
    const int low = HexValue(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    project_id[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return true;
}

const char* ToString(Operation operation) noexcept {
  switch (operation) {
    case Operation::ListProjects: return "LIST_PROJECTS";
    case Operation::ViewProject: return "VIEW_PROJECT";
    case Operation::SetEnabled: return "SET_ENABLED";
    case Operation::RemoveProject: return "REMOVE_PROJECT";
  }
  return "UNKNOWN";
}

const char* ToString(ResponseStatus status) noexcept {
  switch (status) {
    case ResponseStatus::Ok: return "OK";
    case ResponseStatus::MalformedRequest: return "MALFORMED_REQUEST";
    case ResponseStatus::NotFound: return "NOT_FOUND";
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

}  // namespace contextime::management_protocol
