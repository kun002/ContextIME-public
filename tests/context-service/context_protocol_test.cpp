#include "contextime/context_protocol.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using contextime::ContextSnapshot;
using contextime::DecisionSource;
using contextime::DesiredMode;
using contextime::EditorSurface;
using contextime::EditorSyntax;
using contextime::InputMode;
using contextime::OptionalMode;
using contextime::protocol::CodecStatus;
using contextime::protocol::EditorContextRequest;
using contextime::protocol::EditorContextRequestFrame;
using contextime::protocol::EditorContextResponse;
using contextime::protocol::EditorContextResponseFrame;
using contextime::protocol::EvaluateRequest;
using contextime::protocol::EvaluateRequestFrame;
using contextime::protocol::EvaluateResponse;
using contextime::protocol::EvaluateResponseFrame;
using contextime::protocol::ResponseStatus;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void ExpectCodec(CodecStatus actual, CodecStatus expected,
                 const char* message) {
  Expect(actual == expected, message);
}

bool OptionalEqual(OptionalMode left, OptionalMode right) {
  return left.has_value == right.has_value &&
         (!left.has_value || left.value == right.value);
}

ContextSnapshot FullSnapshot() {
  ContextSnapshot snapshot;
  snapshot.current_mode = InputMode::English;
  snapshot.composition_active = true;
  snapshot.candidate_visible = true;
  snapshot.explicit_user_lock = OptionalMode::Some(InputMode::Chinese);
  snapshot.now_ms = 0x0102030405060708ull;
  snapshot.manual_override_until_ms = 0x1112131415161718ull;
  snapshot.automation_enabled = false;
  snapshot.context_available = false;
  snapshot.user_rule = OptionalMode::Some(InputMode::English);
  snapshot.project_rule = OptionalMode::Some(InputMode::Chinese);
  snapshot.syntax_context = OptionalMode::Some(InputMode::English);
  snapshot.surface_context = OptionalMode::Some(InputMode::Chinese);
  snapshot.application_default = OptionalMode::Some(InputMode::English);
  return snapshot;
}

EditorContextRequest FullEditorContextRequest() {
  EditorContextRequest request;
  request.request_id = 0x40302010u;
  request.update.window_focused = true;
  request.update.surface = EditorSurface::Editor;
  request.update.syntax = EditorSyntax::Comment;
  const std::string language = "typescript";
  std::copy(language.begin(), language.end(),
            request.update.language_id.begin());
  return request;
}

void TestRequestRoundTrip() {
  EvaluateRequest input{0x78563412u, FullSnapshot()};
  EvaluateRequestFrame frame;
  ExpectCodec(contextime::protocol::EncodeEvaluateRequest(input, frame),
              CodecStatus::Ok, "encode full request");
  Expect(frame[0] == 'C' && frame[1] == 'I' && frame[2] == 'M' &&
             frame[3] == 'E',
         "request magic is CIME");
  Expect(frame[4] == 1 && frame[5] == 0,
         "request protocol version is little endian v1");
  Expect(frame[12] == 0x12 && frame[13] == 0x34 && frame[14] == 0x56 &&
             frame[15] == 0x78,
         "request id is explicitly little endian");
  Expect(contextime::protocol::ReadRequestId(frame) == input.request_id,
         "fixed header request id reader");

  EvaluateRequest output;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, output),
              CodecStatus::Ok, "decode full request");
  Expect(output.request_id == input.request_id, "request id round trip");
  Expect(output.snapshot.current_mode == input.snapshot.current_mode,
         "current mode round trip");
  Expect(output.snapshot.composition_active, "composition flag round trip");
  Expect(output.snapshot.candidate_visible, "candidate flag round trip");
  Expect(!output.snapshot.automation_enabled,
         "automation flag round trip");
  Expect(!output.snapshot.context_available,
         "context availability round trip");
  Expect(OptionalEqual(output.snapshot.explicit_user_lock,
                       input.snapshot.explicit_user_lock),
         "explicit lock round trip");
  Expect(OptionalEqual(output.snapshot.user_rule, input.snapshot.user_rule),
         "user rule round trip");
  Expect(OptionalEqual(output.snapshot.project_rule,
                       input.snapshot.project_rule),
         "project rule round trip");
  Expect(OptionalEqual(output.snapshot.syntax_context,
                       input.snapshot.syntax_context),
         "syntax context round trip");
  Expect(OptionalEqual(output.snapshot.surface_context,
                       input.snapshot.surface_context),
         "surface context round trip");
  Expect(OptionalEqual(output.snapshot.application_default,
                       input.snapshot.application_default),
         "application default round trip");
  Expect(output.snapshot.now_ms == input.snapshot.now_ms,
         "monotonic time round trip");
  Expect(output.snapshot.manual_override_until_ms ==
             input.snapshot.manual_override_until_ms,
         "manual override deadline round trip");
}

void TestRequestValidation() {
  EvaluateRequestFrame valid;
  ExpectCodec(contextime::protocol::EncodeEvaluateRequest(
                  EvaluateRequest{9, FullSnapshot()}, valid),
              CodecStatus::Ok, "prepare valid request");
  EvaluateRequest decoded;

  auto frame = valid;
  frame[0] = 'X';
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::BadMagic, "reject request magic");

  frame = valid;
  frame[4] = 2;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::UnsupportedVersion,
              "reject unsupported request version");

  frame = valid;
  frame[6] = 2;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::UnexpectedMessageType,
              "reject response in request decoder");

  frame = valid;
  frame[8] = 31;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::InvalidPayloadSize,
              "reject request payload size");

  frame = valid;
  frame[16] = 3;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::InvalidField, "reject current mode");

  frame = valid;
  frame[17] = 0x80;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::InvalidField, "reject unknown request flags");

  frame = valid;
  frame[18] = 3;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::InvalidField, "reject optional mode");

  frame = valid;
  frame[40] = 1;
  ExpectCodec(contextime::protocol::DecodeEvaluateRequest(frame, decoded),
              CodecStatus::NonZeroReserved,
              "reject non-zero request reserved bytes");

  ContextSnapshot invalid = FullSnapshot();
  invalid.current_mode = static_cast<InputMode>(99);
  ExpectCodec(contextime::protocol::EncodeEvaluateRequest(
                  EvaluateRequest{10, invalid}, frame),
              CodecStatus::InvalidField, "reject invalid request enum");
}

void TestResponseRoundTripAndValidation() {
  EvaluateResponse input;
  input.request_id = 0x11223344u;
  input.status = ResponseStatus::Ok;
  input.decision = {DesiredMode::English, InputMode::English,
                    DecisionSource::SyntaxContext, true};
  EvaluateResponseFrame valid;
  ExpectCodec(contextime::protocol::EncodeEvaluateResponse(input, valid),
              CodecStatus::Ok, "encode response");

  EvaluateResponse output;
  ExpectCodec(contextime::protocol::DecodeEvaluateResponse(valid, output),
              CodecStatus::Ok, "decode response");
  Expect(output.request_id == input.request_id, "response id round trip");
  Expect(output.status == input.status, "response status round trip");
  Expect(output.decision.desired_mode == input.decision.desired_mode,
         "desired mode round trip");
  Expect(output.decision.target_mode == input.decision.target_mode,
         "target mode round trip");
  Expect(output.decision.source == input.decision.source,
         "decision source round trip");
  Expect(output.decision.should_switch, "switch flag round trip");

  auto frame = valid;
  frame[16] = 9;
  ExpectCodec(contextime::protocol::DecodeEvaluateResponse(frame, output),
              CodecStatus::InvalidField, "reject response status");
  frame = valid;
  frame[17] = 3;
  ExpectCodec(contextime::protocol::DecodeEvaluateResponse(frame, output),
              CodecStatus::InvalidField, "reject desired mode");
  frame = valid;
  frame[20] = 2;
  ExpectCodec(contextime::protocol::DecodeEvaluateResponse(frame, output),
              CodecStatus::InvalidField, "reject switch boolean");
  frame = valid;
  frame[21] = 1;
  ExpectCodec(contextime::protocol::DecodeEvaluateResponse(frame, output),
              CodecStatus::NonZeroReserved,
              "reject non-zero response reserved bytes");

  input.status = static_cast<ResponseStatus>(99);
  ExpectCodec(contextime::protocol::EncodeEvaluateResponse(input, frame),
              CodecStatus::InvalidField, "reject invalid response enum");
}

void TestEditorContextRequestRoundTrip() {
  const EditorContextRequest input = FullEditorContextRequest();
  EditorContextRequestFrame frame;
  ExpectCodec(contextime::protocol::EncodeEditorContextRequest(input, frame),
              CodecStatus::Ok, "encode editor context request");
  Expect(contextime::protocol::ReadMessageType(frame) == 3,
         "editor request message type");
  Expect(contextime::protocol::ReadRequestId(frame) == input.request_id,
         "editor request id header");
  Expect(frame[19] == 10, "editor language length is explicit");

  EditorContextRequest output;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, output),
              CodecStatus::Ok, "decode editor context request");
  Expect(output.request_id == input.request_id,
         "editor request id round trip");
  Expect(output.update.window_focused, "editor focused flag round trip");
  Expect(output.update.surface == EditorSurface::Editor,
         "editor surface round trip");
  Expect(output.update.syntax == EditorSyntax::Comment,
         "editor syntax round trip");
  Expect(std::string(output.update.language_id.data()) == "typescript",
         "editor language id round trip");
}

void TestEditorContextRequestValidation() {
  EditorContextRequestFrame valid;
  ExpectCodec(contextime::protocol::EncodeEditorContextRequest(
                  FullEditorContextRequest(), valid),
              CodecStatus::Ok, "prepare editor context request");
  EditorContextRequest decoded;

  auto frame = valid;
  frame[6] = 1;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::UnexpectedMessageType,
              "reject evaluate frame in editor decoder");

  frame = valid;
  frame[16] = 0x80;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::InvalidField, "reject editor flags");

  frame = valid;
  frame[17] = 9;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::InvalidField, "reject editor surface");

  frame = valid;
  frame[18] = 9;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::InvalidField, "reject editor syntax");

  frame = valid;
  frame[19] = 21;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::InvalidField, "reject editor language length");

  frame = valid;
  frame[20] = 'T';
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::InvalidField,
              "reject non-normalized editor language id");

  frame = valid;
  frame[35] = 1;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::NonZeroReserved,
              "reject non-zero unused language bytes");

  frame = valid;
  frame[40] = 1;
  ExpectCodec(contextime::protocol::DecodeEditorContextRequest(frame, decoded),
              CodecStatus::NonZeroReserved,
              "reject non-zero editor reserved bytes");

  EditorContextRequest invalid = FullEditorContextRequest();
  invalid.update.syntax = static_cast<EditorSyntax>(99);
  ExpectCodec(contextime::protocol::EncodeEditorContextRequest(invalid, frame),
              CodecStatus::InvalidField, "reject invalid editor enum");

  invalid = FullEditorContextRequest();
  invalid.update.language_id.fill('a');
  ExpectCodec(contextime::protocol::EncodeEditorContextRequest(invalid, frame),
              CodecStatus::InvalidField,
              "reject unterminated editor language id");
}

void TestEditorContextResponseRoundTripAndValidation() {
  EditorContextResponse input;
  input.request_id = 0x10203040u;
  input.status = ResponseStatus::Ok;
  input.accepted = true;
  EditorContextResponseFrame frame;
  ExpectCodec(contextime::protocol::EncodeEditorContextResponse(input, frame),
              CodecStatus::Ok, "encode editor context response");
  Expect(contextime::protocol::ReadMessageType(frame) == 4,
         "editor response message type");

  EditorContextResponse output;
  ExpectCodec(contextime::protocol::DecodeEditorContextResponse(frame, output),
              CodecStatus::Ok, "decode editor context response");
  Expect(output.request_id == input.request_id,
         "editor response id round trip");
  Expect(output.status == ResponseStatus::Ok,
         "editor response status round trip");
  Expect(output.accepted, "editor response accepted round trip");

  auto invalid = frame;
  invalid[17] = 2;
  ExpectCodec(contextime::protocol::DecodeEditorContextResponse(invalid,
                                                                 output),
              CodecStatus::InvalidField,
              "reject invalid editor accepted boolean");
  invalid = frame;
  invalid[18] = 1;
  ExpectCodec(contextime::protocol::DecodeEditorContextResponse(invalid,
                                                                 output),
              CodecStatus::NonZeroReserved,
              "reject editor response reserved bytes");
}

void TestStableNamesAndSizes() {
  static_assert(contextime::protocol::kEvaluateRequestFrameSize == 48,
                "request wire size changed");
  static_assert(contextime::protocol::kEvaluateResponseFrameSize == 32,
                "response wire size changed");
  static_assert(contextime::protocol::kResponseAcknowledgement == 0x06,
                "response acknowledgement changed");
  static_assert(contextime::protocol::kEditorContextRequestFrameSize == 48,
                "editor request wire size changed");
  static_assert(contextime::protocol::kEditorContextResponseFrameSize == 32,
                "editor response wire size changed");
  Expect(std::string(contextime::protocol::ToString(ResponseStatus::Ok)) ==
             "OK",
         "response status stable name");
  Expect(std::string(contextime::protocol::ToString(
             ResponseStatus::UnsupportedVersion)) == "UNSUPPORTED_VERSION",
         "unsupported version stable name");
  Expect(std::string(contextime::protocol::ToString(
             CodecStatus::NonZeroReserved)) == "NON_ZERO_RESERVED",
         "codec status stable name");
}

}  // namespace

int main() {
  TestRequestRoundTrip();
  TestRequestValidation();
  TestResponseRoundTripAndValidation();
  TestEditorContextRequestRoundTrip();
  TestEditorContextRequestValidation();
  TestEditorContextResponseRoundTripAndValidation();
  TestStableNamesAndSizes();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Context Protocol: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
