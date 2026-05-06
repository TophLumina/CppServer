#pragma once

#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <httplib.h>

#include "Context.h"
#include "MappedFileCache.h"
#include "MountPathWatcher.h"

namespace CppServer::Services::Files {
namespace Detail {
inline std::string DetectDirectoryEntryFile(
    const std::filesystem::path &directory_path) {
  namespace fs = std::filesystem;

  for (const char *entry_name : {"index.html", "index.htm"}) {
    std::error_code error;
    const fs::path entry_path = directory_path / entry_name;
    if (fs::is_regular_file(entry_path, error) && !error) {
      return entry_name;
    }
  }

  return "";
}

inline std::optional<std::filesystem::path>
ResolveMountedRequestPath(const std::string &resolved_mount_path,
                          const std::string &request_path) {
  namespace fs = std::filesystem;

  if (request_path.empty() || request_path.front() != '/') {
    return std::nullopt;
  }

  fs::path relative_path;
  const std::string normalized_request = request_path.substr(1);
  if (!normalized_request.empty()) {
    relative_path = fs::path(normalized_request);
    for (const auto &segment : relative_path) {
      if (segment == "..") {
        return std::nullopt;
      }
    }
  }

  const fs::path mount_root = fs::path(resolved_mount_path).lexically_normal();
  const fs::path target_path = (mount_root / relative_path).lexically_normal();

  std::error_code error;
  const fs::path canonical_mount_root = fs::weakly_canonical(mount_root, error);
  if (error) {
    return std::nullopt;
  }

  error.clear();
  const fs::path canonical_target_path = fs::weakly_canonical(target_path, error);
  if (error) {
    return std::nullopt;
  }

  const auto path_component_equals = [](const fs::path &left,
                                        const fs::path &right) {
#ifdef _WIN32
    std::wstring left_value = left.native();
    std::wstring right_value = right.native();
    if (left_value.size() != right_value.size()) {
      return false;
    }

    for (std::size_t index = 0; index < left_value.size(); ++index) {
      if (std::towlower(left_value[index]) != std::towlower(right_value[index])) {
        return false;
      }
    }
    return true;
#else
    return left == right;
#endif
  };

  auto mount_it = canonical_mount_root.begin();
  auto target_it = canonical_target_path.begin();
  for (; mount_it != canonical_mount_root.end(); ++mount_it, ++target_it) {
    if (target_it == canonical_target_path.end() ||
        !path_component_equals(*mount_it, *target_it)) {
      return std::nullopt;
    }
  }

  return canonical_target_path;
}

inline bool ServeStaticFile(const std::filesystem::path &file_path,
                            MappedFileCache &mapped_file_cache,
                            const httplib::Request &req,
                            httplib::Response &res) {
  auto mapped_file = mapped_file_cache.GetOrOpen(file_path);
  if (mapped_file == nullptr || !mapped_file->is_open()) {
    res.status = 404;
    return true;
  }

  const std::string content_type = httplib::detail::find_content_type(
      file_path.string(), {}, "application/octet-stream");
  if (req.method == "HEAD") {
    res.set_header("Content-Type", content_type);
    res.set_header("Content-Length", std::to_string(mapped_file->size()));
    return true;
  }

  res.set_content_provider(
      mapped_file->size(), content_type,
      [mapped_file](size_t offset, size_t length,
                    httplib::DataSink &sink) -> bool {
        sink.write(mapped_file->data() + offset, length);
        return true;
      });
  return true;
}

inline bool TryServeMountedRequest(const httplib::Request &req,
                                   httplib::Response &res,
                                   MappedFileCache &mapped_file_cache,
                                   const std::string &resolved_mount_path) {
  namespace fs = std::filesystem;

  const auto target_path =
      ResolveMountedRequestPath(resolved_mount_path, req.path);
  if (!target_path.has_value()) {
    return false;
  }

  std::error_code error;
  if (fs::is_directory(*target_path, error) && !error) {
    if (req.path.empty() || req.path.back() != '/') {
      res.set_redirect((req.path + "/").c_str());
      return true;
    }

    const std::string index_entry = DetectDirectoryEntryFile(*target_path);
    if (index_entry.empty()) {
      return false;
    }

    return ServeStaticFile(*target_path / index_entry, mapped_file_cache, req,
                           res);
  }

  error.clear();
  if (fs::is_regular_file(*target_path, error) && !error) {
    return ServeStaticFile(*target_path, mapped_file_cache, req, res);
  }

  return false;
}

inline std::shared_ptr<MountPathWatcher> EnsureMountWatcher(Context &file_context) {
  if (file_context.mount_watcher != nullptr) {
    return file_context.mount_watcher;
  }

  file_context.mount_watcher =
      std::make_shared<MountPathWatcher>(file_context.mount_path);
  return file_context.mount_watcher;
}

inline std::shared_ptr<MappedFileCache> EnsureMappedFileCache(
    Context &file_context) {
  if (file_context.mapped_file_cache != nullptr) {
    return file_context.mapped_file_cache;
  }

  file_context.mapped_file_cache = std::make_shared<MappedFileCache>();
  return file_context.mapped_file_cache;
}
} // namespace Detail

inline void ConfigureRuntime(httplib::Server &server, Context &file_context) {
  auto mount_watcher = Detail::EnsureMountWatcher(file_context);
  auto mapped_file_cache = Detail::EnsureMappedFileCache(file_context);

  server.set_pre_routing_handler(
      [mount_watcher = std::move(mount_watcher),
       mapped_file_cache = std::move(mapped_file_cache)](
          const httplib::Request &req,
          httplib::Response &res) -> httplib::Server::HandlerResponse {
        if (req.method != "GET" && req.method != "HEAD") {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        const std::string resolved_mount_path =
            mount_watcher->ResolvedMountPath();

        if (resolved_mount_path.empty()) {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        if (Detail::TryServeMountedRequest(req, res, *mapped_file_cache,
                                          resolved_mount_path)) {
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });
}
} // namespace CppServer::Services::Files