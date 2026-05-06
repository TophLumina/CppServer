#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "MappedFile.h"

namespace CppServer::Services::Files {
class MappedFileCache {
public:
  std::shared_ptr<MappedFile>
  GetOrOpen(const std::filesystem::path &file_path) {
    const std::string path_key = file_path.lexically_normal().string();
    const auto metadata = ReadMetadata(file_path);
    if (!metadata.has_value()) {
      Erase(path_key);
      return nullptr;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto existing = entries_.find(path_key);
      if (existing != entries_.end() && MetadataMatches(existing->second, *metadata) &&
          existing->second.mapped_file != nullptr &&
          existing->second.mapped_file->is_open()) {
        existing->second.last_access_token = ++access_token_;
        return existing->second.mapped_file;
      }
    }

    auto mapped_file = std::make_shared<MappedFile>(file_path);
    if (!mapped_file->is_open()) {
      Erase(path_key);
      return nullptr;
    }

    const auto refreshed_metadata = ReadMetadata(file_path);
    if (!refreshed_metadata.has_value()) {
      return mapped_file;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto &entry = entries_[path_key];
    entry.last_write_time = refreshed_metadata->last_write_time;
    entry.file_size = refreshed_metadata->file_size;
    entry.last_access_token = ++access_token_;
    entry.mapped_file = mapped_file;
    EvictIfNeededLocked();
    return mapped_file;
  }

private:
  struct FileMetadata {
    std::filesystem::file_time_type last_write_time{};
    std::uintmax_t file_size = 0;
  };

  struct CacheEntry {
    std::filesystem::file_time_type last_write_time{};
    std::uintmax_t file_size = 0;
    std::size_t last_access_token = 0;
    std::shared_ptr<MappedFile> mapped_file;
  };

  static std::optional<FileMetadata>
  ReadMetadata(const std::filesystem::path &file_path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file_path, error) || error) {
      return std::nullopt;
    }

    const auto last_write_time = std::filesystem::last_write_time(file_path, error);
    if (error) {
      return std::nullopt;
    }

    const auto file_size = std::filesystem::file_size(file_path, error);
    if (error) {
      return std::nullopt;
    }

    return FileMetadata{last_write_time, file_size};
  }

  static bool MetadataMatches(const CacheEntry &entry,
                              const FileMetadata &metadata) {
    return entry.file_size == metadata.file_size &&
           entry.last_write_time == metadata.last_write_time;
  }

  void Erase(const std::string &path_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(path_key);
  }

  void EvictIfNeededLocked() {
    while (entries_.size() > kMaxEntries) {
      auto oldest = entries_.begin();
      for (auto current = entries_.begin(); current != entries_.end(); ++current) {
        if (current->second.last_access_token < oldest->second.last_access_token) {
          oldest = current;
        }
      }
      entries_.erase(oldest);
    }
  }

  static constexpr std::size_t kMaxEntries = 256;

  std::mutex mutex_;
  std::unordered_map<std::string, CacheEntry> entries_;
  std::size_t access_token_ = 0;
};
} // namespace CppServer::Services::Files