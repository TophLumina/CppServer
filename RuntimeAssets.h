#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CppServer::RuntimeAssets {
enum class PathKind { Any, File, Directory };

inline std::filesystem::path NormalizePath(const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::path absolute_path =
      std::filesystem::absolute(path, error);
  if (!error) {
    return absolute_path.lexically_normal();
  }

  return path.lexically_normal();
}

inline std::vector<std::filesystem::path> SearchRoots() {
  const std::filesystem::path current_directory =
      NormalizePath(std::filesystem::current_path());
  std::vector<std::filesystem::path> roots;
  roots.push_back(current_directory);

  const std::filesystem::path parent_directory = current_directory.parent_path();
  if (!parent_directory.empty() && parent_directory != current_directory) {
    roots.push_back(parent_directory);
  }

  return roots;
}

inline bool MatchesPathKind(const std::filesystem::path &path,
                            const PathKind kind) {
  std::error_code error;
  switch (kind) {
  case PathKind::Any:
    return std::filesystem::exists(path, error) && !error;
  case PathKind::File:
    return std::filesystem::is_regular_file(path, error) && !error;
  case PathKind::Directory:
    return std::filesystem::is_directory(path, error) && !error;
  }

  return false;
}

inline std::optional<std::filesystem::path>
FindExistingPath(const std::filesystem::path &configured_path,
                 const PathKind kind = PathKind::Any) {
  if (configured_path.is_absolute()) {
    const std::filesystem::path normalized_path = NormalizePath(configured_path);
    if (MatchesPathKind(normalized_path, kind)) {
      return normalized_path;
    }
    return std::nullopt;
  }

  for (const std::filesystem::path &root : SearchRoots()) {
    const std::filesystem::path candidate = root / configured_path;
    if (MatchesPathKind(candidate, kind)) {
      return NormalizePath(candidate);
    }
  }

  return std::nullopt;
}

inline std::filesystem::path
ResolvePath(const std::filesystem::path &configured_path) {
  if (configured_path.is_absolute()) {
    return NormalizePath(configured_path);
  }

  const std::vector<std::filesystem::path> search_roots = SearchRoots();
  if (!search_roots.empty()) {
    return NormalizePath(search_roots.front() / configured_path);
  }

  return NormalizePath(configured_path);
}
} // namespace CppServer::RuntimeAssets