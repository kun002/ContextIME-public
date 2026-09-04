#include "contextime/project_dictionary.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace contextime {
namespace {

constexpr char kDictionaryHeader[] = "ContextIME-Project-Dictionary";
constexpr char kDictionaryVersion[] = "1";
constexpr char kDictionaryExtension[] = ".dict";
constexpr std::uintmax_t kMaximumDictionaryFileBytes = 32u * 1024u * 1024u;

using Fields = std::vector<std::string>;
using EntryKey =
    std::tuple<std::string, ProjectSymbolType, ProjectSymbolSource>;

bool IsValidUtf8(const std::string& value) noexcept {
  const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = bytes[index];
    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    if (first <= 0x7f) {
      code_point = first;
    } else if (first >= 0xc2 && first <= 0xdf) {
      continuation_count = 1;
      code_point = first & 0x1fu;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuation_count = 2;
      code_point = first & 0x0fu;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuation_count = 3;
      code_point = first & 0x07u;
    } else {
      return false;
    }
    if (index + continuation_count >= value.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
      const unsigned char continuation = bytes[index + offset];
      if ((continuation & 0xc0u) != 0x80u) {
        return false;
      }
      code_point = (code_point << 6u) | (continuation & 0x3fu);
    }
    if ((continuation_count == 2 && code_point < 0x800u) ||
        (continuation_count == 3 && code_point < 0x10000u) ||
        code_point > 0x10ffffu ||
        (code_point >= 0xd800u && code_point <= 0xdfffu)) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

bool IsKnown(ProjectSymbolType type) noexcept {
  return type >= ProjectSymbolType::Class && type <= ProjectSymbolType::Term;
}

bool IsKnown(ProjectSymbolSource source) noexcept {
  return source >= ProjectSymbolSource::LanguageServer &&
         source <= ProjectSymbolSource::Manual;
}

Fields SplitTabs(const std::string& line) {
  Fields fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t separator = line.find('\t', start);
    if (separator == std::string::npos) {
      fields.push_back(line.substr(start));
      return fields;
    }
    fields.push_back(line.substr(start, separator - start));
    start = separator + 1;
  }
}

template <typename Number>
bool ParseUnsigned(const std::string& text, Number& value) noexcept {
  if (text.empty()) {
    return false;
  }
  Number parsed = 0;
  const char* const begin = text.data();
  const char* const end = begin + text.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end) {
    return false;
  }
  value = parsed;
  return true;
}

char HexDigit(unsigned int value) noexcept {
  return static_cast<char>(value < 10u ? '0' + value : 'a' + (value - 10u));
}

std::string HexEncode(const std::string& value) {
  std::string encoded;
  encoded.reserve(value.size() * 2u);
  for (const unsigned char byte : value) {
    encoded.push_back(HexDigit(byte >> 4u));
    encoded.push_back(HexDigit(byte & 0x0fu));
  }
  return encoded;
}

int HexValue(char value) noexcept {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

bool HexDecode(const std::string& encoded, std::string& value) {
  if (encoded.empty() || (encoded.size() % 2u) != 0u ||
      encoded.size() > kMaximumProjectSymbolBytes * 2u) {
    return false;
  }
  std::string decoded;
  decoded.reserve(encoded.size() / 2u);
  for (std::size_t index = 0; index < encoded.size(); index += 2u) {
    const int high = HexValue(encoded[index]);
    const int low = HexValue(encoded[index + 1u]);
    if (high < 0 || low < 0) {
      return false;
    }
    decoded.push_back(static_cast<char>((high << 4) | low));
  }
  value = std::move(decoded);
  return true;
}

bool ParseType(const std::string& value, ProjectSymbolType& type) noexcept {
  constexpr std::array<ProjectSymbolType, 10> values = {
      ProjectSymbolType::Class,     ProjectSymbolType::Method,
      ProjectSymbolType::Property,  ProjectSymbolType::Enum,
      ProjectSymbolType::Namespace, ProjectSymbolType::File,
      ProjectSymbolType::Directory, ProjectSymbolType::Asset,
      ProjectSymbolType::Shader,    ProjectSymbolType::Term,
  };
  for (const auto candidate : values) {
    if (value == ToString(candidate)) {
      type = candidate;
      return true;
    }
  }
  return false;
}

bool ParseSource(const std::string& value,
                 ProjectSymbolSource& source) noexcept {
  constexpr std::array<ProjectSymbolSource, 5> values = {
      ProjectSymbolSource::LanguageServer,
      ProjectSymbolSource::CompilationDatabase,
      ProjectSymbolSource::ProjectFile,
      ProjectSymbolSource::FileSystem,
      ProjectSymbolSource::Manual,
  };
  for (const auto candidate : values) {
    if (value == ToString(candidate)) {
      source = candidate;
      return true;
    }
  }
  return false;
}

EntryKey MakeKey(const ProjectDictionaryEntry& entry) {
  return {entry.symbol, entry.symbol_type, entry.source};
}

bool EntryLess(const ProjectDictionaryEntry& left,
               const ProjectDictionaryEntry& right) noexcept {
  return std::tie(left.symbol, left.symbol_type, left.source) <
         std::tie(right.symbol, right.symbol_type, right.source);
}

ProjectDictionaryStatus ReplaceFile(const std::filesystem::path& temporary,
                                    const std::filesystem::path& target) {
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return ProjectDictionaryStatus::IoError;
  }
#else
  std::error_code error;
  std::filesystem::rename(temporary, target, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return ProjectDictionaryStatus::IoError;
  }
#endif
  return ProjectDictionaryStatus::Ok;
}

}  // namespace

ProjectDictionaryStore::ProjectDictionaryStore(
    std::filesystem::path root_directory)
    : root_directory_(std::move(root_directory)) {}

bool ProjectDictionaryStore::CaptureFileStamp(
    const std::string& project_id, bool& exists, std::uintmax_t& size,
    std::filesystem::file_time_type& write_time) const noexcept {
  try {
    std::error_code error;
    const std::filesystem::path path = DictionaryPath(project_id);
    exists = std::filesystem::exists(path, error);
    if (error) return false;
    size = 0;
    write_time = {};
    if (!exists) return true;
    size = std::filesystem::file_size(path, error);
    if (error) return false;
    write_time = std::filesystem::last_write_time(path, error);
    return !error;
  } catch (...) {
    return false;
  }
}

bool ProjectDictionaryStore::TryLoadCached(
    const std::string& project_id,
    ProjectDictionarySnapshot& snapshot) const noexcept {
  try {
    const auto cached = cached_snapshot_;
    if (!cached || cached->project_id != project_id) return false;
    bool exists = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type write_time;
    if (!CaptureFileStamp(project_id, exists, size, write_time) ||
        exists != cached_file_exists_ || size != cached_file_size_ ||
        (exists && write_time != cached_file_write_time_)) {
      return false;
    }
    snapshot = *cached;
    return true;
  } catch (...) {
    return false;
  }
}

void ProjectDictionaryStore::CacheSnapshot(
    std::shared_ptr<const ProjectDictionarySnapshot> snapshot) const noexcept {
  try {
    bool exists = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type write_time;
    if (!snapshot ||
        !CaptureFileStamp(snapshot->project_id, exists, size, write_time)) {
      cached_snapshot_.reset();
      return;
    }
    cached_snapshot_ = std::move(snapshot);
    cached_file_exists_ = exists;
    cached_file_size_ = size;
    cached_file_write_time_ = write_time;
  } catch (...) {
    cached_snapshot_.reset();
  }
}

void ProjectDictionaryStore::CacheSnapshotCopy(
    const ProjectDictionarySnapshot& snapshot) const noexcept {
  try {
    CacheSnapshot(std::make_shared<const ProjectDictionarySnapshot>(snapshot));
  } catch (...) {
    cached_snapshot_.reset();
  }
}

void ProjectDictionaryStore::InvalidateCached(
    const std::string& project_id) const noexcept {
  if (cached_snapshot_ && cached_snapshot_->project_id == project_id) {
    cached_snapshot_.reset();
  }
}

ProjectDictionaryStatus ProjectDictionaryStore::Load(
    const std::string& project_id,
    ProjectDictionarySnapshot& snapshot) const noexcept {
  snapshot = {};
  if (!IsValidProjectId(project_id)) {
    return ProjectDictionaryStatus::InvalidProjectId;
  }
  InvalidateCached(project_id);

  try {
    snapshot.project_id = project_id;
    const std::filesystem::path path = DictionaryPath(project_id);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
      if (error) return ProjectDictionaryStatus::IoError;
      CacheSnapshotCopy(snapshot);
      return ProjectDictionaryStatus::Ok;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
      return ProjectDictionaryStatus::IoError;
    }
    if (size > kMaximumDictionaryFileBytes) {
      return ProjectDictionaryStatus::CorruptData;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
      return ProjectDictionaryStatus::IoError;
    }
    std::string line;
    if (!std::getline(input, line)) {
      return ProjectDictionaryStatus::CorruptData;
    }
    const Fields header = SplitTabs(line);
    if (header.size() != 3u || header[0] != kDictionaryHeader ||
        header[1] != kDictionaryVersion ||
        (header[2] != "0" && header[2] != "1")) {
      return ProjectDictionaryStatus::CorruptData;
    }

    ProjectDictionarySnapshot loaded;
    loaded.project_id = project_id;
    std::set<EntryKey> keys;
    loaded.exists = true;
    loaded.enabled = header[2] == "1";
    while (std::getline(input, line)) {
      if (line.empty()) {
        return ProjectDictionaryStatus::CorruptData;
      }
      if (loaded.entries.size() >= kMaximumProjectDictionaryEntries) {
        return ProjectDictionaryStatus::CorruptData;
      }
      const Fields fields = SplitTabs(line);
      ProjectDictionaryEntry entry;
      if (fields.size() != 5u || !ParseType(fields[0], entry.symbol_type) ||
          !ParseUnsigned(fields[1], entry.frequency) ||
          !ParseUnsigned(fields[2], entry.last_seen_ms) ||
          !ParseSource(fields[3], entry.source) ||
          !HexDecode(fields[4], entry.symbol) ||
          !IsValidProjectDictionaryEntry(entry)) {
        return ProjectDictionaryStatus::CorruptData;
      }
      if (!keys.insert(MakeKey(entry)).second) {
        return ProjectDictionaryStatus::CorruptData;
      }
      loaded.entries.push_back(std::move(entry));
    }
    if (!input.eof()) {
      return ProjectDictionaryStatus::IoError;
    }
    std::sort(loaded.entries.begin(), loaded.entries.end(), EntryLess);
    CacheSnapshotCopy(loaded);
    snapshot = std::move(loaded);
    return ProjectDictionaryStatus::Ok;
  } catch (...) {
    snapshot = {};
    return ProjectDictionaryStatus::IoError;
  }
}

ProjectDictionaryStatus ProjectDictionaryStore::Upsert(
    const std::string& project_id,
    const std::vector<ProjectDictionaryEntry>& entries,
    std::shared_ptr<const ProjectDictionarySnapshot>*
        persisted_snapshot) noexcept {
  if (persisted_snapshot != nullptr) {
    persisted_snapshot->reset();
  }
  if (!IsValidProjectId(project_id)) {
    return ProjectDictionaryStatus::InvalidProjectId;
  }
  if (entries.size() > kMaximumProjectDictionaryEntries ||
      std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
        return !IsValidProjectDictionaryEntry(entry);
      })) {
    return ProjectDictionaryStatus::InvalidEntry;
  }

  try {
    ProjectDictionarySnapshot snapshot;
    if (!TryLoadCached(project_id, snapshot)) {
      const ProjectDictionaryStatus load_status = Load(project_id, snapshot);
      if (load_status != ProjectDictionaryStatus::Ok) {
        return load_status;
      }
    }
    snapshot.exists = true;

    // A wire upsert contains at most 64 entries, while a persisted project may
    // contain 100,000. Keep the large loaded vector sorted and normalize only
    // the bounded incoming batch before a linear merge.
    std::map<EntryKey, ProjectDictionaryEntry> incoming;
    for (const auto& entry : entries) {
      const EntryKey key = MakeKey(entry);
      const auto result = incoming.emplace(key, entry);
      if (!result.second) {
        result.first->second.frequency =
            (std::max)(result.first->second.frequency, entry.frequency);
        result.first->second.last_seen_ms =
            (std::max)(result.first->second.last_seen_ms,
                       entry.last_seen_ms);
      }
    }

    std::vector<ProjectDictionaryEntry> merged;
    merged.reserve((std::min)(kMaximumProjectDictionaryEntries,
                              snapshot.entries.size() + incoming.size()));
    std::size_t existing_index = 0;
    auto incoming_iterator = incoming.begin();
    const auto append = [&merged](ProjectDictionaryEntry entry) {
      if (merged.size() >= kMaximumProjectDictionaryEntries) {
        return false;
      }
      merged.push_back(std::move(entry));
      return true;
    };
    while (existing_index < snapshot.entries.size() &&
           incoming_iterator != incoming.end()) {
      auto& existing = snapshot.entries[existing_index];
      auto& pending = incoming_iterator->second;
      if (EntryLess(existing, pending)) {
        if (!append(std::move(existing))) {
          return ProjectDictionaryStatus::InvalidEntry;
        }
        ++existing_index;
      } else if (EntryLess(pending, existing)) {
        if (!append(std::move(pending))) {
          return ProjectDictionaryStatus::InvalidEntry;
        }
        ++incoming_iterator;
      } else {
        existing.frequency = (std::max)(existing.frequency, pending.frequency);
        existing.last_seen_ms =
            (std::max)(existing.last_seen_ms, pending.last_seen_ms);
        if (!append(std::move(existing))) {
          return ProjectDictionaryStatus::InvalidEntry;
        }
        ++existing_index;
        ++incoming_iterator;
      }
    }
    while (existing_index < snapshot.entries.size()) {
      if (!append(std::move(snapshot.entries[existing_index++]))) {
        return ProjectDictionaryStatus::InvalidEntry;
      }
    }
    while (incoming_iterator != incoming.end()) {
      if (!append(std::move(incoming_iterator->second))) {
        return ProjectDictionaryStatus::InvalidEntry;
      }
      ++incoming_iterator;
    }
    snapshot.entries = std::move(merged);
    auto persisted =
        std::make_shared<ProjectDictionarySnapshot>(std::move(snapshot));
    const ProjectDictionaryStatus save_status = Save(project_id, *persisted);
    if (save_status == ProjectDictionaryStatus::Ok) {
      CacheSnapshot(persisted);
      if (persisted_snapshot != nullptr) {
        *persisted_snapshot = std::move(persisted);
      }
    }
    return save_status;
  } catch (...) {
    if (persisted_snapshot != nullptr) {
      persisted_snapshot->reset();
    }
    return ProjectDictionaryStatus::IoError;
  }
}

ProjectDictionaryStatus ProjectDictionaryStore::SetEnabled(
    const std::string& project_id, bool enabled) noexcept {
  ProjectDictionarySnapshot snapshot;
  const ProjectDictionaryStatus load_status = Load(project_id, snapshot);
  if (load_status != ProjectDictionaryStatus::Ok) {
    return load_status;
  }
  snapshot.exists = true;
  snapshot.enabled = enabled;
  const ProjectDictionaryStatus save_status = Save(project_id, snapshot);
  if (save_status == ProjectDictionaryStatus::Ok) {
    CacheSnapshotCopy(snapshot);
  }
  return save_status;
}

ProjectDictionaryStatus ProjectDictionaryStore::Remove(
    const std::string& project_id) noexcept {
  if (!IsValidProjectId(project_id)) {
    return ProjectDictionaryStatus::InvalidProjectId;
  }
  try {
    std::error_code error;
    const std::filesystem::path target = DictionaryPath(project_id);
    std::filesystem::remove(target, error);
    if (error) {
      return ProjectDictionaryStatus::IoError;
    }
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    std::filesystem::remove(temporary, error);
    if (error) return ProjectDictionaryStatus::IoError;
    InvalidateCached(project_id);
    return ProjectDictionaryStatus::Ok;
  } catch (...) {
    return ProjectDictionaryStatus::IoError;
  }
}

ProjectDictionaryStatus ProjectDictionaryStore::ListProjects(
    std::vector<std::string>& project_ids) const noexcept {
  project_ids.clear();
  try {
    std::error_code error;
    if (!std::filesystem::exists(root_directory_, error)) {
      return error ? ProjectDictionaryStatus::IoError
                   : ProjectDictionaryStatus::Ok;
    }
    for (std::filesystem::directory_iterator iterator(root_directory_, error),
         end;
         !error && iterator != end; iterator.increment(error)) {
      if (!iterator->is_regular_file(error)) {
        if (error) {
          return ProjectDictionaryStatus::IoError;
        }
        continue;
      }
      const auto path = iterator->path();
      if (path.extension() != kDictionaryExtension) {
        continue;
      }
      const std::string project_id = path.stem().string();
      if (IsValidProjectId(project_id)) {
        project_ids.push_back(project_id);
      }
    }
    if (error) {
      return ProjectDictionaryStatus::IoError;
    }
    std::sort(project_ids.begin(), project_ids.end());
    return ProjectDictionaryStatus::Ok;
  } catch (...) {
    project_ids.clear();
    return ProjectDictionaryStatus::IoError;
  }
}

ProjectDictionaryStatus ProjectDictionaryStore::Save(
    const std::string& project_id,
    const ProjectDictionarySnapshot& snapshot) const noexcept {
  if (!IsValidProjectId(project_id)) {
    return ProjectDictionaryStatus::InvalidProjectId;
  }
  if (snapshot.entries.size() > kMaximumProjectDictionaryEntries ||
      std::any_of(snapshot.entries.begin(), snapshot.entries.end(),
                  [](const auto& entry) {
                    return !IsValidProjectDictionaryEntry(entry);
                  })) {
    return ProjectDictionaryStatus::InvalidEntry;
  }

  try {
    std::error_code error;
    std::filesystem::create_directories(root_directory_, error);
    if (error) {
      return ProjectDictionaryStatus::IoError;
    }
    const std::filesystem::path target = DictionaryPath(project_id);
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    {
      std::ofstream output(temporary,
                           std::ios::binary | std::ios::trunc);
      if (!output) {
        return ProjectDictionaryStatus::IoError;
      }
      output << kDictionaryHeader << '\t' << kDictionaryVersion << '\t'
             << (snapshot.enabled ? '1' : '0') << '\n';
      for (const auto& entry : snapshot.entries) {
        output << ToString(entry.symbol_type) << '\t' << entry.frequency
               << '\t' << entry.last_seen_ms << '\t'
               << ToString(entry.source) << '\t' << HexEncode(entry.symbol)
               << '\n';
      }
      output.flush();
      if (!output) {
        output.close();
        std::filesystem::remove(temporary, error);
        return ProjectDictionaryStatus::IoError;
      }
    }
    return ReplaceFile(temporary, target);
  } catch (...) {
    return ProjectDictionaryStatus::IoError;
  }
}

std::filesystem::path ProjectDictionaryStore::DictionaryPath(
    const std::string& project_id) const {
  return root_directory_ / (project_id + kDictionaryExtension);
}

bool IsValidProjectId(const std::string& project_id) noexcept {
  return project_id.size() == kProjectIdLength &&
         std::all_of(project_id.begin(), project_id.end(), [](char value) {
           return (value >= '0' && value <= '9') ||
                  (value >= 'a' && value <= 'f');
         });
}

bool IsValidProjectDictionaryEntry(
    const ProjectDictionaryEntry& entry) noexcept {
  if (!IsKnown(entry.symbol_type) || !IsKnown(entry.source) ||
      entry.frequency == 0 || entry.symbol.empty() ||
      entry.symbol.size() > kMaximumProjectSymbolBytes ||
      !IsValidUtf8(entry.symbol)) {
    return false;
  }
  return std::none_of(entry.symbol.begin(), entry.symbol.end(), [](char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return byte == 0 || byte == '\t' || byte == '\n' || byte == '\r' ||
           byte == '\\' || byte == '/' || byte == ':' || byte == 0x7fu ||
           byte < 0x20u;
  });
}

const char* ToString(ProjectSymbolType type) noexcept {
  switch (type) {
    case ProjectSymbolType::Class: return "class";
    case ProjectSymbolType::Method: return "method";
    case ProjectSymbolType::Property: return "property";
    case ProjectSymbolType::Enum: return "enum";
    case ProjectSymbolType::Namespace: return "namespace";
    case ProjectSymbolType::File: return "file";
    case ProjectSymbolType::Directory: return "directory";
    case ProjectSymbolType::Asset: return "asset";
    case ProjectSymbolType::Shader: return "shader";
    case ProjectSymbolType::Term: return "term";
  }
  return "unknown";
}

const char* ToString(ProjectSymbolSource source) noexcept {
  switch (source) {
    case ProjectSymbolSource::LanguageServer: return "language_server";
    case ProjectSymbolSource::CompilationDatabase:
      return "compilation_database";
    case ProjectSymbolSource::ProjectFile: return "project_file";
    case ProjectSymbolSource::FileSystem: return "file_system";
    case ProjectSymbolSource::Manual: return "manual";
  }
  return "unknown";
}

const char* ToString(ProjectDictionaryStatus status) noexcept {
  switch (status) {
    case ProjectDictionaryStatus::Ok: return "OK";
    case ProjectDictionaryStatus::InvalidProjectId:
      return "INVALID_PROJECT_ID";
    case ProjectDictionaryStatus::InvalidEntry: return "INVALID_ENTRY";
    case ProjectDictionaryStatus::CorruptData: return "CORRUPT_DATA";
    case ProjectDictionaryStatus::IoError: return "IO_ERROR";
  }
  return "UNKNOWN";
}

}  // namespace contextime
