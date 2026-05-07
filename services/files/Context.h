#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace CppServer::Services::Files {
class MountPathWatcher;
class MappedFileCache;

struct Context {
  static constexpr int DEFAULT_PORT_BEGIN = 8081;
  static constexpr std::size_t DEFAULT_PORT_COUNT = 1;
  inline static constexpr const char *DEFAULT_MOUNT_PATH = "mount";
  inline static constexpr const char *DEFAULT_DIRECTORY_LISTING_TEMPLATE_PATH =
      "resources/directory-index-template.html";

  explicit Context(int listening_port = DEFAULT_PORT_BEGIN,
                   std::string configured_mount_path = DEFAULT_MOUNT_PATH,
             std::string configured_directory_listing_template_path =
               DEFAULT_DIRECTORY_LISTING_TEMPLATE_PATH)
      : port(listening_port), mount_path(std::move(configured_mount_path)),
      directory_listing_template_path(
        std::move(configured_directory_listing_template_path)) {}

  int port;
  std::string mount_path;
    std::string directory_listing_template_path;
  std::shared_ptr<MountPathWatcher> mount_watcher;
  std::shared_ptr<MappedFileCache> mapped_file_cache;
};
} // namespace CppServer::Services::Files
