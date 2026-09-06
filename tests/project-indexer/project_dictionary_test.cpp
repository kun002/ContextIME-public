#include "contextime/project_dictionary.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using contextime::ProjectDictionaryEntry;
using contextime::ProjectDictionarySnapshot;
using contextime::ProjectDictionaryStatus;
using contextime::ProjectDictionaryStore;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;

constexpr char kProjectA[] = "00112233445566778899aabbccddeeff";
constexpr char kProjectB[] = "ffeeddccbbaa99887766554433221100";

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

class TemporaryDirectory final {
 public:
  TemporaryDirectory() {
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    path_ = std::filesystem::temp_directory_path() /
            ("contextime-project-dictionary-test-" +
             std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

ProjectDictionaryEntry Entry(
    std::string symbol, ProjectSymbolType type,
    ProjectSymbolSource source = ProjectSymbolSource::LanguageServer,
    std::uint32_t frequency = 1, std::uint64_t last_seen_ms = 100) {
  ProjectDictionaryEntry entry;
  entry.symbol = std::move(symbol);
  entry.symbol_type = type;
  entry.frequency = frequency;
  entry.last_seen_ms = last_seen_ms;
  entry.source = source;
  return entry;
}

void TestValidationBoundary() {
  Expect(contextime::IsValidProjectId(kProjectA),
         "lowercase 128-bit project id accepted");
  Expect(!contextime::IsValidProjectId("../00112233445566778899aabbccddeeff"),
         "path traversal project id rejected");
  Expect(!contextime::IsValidProjectId(
             "00112233445566778899AABBCCDDEEFF"),
         "non-canonical uppercase project id rejected");
  Expect(!contextime::IsValidProjectId("00112233"),
         "short project id rejected");

  Expect(contextime::IsValidProjectDictionaryEntry(
             Entry("PlayerController", ProjectSymbolType::Class)),
         "ordinary language-server symbol accepted");
  Expect(contextime::IsValidProjectDictionaryEntry(
             Entry("\xe7\x8e\xa9\xe5\xae\xb6\xe6\x8e\xa7\xe5\x88\xb6\xe5\x99\xa8",
                   ProjectSymbolType::Term,
                   ProjectSymbolSource::Manual)),
         "valid UTF-8 project term accepted");
  Expect(!contextime::IsValidProjectDictionaryEntry(
             Entry("Assets/Secret.txt", ProjectSymbolType::File)),
         "full or relative path is not accepted as a symbol");
  Expect(!contextime::IsValidProjectDictionaryEntry(
             Entry("API\nKey", ProjectSymbolType::Property)),
         "control characters are rejected");
  Expect(!contextime::IsValidProjectDictionaryEntry(
             Entry("\xc0\xaf", ProjectSymbolType::Term)),
         "invalid UTF-8 is rejected");
  Expect(!contextime::IsValidProjectDictionaryEntry(
             Entry("zero", ProjectSymbolType::Term,
                   ProjectSymbolSource::Manual, 0)),
         "zero frequency is rejected");
}

void TestPersistenceIsolationAndIncrementalUpsert() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  const std::vector<ProjectDictionaryEntry> first = {
      Entry("PlayerController", ProjectSymbolType::Class,
            ProjectSymbolSource::LanguageServer, 2, 100),
      Entry("SpawnPlayer", ProjectSymbolType::Method,
            ProjectSymbolSource::LanguageServer, 1, 110),
  };
  std::shared_ptr<const ProjectDictionarySnapshot> persisted;
  Expect(store.Upsert(kProjectA, first, &persisted) ==
             ProjectDictionaryStatus::Ok,
         "first project snapshot persisted");
  Expect(persisted && persisted->project_id == kProjectA &&
             persisted->exists && persisted->enabled &&
             persisted->entries.size() == first.size(),
         "successful upsert returns the exact persisted snapshot");
  Expect(store.Upsert(
             kProjectB,
             {Entry("PlayerController", ProjectSymbolType::Class,
                    ProjectSymbolSource::LanguageServer, 7, 200)}) ==
             ProjectDictionaryStatus::Ok,
         "second project snapshot persisted independently");

  Expect(store.Upsert(
             kProjectA,
             {Entry("PlayerController", ProjectSymbolType::Class,
                    ProjectSymbolSource::LanguageServer, 1, 90),
              Entry("PlayerController", ProjectSymbolType::Term,
                    ProjectSymbolSource::Manual, 4, 130)}) ==
             ProjectDictionaryStatus::Ok,
          "incremental update persisted");

  persisted = std::make_shared<ProjectDictionarySnapshot>();
  Expect(store.Upsert(
             "../../outside", {}, &persisted) ==
             ProjectDictionaryStatus::InvalidProjectId && !persisted,
         "failed upsert clears the persisted snapshot output");

  ProjectDictionaryStore restarted(temporary.path());
  ProjectDictionarySnapshot project_a;
  Expect(restarted.Load(kProjectA, project_a) ==
             ProjectDictionaryStatus::Ok,
         "project A reload succeeds");
  Expect(project_a.exists && project_a.enabled,
         "project A metadata survives restart");
  Expect(project_a.project_id == kProjectA,
         "loaded snapshot exposes only its opaque project id");
  Expect(project_a.entries.size() == 3,
         "same text from distinct provenance remains inspectable");
  Expect(project_a.entries[0].symbol == "PlayerController" &&
             project_a.entries[0].frequency == 2 &&
             project_a.entries[0].last_seen_ms == 100,
         "replayed stale observation cannot move counters backwards");

  ProjectDictionarySnapshot project_b;
  Expect(restarted.Load(kProjectB, project_b) ==
             ProjectDictionaryStatus::Ok,
         "project B reload succeeds");
  Expect(project_b.entries.size() == 1 &&
             project_b.entries[0].frequency == 7,
         "same symbol does not leak frequency across projects");

  std::vector<std::string> projects;
  Expect(restarted.ListProjects(projects) == ProjectDictionaryStatus::Ok,
         "project list succeeds");
  Expect(projects == std::vector<std::string>({kProjectA, kProjectB}),
         "only canonical project dictionary files are listed");
}

void TestDisableDeleteAndMissingProject() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  Expect(store.SetEnabled(kProjectA, false) == ProjectDictionaryStatus::Ok,
         "a disabled empty project dictionary can be created");

  ProjectDictionarySnapshot snapshot;
  Expect(store.Load(kProjectA, snapshot) == ProjectDictionaryStatus::Ok &&
             snapshot.exists && !snapshot.enabled,
         "disabled state persists");
  Expect(store.Upsert(kProjectA,
                      {Entry("Vector3", ProjectSymbolType::Class)}) ==
             ProjectDictionaryStatus::Ok,
         "disabled dictionary can receive background updates");
  Expect(store.Load(kProjectA, snapshot) == ProjectDictionaryStatus::Ok &&
             !snapshot.enabled && snapshot.entries.size() == 1,
         "upsert does not silently enable a dictionary");

  Expect(store.Remove(kProjectA) == ProjectDictionaryStatus::Ok,
         "project dictionary delete succeeds");
  Expect(store.Load(kProjectA, snapshot) == ProjectDictionaryStatus::Ok &&
             snapshot.project_id == kProjectA && !snapshot.exists &&
             snapshot.enabled && snapshot.entries.empty(),
         "deleted project reloads as an empty default snapshot");
  Expect(store.Remove(kProjectA) == ProjectDictionaryStatus::Ok,
         "project dictionary delete is idempotent");
}

void TestInvalidInputCannotMutateStorage() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  Expect(store.Upsert("../../outside", {}) ==
             ProjectDictionaryStatus::InvalidProjectId,
         "invalid project id cannot create a file");
  Expect(store.Upsert(
             kProjectA,
             {Entry("C:\\secret\\token.txt", ProjectSymbolType::File)}) ==
             ProjectDictionaryStatus::InvalidEntry,
         "path-like source value cannot enter dictionary");
  std::vector<std::string> projects;
  Expect(store.ListProjects(projects) == ProjectDictionaryStatus::Ok &&
             projects.empty(),
         "rejected updates leave no dictionary data");
}

void TestCorruptDataIsRejectedWithoutOverwrite() {
  TemporaryDirectory temporary;
  const std::filesystem::path path = temporary.path() /
                                     (std::string(kProjectA) + ".dict");
  {
    std::ofstream output(path, std::ios::binary);
    output << "ContextIME-Project-Dictionary\t1\t1\n"
              "class\t1\t100\tlanguage_server\tnot-hex\n";
  }
  ProjectDictionaryStore store(temporary.path());
  ProjectDictionarySnapshot snapshot;
  Expect(store.Load(kProjectA, snapshot) ==
             ProjectDictionaryStatus::CorruptData,
         "malformed persisted symbol is rejected");
  Expect(store.Upsert(kProjectA,
                      {Entry("SafeSymbol", ProjectSymbolType::Class)}) ==
             ProjectDictionaryStatus::CorruptData,
         "corrupt data is never overwritten by an update");

  std::ifstream input(path, std::ios::binary);
  const std::string unchanged((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
  Expect(unchanged.find("not-hex") != std::string::npos,
         "failed update leaves corrupt evidence available for diagnosis");
}

void TestCachedSnapshotInvalidatesAfterExternalReplacement() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  std::shared_ptr<const ProjectDictionarySnapshot> persisted;
  Expect(store.Upsert(
             kProjectA,
             {Entry("CachedSymbol", ProjectSymbolType::Class)},
             &persisted) == ProjectDictionaryStatus::Ok && persisted,
         "successful upsert establishes the single-owner snapshot cache");

  const std::filesystem::path path = temporary.path() /
                                     (std::string(kProjectA) + ".dict");
  const std::string corrupt =
      "externally-replaced-corrupt-project-dictionary\n";
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << corrupt;
  }

  persisted = std::make_shared<ProjectDictionarySnapshot>();
  Expect(store.Upsert(kProjectA,
                      {Entry("NewSymbol", ProjectSymbolType::Method)},
                      &persisted) == ProjectDictionaryStatus::CorruptData &&
             !persisted,
         "external file replacement invalidates cache and clears output");

  std::ifstream input(path, std::ios::binary);
  const std::string unchanged((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
  Expect(unchanged == corrupt,
         "cached update failure preserves externally replaced evidence");
}

void TestRemoveEntryTargetsSingleKeyAndPersists() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  const std::vector<ProjectDictionaryEntry> seed = {
      Entry("PhotonSlash", ProjectSymbolType::Term,
            ProjectSymbolSource::Manual, 3, 100),
      Entry("PhotonSlash", ProjectSymbolType::Method,
            ProjectSymbolSource::LanguageServer, 2, 110),
      Entry("SpawnPlayer", ProjectSymbolType::Method,
            ProjectSymbolSource::LanguageServer, 1, 120),
  };
  Expect(store.Upsert(kProjectA, seed) == ProjectDictionaryStatus::Ok,
         "remove-entry fixture persists three keyed entries");

  std::shared_ptr<const ProjectDictionarySnapshot> persisted;
  Expect(store.RemoveEntry(
             kProjectA,
             Entry("PhotonSlash", ProjectSymbolType::Term,
                   ProjectSymbolSource::Manual, 9, 999),
             &persisted) == ProjectDictionaryStatus::Ok,
         "removing the manual term key succeeds");
  Expect(persisted && persisted->exists && persisted->enabled &&
             persisted->entries.size() == 2,
         "removal returns the exact persisted snapshot");
  bool only_method_remains = false;
  for (const auto& entry : persisted->entries) {
    if (entry.symbol == "PhotonSlash") {
      only_method_remains =
          entry.symbol_type == ProjectSymbolType::Method &&
          entry.source == ProjectSymbolSource::LanguageServer;
    }
  }
  Expect(only_method_remains,
         "only the matching symbol/type/source key is removed");
  ProjectDictionarySnapshot reloaded;
  Expect(store.Load(kProjectA, reloaded) == ProjectDictionaryStatus::Ok &&
             reloaded.entries.size() == 2,
         "removal persists across reload");

  Expect(store.RemoveEntry(kProjectA,
                           Entry("Missing", ProjectSymbolType::Term,
                                 ProjectSymbolSource::Manual)) ==
             ProjectDictionaryStatus::Ok,
         "removing an absent key is idempotent");
  Expect(store.RemoveEntry(kProjectB,
                           Entry("PhotonSlash", ProjectSymbolType::Term,
                                 ProjectSymbolSource::Manual)) ==
             ProjectDictionaryStatus::Ok,
         "removing from a project without a dictionary is idempotent");
  ProjectDictionarySnapshot missing;
  Expect(store.Load(kProjectB, missing) == ProjectDictionaryStatus::Ok &&
             !missing.exists,
         "removal never creates a project file");

  Expect(store.RemoveEntry(kProjectA,
                           Entry("Bad\nKey", ProjectSymbolType::Term,
                                 ProjectSymbolSource::Manual)) ==
             ProjectDictionaryStatus::InvalidEntry,
         "invalid entry key is rejected before storage");
  Expect(store.RemoveEntry(
             "00112233", Entry("X", ProjectSymbolType::Term,
                               ProjectSymbolSource::Manual)) ==
             ProjectDictionaryStatus::InvalidProjectId,
         "invalid project id is rejected before storage");
}

void TestStableNames() {
  Expect(std::string(contextime::ToString(ProjectSymbolType::Namespace)) ==
             "namespace",
         "symbol type has stable persisted name");
  Expect(std::string(contextime::ToString(
             ProjectSymbolSource::CompilationDatabase)) ==
             "compilation_database",
         "symbol source has stable persisted name");
  Expect(std::string(contextime::ToString(
             ProjectDictionaryStatus::InvalidProjectId)) ==
             "INVALID_PROJECT_ID",
         "store status has stable diagnostic name");
}

}  // namespace

int main() {
  TestValidationBoundary();
  TestPersistenceIsolationAndIncrementalUpsert();
  TestDisableDeleteAndMissingProject();
  TestInvalidInputCannotMutateStorage();
  TestCorruptDataIsRejectedWithoutOverwrite();
  TestCachedSnapshotInvalidatesAfterExternalReplacement();
  TestRemoveEntryTargetsSingleKeyAndPersists();
  TestStableNames();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Dictionary: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
