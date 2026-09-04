#include "contextime/active_project_snapshot.h"

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

contextime::ProjectDictionaryEntry Entry(
    std::string symbol, contextime::ProjectSymbolType type,
    std::uint32_t frequency, std::uint64_t last_seen,
    contextime::ProjectSymbolSource source =
        contextime::ProjectSymbolSource::LanguageServer) {
  contextime::ProjectDictionaryEntry entry;
  entry.symbol = std::move(symbol);
  entry.symbol_type = type;
  entry.frequency = frequency;
  entry.last_seen_ms = last_seen;
  entry.source = source;
  return entry;
}

void TestBoundedLanguageServerSnapshot() {
  contextime::ProjectDictionarySnapshot dictionary;
  dictionary.project_id = "00112233445566778899aabbccddeeff";
  dictionary.enabled = true;
  dictionary.entries = {
      Entry("Low", contextime::ProjectSymbolType::Method, 1, 10),
      Entry("PlayerController", contextime::ProjectSymbolType::Class, 8, 20),
      Entry("PlayerController", contextime::ProjectSymbolType::Method, 7, 30),
      Entry("Recent", contextime::ProjectSymbolType::Property, 8, 40),
      Entry("ManualOnly", contextime::ProjectSymbolType::Term, 99, 50,
            contextime::ProjectSymbolSource::Manual),
  };

  contextime::ActiveProjectSnapshotCache cache;
  cache.Publish(dictionary, 1000);
  const auto snapshot = cache.Read();
  Expect(snapshot && snapshot->project_id == dictionary.project_id,
         "published snapshot owns only the active opaque project ID");
  Expect(snapshot && snapshot->candidates.size() == 3,
         "manual source and duplicate candidate text are excluded");
  Expect(snapshot && snapshot->candidates[0].symbol == "Recent" &&
             snapshot->candidates[1].symbol == "PlayerController" &&
             snapshot->candidates[2].symbol == "Low",
         "snapshot is ranked by frequency then recency");
  Expect(snapshot && contextime::IsActiveProjectSnapshotLive(*snapshot, 4000) &&
             !contextime::IsActiveProjectSnapshotLive(*snapshot, 4001),
         "active project lease expires deterministically");

  const auto generation = snapshot->generation;
  Expect(cache.RefreshLease(dictionary.project_id, 2000),
         "same active project refreshes without dictionary I/O");
  const auto refreshed = cache.Read();
  Expect(refreshed && refreshed->generation == generation &&
             refreshed->expires_at_ms == 5000,
         "lease refresh keeps immutable candidate generation stable");
  Expect(!cache.RefreshLease(dictionary.project_id, 5001) &&
             cache.Read()->expires_at_ms == 5000,
         "expired snapshot cannot be revived by a later project heartbeat");
  Expect(!cache.RefreshLease("ffeeddccbbaa99887766554433221100", 2000),
         "different project cannot refresh the active lease");
}

void TestDisableClearAndLimits() {
  contextime::ProjectDictionarySnapshot dictionary;
  dictionary.project_id = "00112233445566778899aabbccddeeff";
  dictionary.enabled = true;
  for (std::size_t index = 0;
       index < contextime::kMaximumActiveProjectCandidates + 20; ++index) {
    dictionary.entries.push_back(Entry(
        "Symbol" + std::to_string(index),
        contextime::ProjectSymbolType::Class, 1,
        static_cast<std::uint64_t>(index)));
  }
  contextime::ActiveProjectSnapshotCache cache;
  cache.Publish(dictionary, 0);
  Expect(cache.Read()->candidates.size() ==
             contextime::kMaximumActiveProjectCandidates,
         "active snapshot candidate count is bounded");

  dictionary.enabled = false;
  cache.Publish(dictionary, 0);
  Expect(cache.Read()->project_id == dictionary.project_id &&
             cache.Read()->candidates.empty(),
         "disabled dictionary publishes an empty project snapshot");
  cache.Clear();
  Expect(cache.Read()->project_id.empty() && cache.Read()->candidates.empty(),
         "clear removes active project candidates fail-open");
}

}  // namespace

int main() {
  TestBoundedLanguageServerSnapshot();
  TestDisableClearAndLimits();
  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Active Project Snapshot: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
