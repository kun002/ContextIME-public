#include "contextime/active_project_snapshot.h"
#include "contextime/project_dictionary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using contextime::ActiveProjectSnapshotCache;
using contextime::ProjectDictionaryEntry;
using contextime::ProjectDictionarySnapshot;
using contextime::ProjectDictionaryStatus;
using contextime::ProjectDictionaryStore;
using contextime::ProjectSymbolSource;
using contextime::ProjectSymbolType;

using Clock = std::chrono::steady_clock;

constexpr char kProjectId[] = "0123456789abcdef0123456789abcdef";
constexpr std::size_t kEntryCount =
    contextime::kMaximumProjectDictionaryEntries;
constexpr std::size_t kIncrementBatchSize = 64;
constexpr std::size_t kIncrementIterations = 4;
constexpr std::uint64_t kSeedBudgetMs = 30000;
constexpr std::uint64_t kLoadBudgetMs = 10000;
constexpr std::uint64_t kPublishBudgetMs = 5000;
constexpr std::uint64_t kIncrementBudgetMs = 10000;
constexpr std::uint64_t kMaximumSnapshotReadUs = 1000000;

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
    const auto nonce = Clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("contextime-project-performance-test-" +
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

std::string Symbol(std::size_t index) {
  std::string suffix = std::to_string(index);
  suffix.insert(suffix.begin(), 6u - suffix.size(), '0');
  return "ProjectSymbol" + suffix;
}

ProjectDictionaryEntry Entry(std::size_t index, std::uint32_t frequency,
                             std::uint64_t last_seen_ms) {
  ProjectDictionaryEntry entry;
  entry.symbol = Symbol(index);
  entry.symbol_type = static_cast<ProjectSymbolType>(
      index % (static_cast<std::size_t>(ProjectSymbolType::Namespace) + 1u));
  entry.frequency = frequency;
  entry.last_seen_ms = last_seen_ms;
  entry.source = ProjectSymbolSource::LanguageServer;
  return entry;
}

template <typename Operation>
std::uint64_t MeasureMilliseconds(Operation&& operation) {
  const auto started = Clock::now();
  operation();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          Clock::now() - started)
          .count());
}

std::uint64_t MeasureMicroseconds(
    const std::shared_ptr<const contextime::ActiveProjectSnapshot>& snapshot,
    bool& valid) {
  const auto started = Clock::now();
  valid = snapshot && snapshot->project_id == kProjectId &&
          !snapshot->candidates.empty() &&
          snapshot->candidates.size() <=
              contextime::kMaximumActiveProjectCandidates;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now() - started)
          .count());
}

void TestMaximumDictionaryBackgroundIsolation() {
  TemporaryDirectory temporary;
  ProjectDictionaryStore store(temporary.path());
  std::vector<ProjectDictionaryEntry> entries;
  entries.reserve(kEntryCount);
  for (std::size_t index = 0; index < kEntryCount; ++index) {
    entries.push_back(Entry(
        index, static_cast<std::uint32_t>((index % 100u) + 1u),
        1000u + static_cast<std::uint64_t>(index)));
  }

  ProjectDictionaryStatus seed_status = ProjectDictionaryStatus::IoError;
  const std::uint64_t seed_ms = MeasureMilliseconds([&] {
    seed_status = store.Upsert(kProjectId, entries);
  });
  Expect(seed_status == ProjectDictionaryStatus::Ok,
         "maximum dictionary seed succeeds");
  Expect(seed_ms <= kSeedBudgetMs,
         "maximum dictionary seed stays inside the broad runner budget");

  const std::filesystem::path dictionary_path =
      temporary.path() / (std::string(kProjectId) + ".dict");
  std::error_code file_error;
  const std::uintmax_t file_bytes =
      std::filesystem::file_size(dictionary_path, file_error);
  Expect(!file_error && file_bytes > 0u &&
             file_bytes <= 32u * 1024u * 1024u,
         "maximum dictionary remains inside the persisted file cap");

  ProjectDictionarySnapshot dictionary;
  ProjectDictionaryStatus load_status = ProjectDictionaryStatus::IoError;
  const std::uint64_t load_ms = MeasureMilliseconds([&] {
    load_status = store.Load(kProjectId, dictionary);
  });
  Expect(load_status == ProjectDictionaryStatus::Ok && dictionary.exists &&
             dictionary.entries.size() == kEntryCount,
         "maximum dictionary reload returns every entry");
  Expect(load_ms <= kLoadBudgetMs,
         "maximum dictionary reload stays inside the broad runner budget");

  ActiveProjectSnapshotCache cache;
  const std::uint64_t publish_ms = MeasureMilliseconds([&] {
    cache.Publish(dictionary, 1000);
  });
  auto initial = cache.Read();
  Expect(initial && initial->project_id == kProjectId &&
             initial->candidates.size() ==
                 contextime::kMaximumActiveProjectCandidates,
         "maximum dictionary publishes a bounded immutable snapshot");
  Expect(publish_ms <= kPublishBudgetMs,
         "maximum dictionary snapshot publication stays bounded");

  std::array<ProjectDictionaryStatus, kIncrementIterations> upsert_statuses;
  upsert_statuses.fill(ProjectDictionaryStatus::IoError);
  std::atomic<bool> worker_started{false};
  std::atomic<bool> worker_done{false};
  std::uint64_t increment_ms = 0;

  std::thread worker([&] {
    worker_started.store(true, std::memory_order_release);
    increment_ms = MeasureMilliseconds([&] {
      for (std::size_t iteration = 0; iteration < kIncrementIterations;
           ++iteration) {
        std::vector<ProjectDictionaryEntry> updates;
        updates.reserve(kIncrementBatchSize);
        for (std::size_t offset = 0; offset < kIncrementBatchSize; ++offset) {
          const std::size_t index = iteration * kIncrementBatchSize + offset;
          updates.push_back(Entry(
              index, static_cast<std::uint32_t>(1000u + iteration),
              1000000u + static_cast<std::uint64_t>(iteration)));
        }
        std::shared_ptr<const ProjectDictionarySnapshot> updated;
        upsert_statuses[iteration] =
            store.Upsert(kProjectId, updates, &updated);
        if (upsert_statuses[iteration] == ProjectDictionaryStatus::Ok &&
            updated) {
          cache.Publish(*updated, 2000u +
                                      static_cast<std::uint64_t>(iteration));
        }
      }
    });
    worker_done.store(true, std::memory_order_release);
  });

  while (!worker_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::uint64_t snapshot_reads = 0;
  std::uint64_t maximum_snapshot_read_us = 0;
  bool snapshots_valid = true;
  while (!worker_done.load(std::memory_order_acquire)) {
    bool valid = false;
    const auto snapshot = cache.Read();
    const std::uint64_t read_us = MeasureMicroseconds(snapshot, valid);
    maximum_snapshot_read_us =
        (std::max)(maximum_snapshot_read_us, read_us);
    snapshots_valid = snapshots_valid && valid;
    ++snapshot_reads;
    if ((snapshot_reads % 256u) == 0u) {
      std::this_thread::yield();
    }
  }
  worker.join();

  Expect(std::all_of(upsert_statuses.begin(), upsert_statuses.end(),
                     [](ProjectDictionaryStatus status) {
                       return status == ProjectDictionaryStatus::Ok;
                     }),
         "bounded incremental updates return persisted snapshots");
  Expect(increment_ms <= kIncrementBudgetMs,
         "bounded incremental updates stay inside the broad runner budget");
  Expect(snapshot_reads >= 1000u && snapshots_valid,
         "immutable snapshot readers progress during background persistence");
  Expect(maximum_snapshot_read_us <= kMaximumSnapshotReadUs,
         "immutable snapshot reads do not wait on dictionary persistence");

  const auto final_snapshot = cache.Read();
  Expect(final_snapshot && final_snapshot->generation > initial->generation &&
             final_snapshot->candidates.size() ==
                 contextime::kMaximumActiveProjectCandidates,
         "background updates publish a new bounded snapshot generation");

  std::cout << "Project Dictionary Performance metrics: entries="
            << kEntryCount << " file_bytes=" << file_bytes
            << " seed_ms=" << seed_ms << " load_ms=" << load_ms
            << " publish_ms=" << publish_ms
            << " incremental_iterations=" << kIncrementIterations
            << " incremental_ms=" << increment_ms
            << " snapshot_reads=" << snapshot_reads
            << " max_snapshot_read_us=" << maximum_snapshot_read_us << '\n';
}

}  // namespace

int main() {
  TestMaximumDictionaryBackgroundIsolation();
  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Project Dictionary Performance: " << assertions
            << " assertions passed\n";
  return EXIT_SUCCESS;
}
