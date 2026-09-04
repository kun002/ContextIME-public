#include "contextime/context_protocol.h"

#include <algorithm>

namespace contextime::protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'C', 'I', 'M', 'E'};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kMessageTypeOffset = 6;
constexpr std::size_t kPayloadSizeOffset = 8;
constexpr std::size_t kRequestIdOffset = 12;
constexpr std::uint8_t kKnownRequestFlags = 0x0f;
constexpr std::uint8_t kKnownEditorContextFlags = 0x01;
constexpr std::size_t kEditorLanguageWireCapacity =
    kEditorLanguageIdCapacity - 1;

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

bool ToWire(InputMode mode, std::uint8_t& wire) noexcept {
  switch (mode) {
    case InputMode::Chinese:
      wire = 1;
      return true;
    case InputMode::English:
      wire = 2;
      return true;
  }
  return false;
}

bool FromWire(std::uint8_t wire, InputMode& mode) noexcept {
  switch (wire) {
    case 1:
      mode = InputMode::Chinese;
      return true;
    case 2:
      mode = InputMode::English;
      return true;
    default:
      return false;
  }
}

bool OptionalToWire(OptionalMode mode, std::uint8_t& wire) noexcept {
  if (!mode.has_value) {
    wire = 0;
    return true;
  }
  return ToWire(mode.value, wire);
}

bool OptionalFromWire(std::uint8_t wire, OptionalMode& mode) noexcept {
  if (wire == 0) {
    mode = OptionalMode::None();
    return true;
  }
  InputMode value;
  if (!FromWire(wire, value)) {
    return false;
  }
  mode = OptionalMode::Some(value);
  return true;
}

bool DesiredToWire(DesiredMode mode, std::uint8_t& wire) noexcept {
  switch (mode) {
    case DesiredMode::Keep:
      wire = 0;
      return true;
    case DesiredMode::Chinese:
      wire = 1;
      return true;
    case DesiredMode::English:
      wire = 2;
      return true;
  }
  return false;
}

bool DesiredFromWire(std::uint8_t wire, DesiredMode& mode) noexcept {
  switch (wire) {
    case 0:
      mode = DesiredMode::Keep;
      return true;
    case 1:
      mode = DesiredMode::Chinese;
      return true;
    case 2:
      mode = DesiredMode::English;
      return true;
    default:
      return false;
  }
}

bool SourceToWire(DecisionSource source, std::uint8_t& wire) noexcept {
  const auto value = static_cast<std::uint8_t>(source);
  if (value > static_cast<std::uint8_t>(DecisionSource::FallbackKeep)) {
    return false;
  }
  wire = static_cast<std::uint8_t>(value + 1u);
  return true;
}

bool SourceFromWire(std::uint8_t wire, DecisionSource& source) noexcept {
  if (wire == 0 ||
      wire > static_cast<std::uint8_t>(DecisionSource::FallbackKeep) + 1u) {
    return false;
  }
  source = static_cast<DecisionSource>(wire - 1u);
  return true;
}

bool EditorSurfaceToWire(EditorSurface surface, std::uint8_t& wire) noexcept {
  switch (surface) {
    case EditorSurface::Unknown:
      wire = 0;
      return true;
    case EditorSurface::Editor:
      wire = 1;
      return true;
    case EditorSurface::IntegratedTerminal:
      wire = 2;
      return true;
  }
  return false;
}

bool EditorSurfaceFromWire(std::uint8_t wire,
                           EditorSurface& surface) noexcept {
  switch (wire) {
    case 0:
      surface = EditorSurface::Unknown;
      return true;
    case 1:
      surface = EditorSurface::Editor;
      return true;
    case 2:
      surface = EditorSurface::IntegratedTerminal;
      return true;
    default:
      return false;
  }
}

bool EditorSyntaxToWire(EditorSyntax syntax, std::uint8_t& wire) noexcept {
  const auto value = static_cast<std::uint8_t>(syntax);
  if (value > static_cast<std::uint8_t>(EditorSyntax::MarkdownCode)) {
    return false;
  }
  wire = value;
  return true;
}

bool EditorSyntaxFromWire(std::uint8_t wire,
                          EditorSyntax& syntax) noexcept {
  if (wire > static_cast<std::uint8_t>(EditorSyntax::MarkdownCode)) {
    return false;
  }
  syntax = static_cast<EditorSyntax>(wire);
  return true;
}

bool IsLanguageCharacter(char value) noexcept {
  return (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == '-' || value == '_' ||
         value == '.' || value == '+';
}

bool ReadLanguageLength(
    const std::array<char, kEditorLanguageIdCapacity>& language,
    std::size_t& length) noexcept {
  length = 0;
  while (length < language.size() && language[length] != '\0') {
    if (!IsLanguageCharacter(language[length])) {
      return false;
    }
    ++length;
  }
  return length < language.size() && length <= kEditorLanguageWireCapacity;
}

bool IsAllZero(const std::uint8_t* begin, const std::uint8_t* end) noexcept {
  return std::all_of(begin, end,
                     [](std::uint8_t value) { return value == 0; });
}

}  // namespace

CodecStatus EncodeEvaluateRequest(const EvaluateRequest& request,
                                  EvaluateRequestFrame& frame) noexcept {
  frame.fill(0);
  WriteHeader(frame, MessageType::EvaluateRequest,
              static_cast<std::uint32_t>(kEvaluateRequestPayloadSize),
              request.request_id);

  std::uint8_t current_mode = 0;
  std::uint8_t explicit_lock = 0;
  std::uint8_t user_rule = 0;
  std::uint8_t project_rule = 0;
  std::uint8_t syntax_context = 0;
  std::uint8_t surface_context = 0;
  std::uint8_t application_default = 0;
  if (!ToWire(request.snapshot.current_mode, current_mode) ||
      !OptionalToWire(request.snapshot.explicit_user_lock, explicit_lock) ||
      !OptionalToWire(request.snapshot.user_rule, user_rule) ||
      !OptionalToWire(request.snapshot.project_rule, project_rule) ||
      !OptionalToWire(request.snapshot.syntax_context, syntax_context) ||
      !OptionalToWire(request.snapshot.surface_context, surface_context) ||
      !OptionalToWire(request.snapshot.application_default,
                      application_default)) {
    return CodecStatus::InvalidField;
  }

  auto* payload = frame.data() + kHeaderSize;
  payload[0] = current_mode;
  payload[1] = static_cast<std::uint8_t>(
      (request.snapshot.composition_active ? 0x01u : 0u) |
      (request.snapshot.candidate_visible ? 0x02u : 0u) |
      (request.snapshot.automation_enabled ? 0x04u : 0u) |
      (request.snapshot.context_available ? 0x08u : 0u));
  payload[2] = explicit_lock;
  payload[3] = user_rule;
  payload[4] = project_rule;
  payload[5] = syntax_context;
  payload[6] = surface_context;
  payload[7] = application_default;
  WriteU64(payload + 8, request.snapshot.now_ms);
  WriteU64(payload + 16, request.snapshot.manual_override_until_ms);
  return CodecStatus::Ok;
}

CodecStatus DecodeEvaluateRequest(const EvaluateRequestFrame& frame,
                                  EvaluateRequest& request) noexcept {
  const CodecStatus header =
      ValidateHeader(frame, MessageType::EvaluateRequest,
                     static_cast<std::uint32_t>(kEvaluateRequestPayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }

  const auto* payload = frame.data() + kHeaderSize;
  if ((payload[1] & ~kKnownRequestFlags) != 0u) {
    return CodecStatus::InvalidField;
  }
  if (!IsAllZero(payload + 24, payload + kEvaluateRequestPayloadSize)) {
    return CodecStatus::NonZeroReserved;
  }

  ContextSnapshot snapshot;
  if (!FromWire(payload[0], snapshot.current_mode) ||
      !OptionalFromWire(payload[2], snapshot.explicit_user_lock) ||
      !OptionalFromWire(payload[3], snapshot.user_rule) ||
      !OptionalFromWire(payload[4], snapshot.project_rule) ||
      !OptionalFromWire(payload[5], snapshot.syntax_context) ||
      !OptionalFromWire(payload[6], snapshot.surface_context) ||
      !OptionalFromWire(payload[7], snapshot.application_default)) {
    return CodecStatus::InvalidField;
  }

  snapshot.composition_active = (payload[1] & 0x01u) != 0;
  snapshot.candidate_visible = (payload[1] & 0x02u) != 0;
  snapshot.automation_enabled = (payload[1] & 0x04u) != 0;
  snapshot.context_available = (payload[1] & 0x08u) != 0;
  snapshot.now_ms = ReadU64(payload + 8);
  snapshot.manual_override_until_ms = ReadU64(payload + 16);

  request.request_id = ReadU32(frame.data() + kRequestIdOffset);
  request.snapshot = snapshot;
  return CodecStatus::Ok;
}

CodecStatus EncodeEvaluateResponse(const EvaluateResponse& response,
                                   EvaluateResponseFrame& frame) noexcept {
  frame.fill(0);
  WriteHeader(frame, MessageType::EvaluateResponse,
              static_cast<std::uint32_t>(kEvaluateResponsePayloadSize),
              response.request_id);

  const auto response_status = static_cast<std::uint8_t>(response.status);
  if (response_status >
      static_cast<std::uint8_t>(ResponseStatus::InternalError)) {
    return CodecStatus::InvalidField;
  }

  std::uint8_t desired_mode = 0;
  std::uint8_t target_mode = 0;
  std::uint8_t source = 0;
  if (!DesiredToWire(response.decision.desired_mode, desired_mode) ||
      !ToWire(response.decision.target_mode, target_mode) ||
      !SourceToWire(response.decision.source, source)) {
    return CodecStatus::InvalidField;
  }

  auto* payload = frame.data() + kHeaderSize;
  payload[0] = response_status;
  payload[1] = desired_mode;
  payload[2] = target_mode;
  payload[3] = source;
  payload[4] = response.decision.should_switch ? 1u : 0u;
  return CodecStatus::Ok;
}

CodecStatus DecodeEvaluateResponse(const EvaluateResponseFrame& frame,
                                   EvaluateResponse& response) noexcept {
  const CodecStatus header =
      ValidateHeader(frame, MessageType::EvaluateResponse,
                     static_cast<std::uint32_t>(kEvaluateResponsePayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }

  const auto* payload = frame.data() + kHeaderSize;
  if (payload[0] > static_cast<std::uint8_t>(ResponseStatus::InternalError) ||
      payload[4] > 1u) {
    return CodecStatus::InvalidField;
  }
  if (!IsAllZero(payload + 5, payload + kEvaluateResponsePayloadSize)) {
    return CodecStatus::NonZeroReserved;
  }

  Decision decision;
  if (!DesiredFromWire(payload[1], decision.desired_mode) ||
      !FromWire(payload[2], decision.target_mode) ||
      !SourceFromWire(payload[3], decision.source)) {
    return CodecStatus::InvalidField;
  }
  decision.should_switch = payload[4] != 0;

  response.request_id = ReadU32(frame.data() + kRequestIdOffset);
  response.status = static_cast<ResponseStatus>(payload[0]);
  response.decision = decision;
  return CodecStatus::Ok;
}

CodecStatus EncodeEditorContextRequest(
    const EditorContextRequest& request,
    EditorContextRequestFrame& frame) noexcept {
  frame.fill(0);
  WriteHeader(frame, MessageType::EditorContextRequest,
              static_cast<std::uint32_t>(kEditorContextRequestPayloadSize),
              request.request_id);

  std::uint8_t surface = 0;
  std::uint8_t syntax = 0;
  std::size_t language_length = 0;
  if (!EditorSurfaceToWire(request.update.surface, surface) ||
      !EditorSyntaxToWire(request.update.syntax, syntax) ||
      !ReadLanguageLength(request.update.language_id, language_length)) {
    return CodecStatus::InvalidField;
  }

  auto* payload = frame.data() + kHeaderSize;
  payload[0] = request.update.window_focused ? 0x01u : 0u;
  payload[1] = surface;
  payload[2] = syntax;
  payload[3] = static_cast<std::uint8_t>(language_length);
  for (std::size_t index = 0; index < language_length; ++index) {
    payload[4 + index] =
        static_cast<std::uint8_t>(request.update.language_id[index]);
  }
  return CodecStatus::Ok;
}

CodecStatus DecodeEditorContextRequest(
    const EditorContextRequestFrame& frame,
    EditorContextRequest& request) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::EditorContextRequest,
      static_cast<std::uint32_t>(kEditorContextRequestPayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }

  const auto* payload = frame.data() + kHeaderSize;
  const std::size_t language_length = payload[3];
  if ((payload[0] & ~kKnownEditorContextFlags) != 0u ||
      language_length > kEditorLanguageWireCapacity) {
    return CodecStatus::InvalidField;
  }
  if (!IsAllZero(payload + 4 + language_length,
                 payload + 4 + kEditorLanguageWireCapacity) ||
      !IsAllZero(payload + 24, payload + kEditorContextRequestPayloadSize)) {
    return CodecStatus::NonZeroReserved;
  }

  EditorContextUpdate update;
  if (!EditorSurfaceFromWire(payload[1], update.surface) ||
      !EditorSyntaxFromWire(payload[2], update.syntax)) {
    return CodecStatus::InvalidField;
  }
  for (std::size_t index = 0; index < language_length; ++index) {
    const char value = static_cast<char>(payload[4 + index]);
    if (!IsLanguageCharacter(value)) {
      return CodecStatus::InvalidField;
    }
    update.language_id[index] = value;
  }
  update.window_focused = (payload[0] & 0x01u) != 0;

  request.request_id = ReadU32(frame.data() + kRequestIdOffset);
  request.update = update;
  return CodecStatus::Ok;
}

CodecStatus EncodeEditorContextResponse(
    const EditorContextResponse& response,
    EditorContextResponseFrame& frame) noexcept {
  frame.fill(0);
  WriteHeader(frame, MessageType::EditorContextResponse,
              static_cast<std::uint32_t>(kEditorContextResponsePayloadSize),
              response.request_id);
  const auto response_status = static_cast<std::uint8_t>(response.status);
  if (response_status >
      static_cast<std::uint8_t>(ResponseStatus::InternalError)) {
    return CodecStatus::InvalidField;
  }
  auto* payload = frame.data() + kHeaderSize;
  payload[0] = response_status;
  payload[1] = response.accepted ? 1u : 0u;
  return CodecStatus::Ok;
}

CodecStatus DecodeEditorContextResponse(
    const EditorContextResponseFrame& frame,
    EditorContextResponse& response) noexcept {
  const CodecStatus header = ValidateHeader(
      frame, MessageType::EditorContextResponse,
      static_cast<std::uint32_t>(kEditorContextResponsePayloadSize));
  if (header != CodecStatus::Ok) {
    return header;
  }
  const auto* payload = frame.data() + kHeaderSize;
  if (payload[0] > static_cast<std::uint8_t>(ResponseStatus::InternalError) ||
      payload[1] > 1u) {
    return CodecStatus::InvalidField;
  }
  if (!IsAllZero(payload + 2,
                 payload + kEditorContextResponsePayloadSize)) {
    return CodecStatus::NonZeroReserved;
  }
  response.request_id = ReadU32(frame.data() + kRequestIdOffset);
  response.status = static_cast<ResponseStatus>(payload[0]);
  response.accepted = payload[1] != 0;
  return CodecStatus::Ok;
}

std::uint16_t ReadMessageType(const RequestFrame& frame) noexcept {
  return ReadU16(frame.data() + kMessageTypeOffset);
}

std::uint16_t ReadMessageType(const ResponseFrame& frame) noexcept {
  return ReadU16(frame.data() + kMessageTypeOffset);
}

std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept {
  return ReadU32(frame.data() + kRequestIdOffset);
}

const char* ToString(ResponseStatus status) noexcept {
  switch (status) {
    case ResponseStatus::Ok:
      return "OK";
    case ResponseStatus::UnsupportedVersion:
      return "UNSUPPORTED_VERSION";
    case ResponseStatus::MalformedRequest:
      return "MALFORMED_REQUEST";
    case ResponseStatus::InternalError:
      return "INTERNAL_ERROR";
  }
  return "UNKNOWN";
}

const char* ToString(CodecStatus status) noexcept {
  switch (status) {
    case CodecStatus::Ok:
      return "OK";
    case CodecStatus::BadMagic:
      return "BAD_MAGIC";
    case CodecStatus::UnsupportedVersion:
      return "UNSUPPORTED_VERSION";
    case CodecStatus::UnexpectedMessageType:
      return "UNEXPECTED_MESSAGE_TYPE";
    case CodecStatus::InvalidPayloadSize:
      return "INVALID_PAYLOAD_SIZE";
    case CodecStatus::InvalidField:
      return "INVALID_FIELD";
    case CodecStatus::NonZeroReserved:
      return "NON_ZERO_RESERVED";
  }
  return "UNKNOWN";
}

}  // namespace contextime::protocol
