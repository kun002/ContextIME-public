#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "contextime/project_dictionary.h"

namespace contextime {

inline constexpr std::size_t kMaximumActiveProjectCandidates = 256;
inline constexpr std::size_t kMaximumActiveProjectSymbolBytes = 24576;
inline constexpr std::uint32_t kActiveProjectLeaseMs = 3000;

struct ProjectCandidateEntry {
  std::string symbol;
  ProjectSymbolType symbol_type = ProjectSymbolType::Term;
  std::uint32_t frequency = 1;
};

// This object is immutable after publication. Readers may retain it without
// locks while the single Project Indexer owner publishes a replacement.
struct ActiveProjectSnapshot {
  std::uint64_t generation = 0;
  std::uint64_t expires_at_ms = 0;
  std::string project_id;
  std::vector<ProjectCandidateEntry> candidates;
};

class ActiveProjectSnapshotCache final {
 public:
  ActiveProjectSnapshotCache();

  // Single-writer operations. The Project Indexer background thread is the
  // only permitted caller; dictionary I/O must complete before Publish.
  void Publish(const ProjectDictionarySnapshot& dictionary,
               std::uint64_t now_ms) noexcept;
  bool RefreshLease(const std::string& project_id,
                    std::uint64_t now_ms) noexcept;
  void Clear() noexcept;

  // Lock-free immutable read for the candidate snapshot transport thread.
  std::shared_ptr<const ActiveProjectSnapshot> Read() const noexcept;

 private:
  std::uint64_t next_generation_ = 1;
  std::shared_ptr<const ActiveProjectSnapshot> snapshot_;
};

bool IsActiveProjectSnapshotLive(const ActiveProjectSnapshot& snapshot,
                                 std::uint64_t now_ms) noexcept;

}  // namespace contextime
