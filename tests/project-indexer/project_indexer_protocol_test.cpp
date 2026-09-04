#include "contextime/project_indexer_protocol.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using contextime::ProjectDictionaryEntry;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;
using contextime::project_protocol::CodecStatus;
using contextime::project_protocol::RequestOperation;
using contextime::project_protocol::ResponseStatus;
using contextime::project_protocol::UpsertRequest;
using contextime::project_protocol::UpsertResponse;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

ProjectDictionaryEntry Entry(std::string symbol, ProjectSymbolType type,
                             std::uint32_t frequency) {
  ProjectDictionaryEntry entry;
  entry.symbol = std::move(symbol);
  entry.symbol_type = type;
  entry.frequency = frequency;
  entry.source = ProjectSymbolSource::LanguageServer;
  return entry;
}

UpsertRequest Request() {
  UpsertRequest request;
  request.request_id = 0x10203040u;
  for (std::size_t index = 0; index < request.project_id.size(); ++index) {
    request.project_id[index] = static_cast<std::uint8_t>(index);
  }
  request.entries = {
      Entry("PlayerController", ProjectSymbolType::Class, 2),
      Entry("SpawnPlayer", ProjectSymbolType::Method, 1),
      Entry("\xe7\x8e\xa9\xe5\xae\xb6", ProjectSymbolType::Term, 3),
  };
  return request;
}

void TestRequestRoundTripAndWireLayout() {
  const UpsertRequest request = Request();
  contextime::project_protocol::RequestFrame frame;
  Expect(contextime::project_protocol::EncodeUpsertRequest(request, frame) ==
             CodecStatus::Ok,
         "project request encodes");
  Expect(frame.size() == 4096, "project request remains fixed at 4096 bytes");
  Expect(std::string(frame.begin(), frame.begin() + 4) == "CIPD",
         "project request magic");
  Expect(frame[4] == 1 && frame[5] == 0, "project protocol version");
  Expect(frame[6] == 1 && frame[7] == 0, "project request type");
  Expect(frame[16] == 1 && frame[17] == 3,
         "upsert operation and record count");
  Expect(frame[48] == 0 && frame[49] == 0,
         "class and language-server wire values");
  Expect(frame[50] == 16 && frame[51] == 0,
         "first symbol byte length");

  UpsertRequest decoded;
  Expect(contextime::project_protocol::DecodeUpsertRequest(frame, decoded) ==
             CodecStatus::Ok,
         "project request decodes");
  Expect(decoded.request_id == request.request_id, "request id round-trips");
  Expect(decoded.project_id == request.project_id, "project id round-trips");
  Expect(decoded.entries.size() == 3, "record count round-trips");
  Expect(decoded.entries[0].symbol == "PlayerController" &&
             decoded.entries[0].symbol_type == ProjectSymbolType::Class &&
             decoded.entries[0].frequency == 2 &&
             decoded.entries[0].source ==
                 ProjectSymbolSource::LanguageServer,
         "first symbol fields round-trip");
  Expect(decoded.entries[2].symbol == "\xe7\x8e\xa9\xe5\xae\xb6",
         "UTF-8 symbol round-trips");
  Expect(contextime::project_protocol::ProjectIdToString(request.project_id) ==
             "000102030405060708090a0b0c0d0e0f",
         "project id converts to canonical storage key");
}

void TestMalformedRequestsAreRejected() {
  contextime::project_protocol::RequestFrame valid;
  Expect(contextime::project_protocol::EncodeUpsertRequest(Request(), valid) ==
             CodecStatus::Ok,
         "malformed fixtures start valid");
  struct Fixture {
    const char* name;
    std::size_t offset;
    std::uint8_t value;
    CodecStatus expected;
  };
  const Fixture fixtures[] = {
      {"magic", 0, 0, CodecStatus::BadMagic},
      {"version", 4, 2, CodecStatus::UnsupportedVersion},
      {"type", 6, 2, CodecStatus::UnexpectedMessageType},
      {"payload", 8, 0, CodecStatus::InvalidPayloadSize},
      {"operation", 16, 2, CodecStatus::InvalidField},
      {"count", 17, 0, CodecStatus::InvalidField},
      {"header reserved", 18, 1, CodecStatus::NonZeroReserved},
      {"payload reserved", 36, 1, CodecStatus::NonZeroReserved},
      {"symbol type", 48, 99, CodecStatus::InvalidField},
      {"symbol source", 49, 99, CodecStatus::InvalidField},
      {"symbol length", 50, 0, CodecStatus::InvalidField},
      {"frequency", 52, 0, CodecStatus::InvalidField},
      {"tail", valid.size() - 1, 1, CodecStatus::NonZeroReserved},
  };
  for (const auto& fixture : fixtures) {
    auto frame = valid;
    frame[fixture.offset] = fixture.value;
    UpsertRequest decoded;
    Expect(contextime::project_protocol::DecodeUpsertRequest(frame, decoded) ==
               fixture.expected,
           fixture.name);
  }

  UpsertRequest empty;
  contextime::project_protocol::RequestFrame ignored;
  Expect(contextime::project_protocol::EncodeUpsertRequest(empty, ignored) ==
             CodecStatus::InvalidField,
         "empty request cannot encode");
  UpsertRequest unsafe = Request();
  unsafe.entries[0].symbol = "Assets/Secret";
  Expect(contextime::project_protocol::EncodeUpsertRequest(unsafe, ignored) ==
             CodecStatus::InvalidField,
         "path-like symbol cannot encode");
}

void TestActiveProjectOperations() {
  UpsertRequest activate = Request();
  activate.operation = RequestOperation::ActivateProject;
  activate.entries.clear();
  contextime::project_protocol::RequestFrame frame;
  Expect(contextime::project_protocol::EncodeUpsertRequest(activate, frame) ==
             CodecStatus::Ok && frame[16] == 2 && frame[17] == 0,
         "active project request is a zero-record bounded frame");
  UpsertRequest decoded;
  Expect(contextime::project_protocol::DecodeUpsertRequest(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.operation == RequestOperation::ActivateProject &&
             decoded.project_id == activate.project_id &&
             decoded.entries.empty(),
         "active project request round-trips");

  UpsertRequest deactivate;
  deactivate.request_id = 9;
  deactivate.operation = RequestOperation::DeactivateProject;
  Expect(contextime::project_protocol::EncodeUpsertRequest(deactivate, frame) ==
             CodecStatus::Ok && frame[16] == 3 && frame[17] == 0,
         "deactivate request is explicit and contains no project ID");
  Expect(contextime::project_protocol::DecodeUpsertRequest(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.operation == RequestOperation::DeactivateProject,
         "deactivate request round-trips");

  deactivate.project_id[0] = 1;
  Expect(contextime::project_protocol::EncodeUpsertRequest(deactivate, frame) ==
             CodecStatus::InvalidField,
         "deactivate request cannot smuggle a project ID");
}

void TestResponseRoundTripAndValidation() {
  UpsertResponse response;
  response.request_id = 77;
  response.status = ResponseStatus::Ok;
  response.accepted = true;
  response.accepted_count = 3;
  contextime::project_protocol::ResponseFrame frame;
  Expect(contextime::project_protocol::EncodeUpsertResponse(response, frame) ==
             CodecStatus::Ok,
         "accepted response encodes");
  UpsertResponse decoded;
  Expect(contextime::project_protocol::DecodeUpsertResponse(frame, decoded) ==
             CodecStatus::Ok,
         "accepted response decodes");
  Expect(decoded.request_id == 77 && decoded.status == ResponseStatus::Ok &&
             decoded.accepted && decoded.accepted_count == 3,
         "accepted response round-trips");

  response.accepted_count = 0;
  Expect(contextime::project_protocol::EncodeUpsertResponse(response, frame) ==
             CodecStatus::Ok &&
             contextime::project_protocol::DecodeUpsertResponse(
                 frame, decoded) == CodecStatus::Ok &&
             decoded.accepted && decoded.accepted_count == 0,
         "successful activation response accepts zero records");

  response.status = ResponseStatus::StoreError;
  response.accepted = false;
  response.accepted_count = 0;
  Expect(contextime::project_protocol::EncodeUpsertResponse(response, frame) ==
             CodecStatus::Ok,
         "store error response encodes");
  Expect(contextime::project_protocol::DecodeUpsertResponse(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.status == ResponseStatus::StoreError &&
             !decoded.accepted,
         "store error response round-trips");

  response.status = ResponseStatus::Ok;
  response.accepted = false;
  Expect(contextime::project_protocol::EncodeUpsertResponse(response, frame) ==
             CodecStatus::InvalidField,
         "OK response must accept records");

  response.status = ResponseStatus::MalformedRequest;
  Expect(contextime::project_protocol::EncodeUpsertResponse(response, frame) ==
             CodecStatus::Ok,
         "malformed response fixture encodes");
  frame[20] = 1;
  Expect(contextime::project_protocol::DecodeUpsertResponse(frame, decoded) ==
             CodecStatus::InvalidField,
         "response reserved bytes must be zero");
}

void TestStableNames() {
  Expect(std::string(contextime::project_protocol::ToString(
             ResponseStatus::StoreError)) == "STORE_ERROR",
         "response status stable name");
  Expect(std::string(contextime::project_protocol::ToString(
             CodecStatus::NonZeroReserved)) == "NON_ZERO_RESERVED",
         "codec status stable name");
}

}  // namespace

int main() {
  TestRequestRoundTripAndWireLayout();
  TestMalformedRequestsAreRejected();
  TestActiveProjectOperations();
  TestResponseRoundTripAndValidation();
  TestStableNames();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Indexer Protocol: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
