#include "contextime/active_project_snapshot.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <utility>

namespace contextime {
namespace {

std::uint64_t LeaseDeadline(std::uint64_t now_ms) noexcept {
  const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  return now_ms > maximum - kActiveProjectLeaseMs
             ? maximum
             : now_ms + kActiveProjectLeaseMs;
}

bool CandidateOrder(const ProjectDictionaryEntry* left,
                    const ProjectDictionaryEntry* right) noexcept {
  if (left->frequency != right->frequency) {
    return left->frequency > right->frequency;
  }
  if (left->last_seen_ms != right->last_seen_ms) {
    return left->last_seen_ms > right->last_seen_ms;
  }
  if (left->symbol != right->symbol) {
    return left->symbol < right->symbol;
  }
  return left->symbol_type < right->symbol_type;
}

std::shared_ptr<const ActiveProjectSnapshot> EmptySnapshot(
    std::uint64_t generation) {
  auto snapshot = std::make_shared<ActiveProjectSnapshot>();
  snapshot->generation = generation;
  return snapshot;
}

}  // namespace

ActiveProjectSnapshotCache::ActiveProjectSnapshotCache()
    : snapshot_(EmptySnapshot(next_generation_++)) {}

void ActiveProjectSnapshotCache::Publish(
    const ProjectDictionarySnapshot& dictionary,
    std::uint64_t now_ms) noexcept {
  try {
    auto published = std::make_shared<ActiveProjectSnapshot>();
    published->generation = next_generation_++;
    published->expires_at_ms = LeaseDeadline(now_ms);
    published->project_id = dictionary.project_id;

    if (dictionary.enabled) {
      std::vector<const ProjectDictionaryEntry*> ordered;
      ordered.reserve(dictionary.entries.size());
      for (const auto& entry : dictionary.entries) {
        // Only approved sources reach candidates: M5.2 Language Server symbols
        // and M5.5 user-approved manual terms added through the manager.
        if ((entry.source == ProjectSymbolSource::LanguageServer ||
             entry.source == ProjectSymbolSource::Manual) &&
            IsValidProjectDictionaryEntry(entry)) {
          ordered.push_back(&entry);
        }
      }
      std::sort(ordered.begin(), ordered.end(), CandidateOrder);

      std::set<std::string> emitted;
      std::size_t symbol_bytes = 0;
      for (const auto* entry : ordered) {
        if (published->candidates.size() >=
                kMaximumActiveProjectCandidates ||
            symbol_bytes + entry->symbol.size() >
                kMaximumActiveProjectSymbolBytes) {
          break;
        }
        if (!emitted.insert(entry->symbol).second) {
          continue;
        }
        ProjectCandidateEntry candidate;
        candidate.symbol = entry->symbol;
        candidate.symbol_type = entry->symbol_type;
        candidate.frequency = entry->frequency;
        symbol_bytes += candidate.symbol.size();
        published->candidates.push_back(std::move(candidate));
      }
    }

    std::atomic_store_explicit(
        &snapshot_, std::shared_ptr<const ActiveProjectSnapshot>(published),
        std::memory_order_release);
  } catch (...) {
    // Fail open and remove any previous project's candidates. Allocation or
    // filtering failure must never leave a cross-project stale snapshot.
    Clear();
  }
}

bool ActiveProjectSnapshotCache::RefreshLease(
    const std::string& project_id, std::uint64_t now_ms) noexcept {
  try {
    const auto current = Read();
    if (!current || current->project_id != project_id || project_id.empty() ||
        !IsActiveProjectSnapshotLive(*current, now_ms)) {
      return false;
    }
    auto refreshed = std::make_shared<ActiveProjectSnapshot>(*current);
    refreshed->expires_at_ms = LeaseDeadline(now_ms);
    std::atomic_store_explicit(
        &snapshot_, std::shared_ptr<const ActiveProjectSnapshot>(refreshed),
        std::memory_order_release);
    return true;
  } catch (...) {
    Clear();
    return false;
  }
}

void ActiveProjectSnapshotCache::Clear() noexcept {
  try {
    std::atomic_store_explicit(&snapshot_, EmptySnapshot(next_generation_++),
                               std::memory_order_release);
  } catch (...) {
    // Retain the last immutable object only if even an empty allocation fails.
    // Its lease still expires, so readers will fail open.
  }
}

std::shared_ptr<const ActiveProjectSnapshot>
ActiveProjectSnapshotCache::Read() const noexcept {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

bool IsActiveProjectSnapshotLive(const ActiveProjectSnapshot& snapshot,
                                 std::uint64_t now_ms) noexcept {
  return !snapshot.project_id.empty() && now_ms <= snapshot.expires_at_ms;
}

}  // namespace contextime
