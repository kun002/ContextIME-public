#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace contextime {

inline constexpr std::size_t kProjectIdLength = 32;
inline constexpr std::size_t kMaximumProjectSymbolBytes = 128;
inline constexpr std::size_t kMaximumProjectDictionaryEntries = 100000;

enum class ProjectSymbolType : std::uint8_t {
  Class = 0,
  Method,
  Property,
  Enum,
  Namespace,
  File,
  Directory,
  Asset,
  Shader,
  Term,
};

enum class ProjectSymbolSource : std::uint8_t {
  LanguageServer = 0,
  CompilationDatabase,
  ProjectFile,
  FileSystem,
  Manual,
};

struct ProjectDictionaryEntry {
  std::string symbol;
  ProjectSymbolType symbol_type = ProjectSymbolType::Term;
  std::uint32_t frequency = 1;
  std::uint64_t last_seen_ms = 0;
  ProjectSymbolSource source = ProjectSymbolSource::Manual;
};

struct ProjectDictionarySnapshot {
  std::string project_id;
  bool exists = false;
  bool enabled = true;
  std::vector<ProjectDictionaryEntry> entries;
};

enum class ProjectDictionaryStatus : std::uint8_t {
  Ok = 0,
  InvalidProjectId,
  InvalidEntry,
  CorruptData,
  IoError,
};

// This store is owned by one background Project Indexer or Context Service
// thread. It deliberately performs file I/O and must never be called from the
// TSF key-event path.
class ProjectDictionaryStore final {
 public:
  explicit ProjectDictionaryStore(std::filesystem::path root_directory);

  ProjectDictionaryStatus Load(
      const std::string& project_id,
      ProjectDictionarySnapshot& snapshot) const noexcept;

  // Adds or refreshes entries identified by symbol/type/source. Frequency and
  // last_seen_ms never move backwards when the same entry is observed again.
  ProjectDictionaryStatus Upsert(
      const std::string& project_id,
      const std::vector<ProjectDictionaryEntry>& entries,
      std::shared_ptr<const ProjectDictionarySnapshot>* persisted_snapshot =
          nullptr) noexcept;

  ProjectDictionaryStatus SetEnabled(const std::string& project_id,
                                     bool enabled) noexcept;
  // Removes the single entry keyed by symbol/type/source. Removing an absent
  // key succeeds without touching the file; the optional out-parameter receives
  // the exact persisted snapshot after a successful save.
  ProjectDictionaryStatus RemoveEntry(
      const std::string& project_id, const ProjectDictionaryEntry& entry,
      std::shared_ptr<const ProjectDictionarySnapshot>* persisted_snapshot =
          nullptr) noexcept;
  ProjectDictionaryStatus Remove(const std::string& project_id) noexcept;
  ProjectDictionaryStatus ListProjects(
      std::vector<std::string>& project_ids) const noexcept;

  const std::filesystem::path& root_directory() const noexcept {
    return root_directory_;
  }

 private:
  ProjectDictionaryStatus Save(
      const std::string& project_id,
      const ProjectDictionarySnapshot& snapshot) const noexcept;
  std::filesystem::path DictionaryPath(
      const std::string& project_id) const;
  bool CaptureFileStamp(
      const std::string& project_id, bool& exists, std::uintmax_t& size,
      std::filesystem::file_time_type& write_time) const noexcept;
  bool TryLoadCached(const std::string& project_id,
                     ProjectDictionarySnapshot& snapshot) const noexcept;
  void CacheSnapshot(
      std::shared_ptr<const ProjectDictionarySnapshot> snapshot) const noexcept;
  void CacheSnapshotCopy(
      const ProjectDictionarySnapshot& snapshot) const noexcept;
  void InvalidateCached(const std::string& project_id) const noexcept;

  std::filesystem::path root_directory_;
  // The Store has one background owner. Retaining only its most recently
  // observed project avoids reparsing a 100k-entry active dictionary for every
  // bounded 64-record batch while keeping memory bounded to one project.
  mutable std::shared_ptr<const ProjectDictionarySnapshot> cached_snapshot_;
  mutable bool cached_file_exists_ = false;
  mutable std::uintmax_t cached_file_size_ = 0;
  mutable std::filesystem::file_time_type cached_file_write_time_{};
};

bool IsValidProjectId(const std::string& project_id) noexcept;
bool IsValidProjectDictionaryEntry(
    const ProjectDictionaryEntry& entry) noexcept;
const char* ToString(ProjectSymbolType type) noexcept;
const char* ToString(ProjectSymbolSource source) noexcept;
const char* ToString(ProjectDictionaryStatus status) noexcept;

}  // namespace contextime
