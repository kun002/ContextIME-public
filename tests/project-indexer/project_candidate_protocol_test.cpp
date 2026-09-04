#include "contextime/project_candidate_protocol.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

contextime::ProjectCandidateEntry Candidate(
    std::string symbol, contextime::ProjectSymbolType type,
    std::uint32_t frequency) {
  contextime::ProjectCandidateEntry candidate;
  candidate.symbol = std::move(symbol);
  candidate.symbol_type = type;
  candidate.frequency = frequency;
  return candidate;
}

void TestQueryAndSnapshotRoundTrip() {
  contextime::candidate_protocol::ProjectCandidateQuery query;
  query.request_id = 0x10203040u;
  contextime::candidate_protocol::QueryFrame query_frame;
  Expect(contextime::candidate_protocol::EncodeQuery(query, query_frame) ==
             contextime::candidate_protocol::CodecStatus::Ok,
         "candidate query encodes");
  contextime::candidate_protocol::ProjectCandidateQuery decoded_query;
  Expect(contextime::candidate_protocol::DecodeQuery(
             query_frame, decoded_query) ==
             contextime::candidate_protocol::CodecStatus::Ok &&
             decoded_query.request_id == query.request_id,
         "candidate query round-trips");

  contextime::candidate_protocol::ProjectCandidateResponse response;
  response.request_id = query.request_id;
  response.generation = 77;
  response.active = true;
  response.ttl_ms = 2500;
  response.project_id = "00112233445566778899aabbccddeeff";
  response.candidates = {
      Candidate("PlayerController", contextime::ProjectSymbolType::Class, 8),
      Candidate("SpawnPlayer", contextime::ProjectSymbolType::Method, 2),
  };
  contextime::candidate_protocol::ResponseFrame frame;
  Expect(contextime::candidate_protocol::EncodeResponse(response, frame) ==
             contextime::candidate_protocol::CodecStatus::Ok,
         "active candidate snapshot encodes");
  Expect(frame.size() == 32768 && frame[24] == 1 && frame[26] == 2,
         "candidate response has fixed bounded wire layout");
  contextime::candidate_protocol::ProjectCandidateResponse decoded;
  Expect(contextime::candidate_protocol::DecodeResponse(frame, decoded) ==
             contextime::candidate_protocol::CodecStatus::Ok,
         "active candidate snapshot decodes");
  Expect(decoded.request_id == response.request_id &&
             decoded.generation == 77 && decoded.active &&
             decoded.project_id == response.project_id &&
             decoded.candidates.size() == 2 &&
             decoded.candidates[0].symbol == "PlayerController" &&
             decoded.candidates[0].frequency == 8,
         "candidate fields round-trip without paths or source text");
}

void TestInactiveAndMalformedFrames() {
  contextime::candidate_protocol::ProjectCandidateResponse response;
  response.request_id = 3;
  response.generation = 9;
  contextime::candidate_protocol::ResponseFrame frame;
  Expect(contextime::candidate_protocol::EncodeResponse(response, frame) ==
             contextime::candidate_protocol::CodecStatus::Ok,
         "inactive fail-open snapshot encodes");
  contextime::candidate_protocol::ProjectCandidateResponse decoded;
  Expect(contextime::candidate_protocol::DecodeResponse(frame, decoded) ==
             contextime::candidate_protocol::CodecStatus::Ok &&
             !decoded.active && decoded.candidates.empty(),
         "inactive fail-open snapshot round-trips");

  auto corrupt = frame;
  corrupt[corrupt.size() - 1] = 1;
  Expect(contextime::candidate_protocol::DecodeResponse(corrupt, decoded) ==
             contextime::candidate_protocol::CodecStatus::NonZeroReserved,
         "non-zero response tail is rejected");
  response.active = true;
  response.project_id = "bad";
  response.ttl_ms = 1;
  Expect(contextime::candidate_protocol::EncodeResponse(response, frame) ==
             contextime::candidate_protocol::CodecStatus::InvalidField,
         "non-canonical active project ID is rejected");

  contextime::candidate_protocol::ProjectCandidateQuery query;
  contextime::candidate_protocol::QueryFrame query_frame;
  contextime::candidate_protocol::EncodeQuery(query, query_frame);
  query_frame[16] = 1;
  Expect(contextime::candidate_protocol::DecodeQuery(query_frame, query) ==
             contextime::candidate_protocol::CodecStatus::NonZeroReserved,
         "query reserved bytes are strict");
}

}  // namespace

int main() {
  TestQueryAndSnapshotRoundTrip();
  TestInactiveAndMalformedFrames();
  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Candidate Protocol: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
