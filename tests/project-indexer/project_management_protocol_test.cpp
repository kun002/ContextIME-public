#include "contextime/project_management_protocol.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using contextime::ProjectDictionaryEntry;
using contextime::ProjectDictionaryStatus;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;
using contextime::management_protocol::CodecStatus;
using contextime::management_protocol::ManagementRequest;
using contextime::management_protocol::ManagementResponse;
using contextime::management_protocol::Operation;
using contextime::management_protocol::ResponseStatus;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

contextime::management_protocol::ProjectId ProjectId(std::uint8_t seed) {
  contextime::management_protocol::ProjectId project_id{};
  for (std::size_t index = 0; index < project_id.size(); ++index) {
    project_id[index] = static_cast<std::uint8_t>(seed + index);
  }
  return project_id;
}

ProjectDictionaryEntry Entry(std::string symbol, ProjectSymbolType type,
                             std::uint32_t frequency,
                             std::uint64_t last_seen_ms) {
  ProjectDictionaryEntry entry;
  entry.symbol = std::move(symbol);
  entry.symbol_type = type;
  entry.frequency = frequency;
  entry.last_seen_ms = last_seen_ms;
  entry.source = ProjectSymbolSource::LanguageServer;
  return entry;
}

void TestRequestOperationsAndWireLayout() {
  ManagementRequest list;
  list.request_id = 0x10203040u;
  list.operation = Operation::ListProjects;
  list.page_size = 200;
  list.cursor = 5;
  contextime::management_protocol::RequestFrame frame;
  Expect(contextime::management_protocol::EncodeRequest(list, frame) ==
             CodecStatus::Ok,
         "list request encodes");
  Expect(frame.size() == 4096 &&
             std::string(frame.begin(), frame.begin() + 4) == "CIPM",
         "management request has an isolated fixed frame");
  Expect(frame[16] == 1 && frame[17] == 0 && frame[18] == 200 &&
             frame[19] == 0 && frame[36] == 5,
         "list pagination has stable wire offsets");
  ManagementRequest decoded;
  Expect(contextime::management_protocol::DecodeRequest(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.request_id == list.request_id &&
             decoded.operation == Operation::ListProjects &&
             decoded.page_size == 200 && decoded.cursor == 5,
         "list request round-trips");

  ManagementRequest view;
  view.request_id = 8;
  view.operation = Operation::ViewProject;
  view.project_id = ProjectId(1);
  view.page_size = 100;
  view.cursor = 200;
  Expect(contextime::management_protocol::EncodeRequest(view, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeRequest(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.project_id == view.project_id && decoded.cursor == 200,
         "view request round-trips a project and entry cursor");

  ManagementRequest enabled;
  enabled.request_id = 9;
  enabled.operation = Operation::SetEnabled;
  enabled.project_id = ProjectId(2);
  enabled.enabled = true;
  Expect(contextime::management_protocol::EncodeRequest(enabled, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeRequest(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.enabled,
         "enable request round-trips");

  ManagementRequest remove;
  remove.request_id = 10;
  remove.operation = Operation::RemoveProject;
  remove.project_id = ProjectId(3);
  Expect(contextime::management_protocol::EncodeRequest(remove, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeRequest(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.operation == Operation::RemoveProject,
         "remove request round-trips");
}

void TestMalformedRequestsAreRejected() {
  ManagementRequest request;
  request.operation = Operation::ListProjects;
  request.page_size = 10;
  contextime::management_protocol::RequestFrame valid;
  contextime::management_protocol::EncodeRequest(request, valid);
  struct Fixture {
    const char* name;
    std::size_t offset;
    std::uint8_t value;
    CodecStatus status;
  };
  const Fixture fixtures[] = {
      {"magic", 0, 0, CodecStatus::BadMagic},
      {"version", 4, 2, CodecStatus::UnsupportedVersion},
      {"type", 6, 2, CodecStatus::UnexpectedMessageType},
      {"payload", 8, 0, CodecStatus::InvalidPayloadSize},
      {"operation", 16, 99, CodecStatus::InvalidField},
      {"boolean", 17, 2, CodecStatus::InvalidField},
      {"reserved tail", 40, 1, CodecStatus::NonZeroReserved},
  };
  for (const auto& fixture : fixtures) {
    auto frame = valid;
    frame[fixture.offset] = fixture.value;
    ManagementRequest decoded;
    Expect(contextime::management_protocol::DecodeRequest(frame, decoded) ==
               fixture.status,
           fixture.name);
  }

  request.page_size = 0;
  Expect(contextime::management_protocol::EncodeRequest(request, valid) ==
             CodecStatus::InvalidField,
         "list rejects zero page size");
  request.page_size = static_cast<std::uint16_t>(
      contextime::management_protocol::kMaximumProjectsPerPage + 1);
  Expect(contextime::management_protocol::EncodeRequest(request, valid) ==
             CodecStatus::InvalidField,
         "list rejects an unbounded page");
  request.page_size = 10;
  request.project_id[0] = 1;
  Expect(contextime::management_protocol::EncodeRequest(request, valid) ==
             CodecStatus::InvalidField,
         "list cannot smuggle a project ID");
}

void TestListAndViewResponses() {
  ManagementResponse list;
  list.request_id = 21;
  list.operation = Operation::ListProjects;
  list.total_count = 3;
  list.next_cursor = 2;
  list.projects = {ProjectId(1), ProjectId(20)};
  contextime::management_protocol::ResponseFrame frame;
  Expect(contextime::management_protocol::EncodeResponse(list, frame) ==
             CodecStatus::Ok,
         "project list response encodes");
  Expect(frame.size() == 32768 &&
             std::string(frame.begin(), frame.begin() + 4) == "CIPM" &&
             frame[18] == 2 && frame[24] == 2,
         "project list response has bounded stable layout");
  ManagementResponse decoded;
  Expect(contextime::management_protocol::DecodeResponse(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.request_id == 21 && decoded.total_count == 3 &&
             decoded.next_cursor == 2 && decoded.projects == list.projects,
         "project list response round-trips");

  ManagementResponse view;
  view.request_id = 22;
  view.operation = Operation::ViewProject;
  view.exists = true;
  view.enabled = false;
  view.total_count = 2;
  view.entries = {
      Entry("PlayerController", ProjectSymbolType::Class, 7, 123456),
      Entry("\xe7\x8e\xa9\xe5\xae\xb6", ProjectSymbolType::Term, 3, 987654),
  };
  Expect(contextime::management_protocol::EncodeResponse(view, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeResponse(frame, decoded) ==
                 CodecStatus::Ok,
         "project view response encodes and decodes");
  Expect(decoded.operation == Operation::ViewProject && decoded.exists &&
             !decoded.enabled && decoded.entries.size() == 2 &&
             decoded.entries[0].symbol == "PlayerController" &&
             decoded.entries[0].frequency == 7 &&
             decoded.entries[1].symbol == "\xe7\x8e\xa9\xe5\xae\xb6" &&
             decoded.entries[1].last_seen_ms == 987654,
         "project entry fields and UTF-8 round-trip");
}

void TestMutationAndErrorResponses() {
  ManagementResponse enabled;
  enabled.request_id = 30;
  enabled.operation = Operation::SetEnabled;
  enabled.exists = true;
  enabled.enabled = true;
  enabled.total_count = 100000;
  contextime::management_protocol::ResponseFrame frame;
  ManagementResponse decoded;
  Expect(contextime::management_protocol::EncodeResponse(enabled, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeResponse(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.enabled && decoded.total_count == 100000,
         "enable response returns resulting metadata");

  ManagementResponse error;
  error.request_id = 31;
  error.operation = Operation::ViewProject;
  error.status = ResponseStatus::StoreError;
  error.store_status = ProjectDictionaryStatus::CorruptData;
  Expect(contextime::management_protocol::EncodeResponse(error, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeResponse(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.status == ResponseStatus::StoreError &&
             decoded.store_status == ProjectDictionaryStatus::CorruptData,
         "store failure preserves a bounded diagnostic status");

  frame[frame.size() - 1] = 1;
  Expect(contextime::management_protocol::DecodeResponse(frame, decoded) ==
             CodecStatus::InvalidField,
         "response rejects nonzero trailing bytes");
}

void TestProjectIdConversionAndNames() {
  contextime::management_protocol::ProjectId project_id{};
  Expect(contextime::management_protocol::ProjectIdFromString(
             "00112233445566778899aabbccddeeff", project_id) &&
             contextime::project_protocol::ProjectIdToString(project_id) ==
                 "00112233445566778899aabbccddeeff",
         "canonical project ID converts both ways");
  Expect(!contextime::management_protocol::ProjectIdFromString(
             "00112233445566778899AABBCCDDEEFF", project_id),
         "uppercase project ID is rejected");
  Expect(std::string(contextime::management_protocol::ToString(
             Operation::RemoveProject)) == "REMOVE_PROJECT" &&
             std::string(contextime::management_protocol::ToString(
                 ResponseStatus::StoreError)) == "STORE_ERROR" &&
             std::string(contextime::management_protocol::ToString(
                 CodecStatus::NonZeroReserved)) == "NON_ZERO_RESERVED",
         "management protocol stable names");
}

void TestTermRequestsRoundTripAndValidate() {
  ManagementRequest upsert;
  upsert.request_id = 40;
  upsert.operation = Operation::UpsertTerm;
  upsert.project_id = ProjectId(4);
  upsert.entry.symbol = "PhotonSlash";
  upsert.entry.symbol_type = ProjectSymbolType::Term;
  upsert.entry.source = ProjectSymbolSource::Manual;
  upsert.entry.frequency = 1;
  contextime::management_protocol::RequestFrame frame;
  Expect(contextime::management_protocol::EncodeRequest(upsert, frame) ==
             CodecStatus::Ok,
         "term upsert request encodes");
  Expect(frame[40] == 11 && frame[41] == 0 && frame[42] == 9 &&
             frame[43] == 4 && frame[44] == 'P',
         "term upsert entry has a stable bounded request layout");
  ManagementRequest decoded;
  Expect(contextime::management_protocol::DecodeRequest(frame, decoded) ==
             CodecStatus::Ok &&
             decoded.operation == Operation::UpsertTerm &&
             decoded.entry.symbol == "PhotonSlash" &&
             decoded.entry.symbol_type == ProjectSymbolType::Term &&
             decoded.entry.source == ProjectSymbolSource::Manual &&
             decoded.entry.frequency == 1,
         "term upsert request round-trips");

  ManagementRequest utf8 = upsert;
  utf8.request_id = 41;
  utf8.entry.symbol = "\xe6\x9c\xaf\xe8\xaf\xad";
  Expect(contextime::management_protocol::EncodeRequest(utf8, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeRequest(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.entry.symbol == "\xe6\x9c\xaf\xe8\xaf\xad",
         "UTF-8 term round-trips");

  ManagementRequest bad = upsert;
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::Ok,
         "baseline term request encodes");
  bad.project_id.fill(0);
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert rejects a zero project id");
  bad = upsert;
  bad.entry.symbol_type = ProjectSymbolType::Class;
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert is locked to the term type");
  bad = upsert;
  bad.entry.source = ProjectSymbolSource::LanguageServer;
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert is locked to the manual source");
  bad = upsert;
  bad.entry.symbol.clear();
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert rejects an empty symbol");
  bad = upsert;
  bad.entry.symbol = "Assets/Secret.txt";
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert rejects path separators");
  bad = upsert;
  bad.entry.symbol.assign(128, 'a');
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::Ok,
         "term upsert accepts a 128-byte symbol");
  bad.entry.symbol.assign(129, 'a');
  Expect(contextime::management_protocol::EncodeRequest(bad, frame) ==
             CodecStatus::InvalidField,
         "term upsert rejects a 129-byte symbol");

  ManagementRequest remove_entry;
  remove_entry.request_id = 42;
  remove_entry.operation = Operation::RemoveEntry;
  remove_entry.project_id = ProjectId(5);
  remove_entry.entry.symbol = "PlayerController";
  remove_entry.entry.symbol_type = ProjectSymbolType::Class;
  remove_entry.entry.source = ProjectSymbolSource::LanguageServer;
  remove_entry.entry.frequency = 1;
  Expect(contextime::management_protocol::EncodeRequest(remove_entry, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeRequest(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.operation == Operation::RemoveEntry &&
             decoded.entry.symbol == "PlayerController" &&
             decoded.entry.symbol_type == ProjectSymbolType::Class &&
             decoded.entry.source == ProjectSymbolSource::LanguageServer,
         "entry removal round-trips any keyed type and source");
  frame[44 + remove_entry.entry.symbol.size()] = 1;
  Expect(contextime::management_protocol::DecodeRequest(frame, decoded) ==
             CodecStatus::NonZeroReserved,
         "entry removal rejects nonzero bytes after the symbol");
}

void TestTermResponseShape() {
  ManagementResponse added;
  added.request_id = 50;
  added.operation = Operation::UpsertTerm;
  added.exists = true;
  added.enabled = true;
  added.total_count = 3;
  contextime::management_protocol::ResponseFrame frame;
  ManagementResponse decoded;
  Expect(contextime::management_protocol::EncodeResponse(added, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeResponse(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.exists && decoded.enabled &&
             decoded.total_count == 3,
         "term upsert response returns resulting metadata");
  added.entries = {Entry("PlayerController", ProjectSymbolType::Class, 7, 1)};
  Expect(contextime::management_protocol::EncodeResponse(added, frame) ==
             CodecStatus::InvalidField,
         "term upsert response cannot carry entries");

  ManagementResponse removed;
  removed.request_id = 51;
  removed.operation = Operation::RemoveEntry;
  removed.exists = true;
  removed.enabled = false;
  removed.total_count = 2;
  Expect(contextime::management_protocol::EncodeResponse(removed, frame) ==
             CodecStatus::Ok &&
             contextime::management_protocol::DecodeResponse(frame, decoded) ==
                 CodecStatus::Ok &&
             decoded.operation == Operation::RemoveEntry &&
             decoded.total_count == 2,
         "entry removal response round-trips");
  Expect(std::string(contextime::management_protocol::ToString(
             Operation::UpsertTerm)) == "UPSERT_TERM" &&
             std::string(contextime::management_protocol::ToString(
                 Operation::RemoveEntry)) == "REMOVE_ENTRY",
         "term operations have stable diagnostic names");
}

}  // namespace

int main() {
  TestRequestOperationsAndWireLayout();
  TestMalformedRequestsAreRejected();
  TestTermRequestsRoundTripAndValidate();
  TestListAndViewResponses();
  TestMutationAndErrorResponses();
  TestTermResponseShape();
  TestProjectIdConversionAndNames();
  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Management Protocol: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
