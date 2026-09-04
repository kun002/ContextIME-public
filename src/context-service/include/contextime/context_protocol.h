#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "contextime/context_engine.h"
#include "contextime/editor_context.h"

namespace contextime::protocol {

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEvaluateRequestPayloadSize = 32;
constexpr std::size_t kEvaluateResponsePayloadSize = 16;
constexpr std::size_t kEditorContextRequestPayloadSize = 32;
constexpr std::size_t kEditorContextResponsePayloadSize = 16;
constexpr std::size_t kEvaluateRequestFrameSize =
    kHeaderSize + kEvaluateRequestPayloadSize;
constexpr std::size_t kEvaluateResponseFrameSize =
    kHeaderSize + kEvaluateResponsePayloadSize;
constexpr std::size_t kEditorContextRequestFrameSize =
    kHeaderSize + kEditorContextRequestPayloadSize;
constexpr std::size_t kEditorContextResponseFrameSize =
    kHeaderSize + kEditorContextResponsePayloadSize;
constexpr std::uint8_t kResponseAcknowledgement = 0x06;

static_assert(kEvaluateRequestFrameSize == kEditorContextRequestFrameSize,
              "v1 request frames must retain one bounded wire size");
static_assert(kEvaluateResponseFrameSize == kEditorContextResponseFrameSize,
              "v1 response frames must retain one bounded wire size");

using RequestFrame = std::array<std::uint8_t, kEvaluateRequestFrameSize>;
using ResponseFrame = std::array<std::uint8_t, kEvaluateResponseFrameSize>;
using EvaluateRequestFrame = RequestFrame;
using EvaluateResponseFrame = ResponseFrame;
using EditorContextRequestFrame = RequestFrame;
using EditorContextResponseFrame = ResponseFrame;

enum class MessageType : std::uint16_t {
  EvaluateRequest = 1,
  EvaluateResponse = 2,
  EditorContextRequest = 3,
  EditorContextResponse = 4,
};

enum class ResponseStatus : std::uint8_t {
  Ok = 0,
  UnsupportedVersion = 1,
  MalformedRequest = 2,
  InternalError = 3,
};

enum class CodecStatus : std::uint8_t {
  Ok = 0,
  BadMagic,
  UnsupportedVersion,
  UnexpectedMessageType,
  InvalidPayloadSize,
  InvalidField,
  NonZeroReserved,
};

struct EvaluateRequest {
  std::uint32_t request_id = 0;
  ContextSnapshot snapshot;
};

struct EvaluateResponse {
  std::uint32_t request_id = 0;
  ResponseStatus status = ResponseStatus::Ok;
  Decision decision;
};

struct EditorContextRequest {
  std::uint32_t request_id = 0;
  EditorContextUpdate update;
};

struct EditorContextResponse {
  std::uint32_t request_id = 0;
  ResponseStatus status = ResponseStatus::Ok;
  bool accepted = false;
};

CodecStatus EncodeEvaluateRequest(const EvaluateRequest& request,
                                  EvaluateRequestFrame& frame) noexcept;
CodecStatus DecodeEvaluateRequest(const EvaluateRequestFrame& frame,
                                  EvaluateRequest& request) noexcept;

CodecStatus EncodeEvaluateResponse(const EvaluateResponse& response,
                                   EvaluateResponseFrame& frame) noexcept;
CodecStatus DecodeEvaluateResponse(const EvaluateResponseFrame& frame,
                                   EvaluateResponse& response) noexcept;

CodecStatus EncodeEditorContextRequest(
    const EditorContextRequest& request,
    EditorContextRequestFrame& frame) noexcept;
CodecStatus DecodeEditorContextRequest(
    const EditorContextRequestFrame& frame,
    EditorContextRequest& request) noexcept;

CodecStatus EncodeEditorContextResponse(
    const EditorContextResponse& response,
    EditorContextResponseFrame& frame) noexcept;
CodecStatus DecodeEditorContextResponse(
    const EditorContextResponseFrame& frame,
    EditorContextResponse& response) noexcept;

// The request id is in the fixed header and remains readable when the
// protocol version or payload is rejected. Servers use it only to correlate
// an error response; no other unvalidated request field is consumed.
std::uint16_t ReadMessageType(const RequestFrame& frame) noexcept;
std::uint16_t ReadMessageType(const ResponseFrame& frame) noexcept;
std::uint32_t ReadRequestId(const RequestFrame& frame) noexcept;

const char* ToString(ResponseStatus status) noexcept;
const char* ToString(CodecStatus status) noexcept;

}  // namespace contextime::protocol
