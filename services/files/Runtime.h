#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <httplib.h>

#include "RuntimeAssets.h"
#include "Context.h"

namespace CppServer::Services::Files {
namespace Detail {
struct MountSnapshot {
  std::string resolved_mount_path;
  std::filesystem::file_time_type latest_write_time{};
  std::size_t entry_count = 0;
};

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

inline std::optional<MountSnapshot>
BuildMountSnapshot(const std::string &configured_mount_path) {
  namespace fs = std::filesystem;

  const auto resolved_mount_path =
      CppServer::RuntimeAssets::FindExistingPath(
          configured_mount_path, CppServer::RuntimeAssets::PathKind::Directory);
  if (!resolved_mount_path.has_value()) {
    return std::nullopt;
  }

  std::error_code error;
  const fs::path mount_root(*resolved_mount_path);
  MountSnapshot snapshot;
  snapshot.resolved_mount_path = resolved_mount_path->string();
  snapshot.entry_count = 1;
  snapshot.latest_write_time = fs::last_write_time(mount_root, error);
  if (error) {
    snapshot.latest_write_time = std::filesystem::file_time_type::min();
    error.clear();
  }

  fs::recursive_directory_iterator entry_it(
      mount_root, fs::directory_options::skip_permission_denied, error);
  const fs::recursive_directory_iterator end_it;
  while (!error && entry_it != end_it) {
    ++snapshot.entry_count;

    std::error_code entry_error;
    const auto entry_write_time =
        fs::last_write_time(entry_it->path(), entry_error);
    if (!entry_error && entry_write_time > snapshot.latest_write_time) {
      snapshot.latest_write_time = entry_write_time;
    }

    entry_it.increment(error);
  }

  return snapshot;
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

  return (fs::path(resolved_mount_path) / relative_path).lexically_normal();
}

inline bool ServeStaticFile(const std::filesystem::path &file_path,
                            const httplib::Request &req,
                            httplib::Response &res) {
  auto mapped_file =
      std::make_shared<httplib::detail::mmap>(file_path.string().c_str());
  if (!mapped_file->is_open()) {
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

    return ServeStaticFile(*target_path / index_entry, req, res);
  }

  error.clear();
  if (fs::is_regular_file(*target_path, error) && !error) {
    return ServeStaticFile(*target_path, req, res);
  }

  return false;
}
} // namespace Detail

inline void RefreshStateIfChanged(Context &file_context) {
  const auto snapshot = Detail::BuildMountSnapshot(file_context.mount_path);

  std::lock_guard<std::mutex> lock(file_context.mount_state_mutex);
  if (!snapshot.has_value()) {
    file_context.mount_state_initialized = false;
    file_context.resolved_mount_path.clear();
    file_context.entry_count = 0;
    file_context.latest_write_time = std::filesystem::file_time_type{};
    return;
  }

  const bool state_changed =
      !file_context.mount_state_initialized ||
      file_context.resolved_mount_path != snapshot->resolved_mount_path ||
      file_context.entry_count != snapshot->entry_count ||
      file_context.latest_write_time != snapshot->latest_write_time;
  if (!state_changed) {
    return;
  }

  file_context.mount_state_initialized = true;
  file_context.resolved_mount_path = snapshot->resolved_mount_path;
  file_context.entry_count = snapshot->entry_count;
  file_context.latest_write_time = snapshot->latest_write_time;
}

inline void ConfigureRuntime(httplib::Server &server, Context &file_context) {
  server.set_pre_routing_handler(
      [&file_context](const httplib::Request &req,
                      httplib::Response &res) -> httplib::Server::HandlerResponse {
        if (req.method != "GET" && req.method != "HEAD") {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        RefreshStateIfChanged(file_context);

        std::string resolved_mount_path;
        {
          std::lock_guard<std::mutex> lock(file_context.mount_state_mutex);
          resolved_mount_path = file_context.resolved_mount_path;
        }

        if (resolved_mount_path.empty()) {
          return httplib::Server::HandlerResponse::Unhandled;
        }

        if (Detail::TryServeMountedRequest(req, res, resolved_mount_path)) {
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });
}
} // namespace CppServer::Services::Files