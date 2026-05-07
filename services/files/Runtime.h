#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <httplib.h>

#include "RuntimeAssets.h"
#include "Context.h"
#include "MappedFileCache.h"
#include "MountPathWatcher.h"

namespace CppServer::Services::Files {
namespace Detail {
inline std::string ToUtf8String(const std::filesystem::path &path) {
  const auto utf8_value = path.generic_u8string();
  std::string result;
  result.reserve(utf8_value.size());
  for (const auto character : utf8_value) {
    result.push_back(static_cast<char>(character));
  }
  return result;
}

inline bool PathComponentEquals(const std::filesystem::path &left,
                                const std::filesystem::path &right) {
#ifdef _WIN32
  std::wstring left_value = left.native();
  std::wstring right_value = right.native();
  if (left_value.size() != right_value.size()) {
    return false;
  }

  for (std::size_t index = 0; index < left_value.size(); ++index) {
    if (std::towlower(left_value[index]) !=
        std::towlower(right_value[index])) {
      return false;
    }
  }
  return true;
#else
  return left == right;
#endif
}

inline bool IsPathWithinMountRoot(
    const std::filesystem::path &canonical_mount_root,
    const std::filesystem::path &canonical_target_path) {
  auto mount_it = canonical_mount_root.begin();
  auto target_it = canonical_target_path.begin();
  for (; mount_it != canonical_mount_root.end(); ++mount_it, ++target_it) {
    if (target_it == canonical_target_path.end() ||
        !PathComponentEquals(*mount_it, *target_it)) {
      return false;
    }
  }

  return true;
}

inline std::optional<std::filesystem::path>
CanonicalizeContainedPath(const std::filesystem::path &mount_root,
                          const std::filesystem::path &target_path) {
  namespace fs = std::filesystem;

  std::error_code error;
  const fs::path canonical_mount_root = fs::weakly_canonical(mount_root, error);
  if (error) {
    return std::nullopt;
  }

  error.clear();
  const fs::path canonical_target_path = fs::weakly_canonical(target_path, error);
  if (error ||
      !IsPathWithinMountRoot(canonical_mount_root, canonical_target_path)) {
    return std::nullopt;
  }

  return canonical_target_path;
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

  return CanonicalizeContainedPath(mount_root, target_path);
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

  res.set_content_provider(mapped_file->size(), content_type,
                           [mapped_file](size_t offset, size_t length,
                                         httplib::DataSink &sink) -> bool {
                             sink.write(mapped_file->data() + offset, length);
                             return true;
                           });
  return true;
}

inline std::string EscapeHtml(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&#39;";
      break;
    default:
      escaped.push_back(character);
      break;
    }
  }
  return escaped;
}

inline std::string UrlEncodeSegment(const std::string &segment) {
  static constexpr char hex_digits[] = "0123456789ABCDEF";

  std::string encoded;
  encoded.reserve(segment.size());
  for (const unsigned char character : segment) {
    if ((character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.' || character == '~') {
      encoded.push_back(static_cast<char>(character));
      continue;
    }

    encoded.push_back('%');
    encoded.push_back(hex_digits[(character >> 4) & 0x0F]);
    encoded.push_back(hex_digits[character & 0x0F]);
  }

  return encoded;
}

inline std::string BuildRequestHref(const std::filesystem::path &relative_path,
                                    const bool is_directory) {
  if (relative_path.empty() || relative_path == ".") {
    return "/";
  }

  std::string href = "/";
  bool first_segment = true;
  for (const auto &segment : relative_path) {
    if (segment.empty() || segment == ".") {
      continue;
    }

    if (!first_segment) {
      href.push_back('/');
    }
    href += UrlEncodeSegment(ToUtf8String(segment));
    first_segment = false;
  }

  if (is_directory && !href.empty() && href.back() != '/') {
    href.push_back('/');
  }

  return href;
}

inline void ReplaceAll(std::string &value, const std::string &needle,
                       const std::string &replacement) {
  std::size_t position = 0;
  while ((position = value.find(needle, position)) != std::string::npos) {
    value.replace(position, needle.size(), replacement);
    position += replacement.size();
  }
}

inline std::string LoadTextFile(const std::filesystem::path &file_path,
                                MappedFileCache &mapped_file_cache) {
  const auto mapped_file = mapped_file_cache.GetOrOpen(file_path);
  if (mapped_file == nullptr || !mapped_file->is_open()) {
    return "";
  }

  return std::string(mapped_file->data(), mapped_file->size());
}

inline std::string
LoadDirectoryListingTemplate(const std::string &template_path,
                             MappedFileCache &mapped_file_cache) {
  const auto resolved_template_path = CppServer::RuntimeAssets::FindExistingPath(
      template_path, CppServer::RuntimeAssets::PathKind::File);
  if (resolved_template_path.has_value()) {
    const std::string file_content =
        LoadTextFile(*resolved_template_path, mapped_file_cache);
    if (!file_content.empty()) {
      return file_content;
    }
  }

  return "";
}

inline std::string BuildPathLabel(const std::filesystem::path &relative_path) {
  if (relative_path.empty() || relative_path == ".") {
    return "/";
  }

  return "/" + ToUtf8String(relative_path) + "/";
}

inline std::string BuildBreadcrumbsHtml(
    const std::filesystem::path &relative_path) {
  std::ostringstream html;
  html << "<a href=\"/\">root</a>";

  if (relative_path.empty() || relative_path == ".") {
    return html.str();
  }

  std::filesystem::path current;
  for (const auto &segment : relative_path) {
    if (segment.empty() || segment == ".") {
      continue;
    }

    current /= segment;
    html << "<span>/</span><a href=\""
         << EscapeHtml(BuildRequestHref(current, true)) << "\">"
         << EscapeHtml(ToUtf8String(segment)) << "</a>";
  }

  return html.str();
}

struct DirectoryListingEntry {
  std::string name;
  std::string href;
  bool is_directory = false;
  bool is_parent = false;
};

inline std::string BuildDirectoryEntriesHtml(
    const std::filesystem::path &mount_root,
    const std::filesystem::path &directory_path,
    const std::filesystem::path &relative_path) {
  namespace fs = std::filesystem;

  std::vector<DirectoryListingEntry> entries;
  if (!relative_path.empty() && relative_path != ".") {
    entries.push_back(DirectoryListingEntry{"..", BuildRequestHref(
                                                    relative_path.parent_path(),
                                                    true),
                                            true, true});
  }

  std::error_code error;
  fs::directory_iterator iterator(directory_path,
                                  fs::directory_options::skip_permission_denied,
                                  error);
  if (!error) {
    for (const auto &entry : iterator) {
      const auto contained_path =
          CanonicalizeContainedPath(mount_root, entry.path());
      if (!contained_path.has_value()) {
        continue;
      }

      error.clear();
      const bool is_directory =
          fs::is_directory(*contained_path, error) && !error;
      error.clear();
      const bool is_regular_file =
          fs::is_regular_file(*contained_path, error) && !error;
      if (!is_directory && !is_regular_file) {
        continue;
      }

      const fs::path child_name = entry.path().filename();
      const fs::path child_relative =
          relative_path.empty() || relative_path == "."
              ? fs::path(child_name)
              : relative_path / child_name;
      entries.push_back(DirectoryListingEntry{ToUtf8String(child_name),
                                              BuildRequestHref(child_relative,
                                                               is_directory),
                                              is_directory, false});
    }
  }

  std::stable_sort(entries.begin(), entries.end(),
                   [](const DirectoryListingEntry &left,
                      const DirectoryListingEntry &right) {
                     if (left.is_parent != right.is_parent) {
                       return left.is_parent;
                     }
                     if (left.is_directory != right.is_directory) {
                       return left.is_directory;
                     }

                     std::string left_name = left.name;
                     std::string right_name = right.name;
                     std::transform(left_name.begin(), left_name.end(),
                                    left_name.begin(),
                                    [](unsigned char character) {
                                      return static_cast<char>(
                                          std::tolower(character));
                                    });
                     std::transform(right_name.begin(), right_name.end(),
                                    right_name.begin(),
                                    [](unsigned char character) {
                                      return static_cast<char>(
                                          std::tolower(character));
                                    });
                     return left_name < right_name;
                   });

  if (entries.empty()) {
    return "<li class=\"empty\">This directory is empty.</li>";
  }

  std::ostringstream html;
  for (const auto &entry : entries) {
    const std::string display_name = entry.is_directory && !entry.is_parent
                                         ? entry.name + "/"
                                         : entry.name;
    html << "<li class=\"entry "
         << (entry.is_directory ? "entry-directory" : "entry-file")
         << "\"><a href=\"" << EscapeHtml(entry.href)
         << "\"><span class=\"label\"><span class=\"name\">"
         << EscapeHtml(display_name)
         << "</span></span><span class=\"kind\">"
         << (entry.is_directory ? (entry.is_parent ? "parent" : "folder")
                                : "file")
         << "</span></a></li>";
  }

  return html.str();
}

inline std::string BuildDirectoryListingHtml(
    const std::filesystem::path &mount_root,
    const std::filesystem::path &directory_path,
  const std::string &directory_listing_template_path,
    MappedFileCache &mapped_file_cache) {
  namespace fs = std::filesystem;

  std::error_code error;
  fs::path relative_path = fs::relative(directory_path, mount_root, error);
  if (error) {
    relative_path.clear();
  }

  std::string html =
      LoadDirectoryListingTemplate(directory_listing_template_path,
                                   mapped_file_cache);
  if (html.empty()) {
    return "";
  }

  ReplaceAll(html, "{{TITLE}}",
             EscapeHtml("Index of " + BuildPathLabel(relative_path)));
  ReplaceAll(html, "{{CURRENT_PATH}}", EscapeHtml(BuildPathLabel(relative_path)));
  ReplaceAll(html, "{{BREADCRUMBS}}",
             BuildBreadcrumbsHtml(relative_path));
  ReplaceAll(html, "{{ENTRIES}}",
             BuildDirectoryEntriesHtml(mount_root, directory_path,
                                       relative_path));
  return html;
}

inline bool ServeDirectoryListing(const std::filesystem::path &mount_root,
                                  const std::filesystem::path &directory_path,
                                  const std::string &directory_listing_template_path,
                                  MappedFileCache &mapped_file_cache,
                                  const httplib::Request &req,
                                  httplib::Response &res) {
  const std::string html = BuildDirectoryListingHtml(
      mount_root, directory_path, directory_listing_template_path,
      mapped_file_cache);
  if (html.empty()) {
    res.status = 500;
    res.set_content("Directory listing template not found: " +
                        directory_listing_template_path,
                    "text/plain; charset=utf-8");
    return true;
  }

  if (req.method == "HEAD") {
    res.set_header("Content-Type", "text/html; charset=utf-8");
    res.set_header("Content-Length", std::to_string(html.size()));
    return true;
  }

  res.set_content(html, "text/html; charset=utf-8");
  return true;
}

inline bool TryServeMountedRequest(const httplib::Request &req,
                                   httplib::Response &res,
                                   const std::string &directory_listing_template_path,
                                   MappedFileCache &mapped_file_cache,
                                   const std::string &resolved_mount_path) {
  namespace fs = std::filesystem;
  const fs::path mount_root = fs::path(resolved_mount_path).lexically_normal();

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

    const auto directory_index_path =
        CanonicalizeContainedPath(mount_root, *target_path / "index.html");
    if (directory_index_path.has_value()) {
      error.clear();
      if (fs::is_regular_file(*directory_index_path, error) && !error) {
        return ServeStaticFile(*directory_index_path, mapped_file_cache, req,
                               res);
      }
    }

    return ServeDirectoryListing(mount_root, *target_path,
                   directory_listing_template_path,
                   mapped_file_cache, req, res);
  }

  error.clear();
  if (fs::is_regular_file(*target_path, error) && !error) {
    return ServeStaticFile(*target_path, mapped_file_cache, req, res);
  }

  return false;
}

inline std::shared_ptr<MountPathWatcher>
EnsureMountWatcher(Context &file_context) {
  if (file_context.mount_watcher != nullptr) {
    return file_context.mount_watcher;
  }

  file_context.mount_watcher =
      std::make_shared<MountPathWatcher>(file_context.mount_path);
  return file_context.mount_watcher;
}

inline std::shared_ptr<MappedFileCache>
EnsureMappedFileCache(Context &file_context) {
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
  const std::string directory_listing_template_path =
      file_context.directory_listing_template_path;

  server.set_pre_routing_handler(
      [mount_watcher = std::move(mount_watcher),
       mapped_file_cache = std::move(mapped_file_cache),
       directory_listing_template_path =
           std::move(directory_listing_template_path)](
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

        if (Detail::TryServeMountedRequest(req, res,
                                           directory_listing_template_path,
                                           *mapped_file_cache,
                                           resolved_mount_path)) {
          return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
      });
}
} // namespace CppServer::Services::Files