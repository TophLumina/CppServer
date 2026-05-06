#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "RuntimeAssets.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#include <cstdint>
#if defined(__linux__)
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef O_EVTONLY
#define O_EVTONLY O_RDONLY
#endif
#endif
#endif

namespace CppServer::Services::Files {
class MountPathWatcher {
public:
  explicit MountPathWatcher(std::string configured_mount_path)
      : configured_mount_path_(std::move(configured_mount_path)) {
    RefreshResolvedMountPath();
#ifdef _WIN32
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
    RebuildWatchSet();
    worker_ = std::thread([this] { WatchLoop(); });
  }

  ~MountPathWatcher() { Stop(); }

  MountPathWatcher(const MountPathWatcher &) = delete;
  MountPathWatcher &operator=(const MountPathWatcher &) = delete;
  MountPathWatcher(MountPathWatcher &&) = delete;
  MountPathWatcher &operator=(MountPathWatcher &&) = delete;

  void Stop() {
    if (stop_requested_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }

#ifdef _WIN32
    if (stop_event_ != nullptr) {
      SetEvent(stop_event_);
    }
#endif

    if (worker_.joinable()) {
      worker_.join();
    }

    CloseWatchSet();

#ifdef _WIN32
    if (stop_event_ != nullptr) {
      CloseHandle(stop_event_);
      stop_event_ = nullptr;
    }
#endif
  }

  std::string ResolvedMountPath() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return resolved_mount_path_;
  }

private:
  enum class WaitStatus { Timeout, Event, Stop };

  void RefreshResolvedMountPath() {
    const auto resolved_mount_path = CppServer::RuntimeAssets::FindExistingPath(
        configured_mount_path_, CppServer::RuntimeAssets::PathKind::Directory);

    std::lock_guard<std::mutex> lock(state_mutex_);
    resolved_mount_path_ =
        resolved_mount_path.has_value() ? resolved_mount_path->string() : "";
  }

  std::vector<std::filesystem::path> BuildWatchDirectories() const {
    namespace fs = std::filesystem;

    std::vector<fs::path> watch_directories;
    std::unordered_set<std::string> seen_directories;

    const auto add_directory = [&](const fs::path &directory_path) {
      std::error_code error;
      if (!fs::is_directory(directory_path, error) || error) {
        return;
      }

      const fs::path normalized_path =
          CppServer::RuntimeAssets::NormalizePath(directory_path);
      if (seen_directories.emplace(normalized_path.string()).second) {
        watch_directories.push_back(normalized_path);
      }
    };

    const auto add_relative_chain = [&](const fs::path &base_path,
                                        const fs::path &relative_path) {
      fs::path current = CppServer::RuntimeAssets::NormalizePath(base_path);
      add_directory(current);

      std::vector<fs::path> segments;
      for (const auto &segment : relative_path) {
        if (segment.empty() || segment == ".") {
          continue;
        }
        segments.push_back(segment);
      }

      for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        current /= segments[index];

        std::error_code error;
        if (!fs::is_directory(current, error) || error) {
          break;
        }

        add_directory(current);
      }
    };

    const fs::path configured_path(configured_mount_path_);
    if (configured_path.is_absolute()) {
      fs::path current = configured_path.root_path();
      if (!current.empty()) {
        add_directory(current);
      }

      std::vector<fs::path> segments;
      for (const auto &segment : configured_path.relative_path()) {
        if (segment.empty() || segment == ".") {
          continue;
        }
        segments.push_back(segment);
      }

      for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        current /= segments[index];

        std::error_code error;
        if (!fs::is_directory(current, error) || error) {
          break;
        }

        add_directory(current);
      }

      return watch_directories;
    }

    for (const auto &search_root : CppServer::RuntimeAssets::SearchRoots()) {
      add_relative_chain(search_root, configured_path);
    }

    return watch_directories;
  }

  void RefreshStateAndWatchSet() {
    RefreshResolvedMountPath();
    RebuildWatchSet();
  }

  void WatchLoop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
      switch (WaitForEvent()) {
      case WaitStatus::Timeout:
        continue;
      case WaitStatus::Event:
        RefreshStateAndWatchSet();
        break;
      case WaitStatus::Stop:
        return;
      }
    }
  }

#ifdef _WIN32
  struct ChangeHandle {
    std::filesystem::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
  };

  void CloseWatchSet() {
    for (auto &watch_handle : watch_handles_) {
      if (watch_handle.handle != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(watch_handle.handle);
      }
    }
    watch_handles_.clear();
  }

  void RebuildWatchSet() {
    CloseWatchSet();

    constexpr DWORD notify_filter = FILE_NOTIFY_CHANGE_DIR_NAME |
                                    FILE_NOTIFY_CHANGE_FILE_NAME |
                                    FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                    FILE_NOTIFY_CHANGE_CREATION |
                                    FILE_NOTIFY_CHANGE_LAST_WRITE;

    for (const auto &watch_directory : BuildWatchDirectories()) {
      const HANDLE handle = FindFirstChangeNotificationW(
          watch_directory.c_str(), FALSE, notify_filter);
      if (handle == INVALID_HANDLE_VALUE) {
        continue;
      }

      watch_handles_.push_back(ChangeHandle{watch_directory, handle});
    }
  }

  WaitStatus WaitForEvent() {
    if (stop_event_ == nullptr) {
      return WaitStatus::Stop;
    }

    std::vector<HANDLE> handles;
    handles.reserve(1 + watch_handles_.size());
    handles.push_back(stop_event_);
    for (const auto &watch_handle : watch_handles_) {
      if (watch_handle.handle != INVALID_HANDLE_VALUE) {
        handles.push_back(watch_handle.handle);
      }
    }

    const DWORD timeout_ms = watch_handles_.empty() ? 250 : INFINITE;
    const DWORD result = WaitForMultipleObjects(
        static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout_ms);
    if (result == WAIT_TIMEOUT) {
      return WaitStatus::Timeout;
    }
    if (result == WAIT_OBJECT_0) {
      return WaitStatus::Stop;
    }

    const std::size_t watch_index =
        static_cast<std::size_t>(result - WAIT_OBJECT_0 - 1);
    if (watch_index >= watch_handles_.size()) {
      return WaitStatus::Event;
    }

    if (watch_handles_[watch_index].handle != INVALID_HANDLE_VALUE) {
      FindNextChangeNotification(watch_handles_[watch_index].handle);
    }
    return WaitStatus::Event;
  }

  HANDLE stop_event_ = nullptr;
  std::vector<ChangeHandle> watch_handles_;
#elif defined(__linux__)
  void CloseWatchSet() {
    if (inotify_fd_ != -1) {
      close(inotify_fd_);
      inotify_fd_ = -1;
    }
  }

  void RebuildWatchSet() {
    CloseWatchSet();

    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ == -1) {
      return;
    }

    constexpr std::uint32_t watch_mask = IN_CREATE | IN_DELETE |
                                         IN_MOVED_FROM | IN_MOVED_TO |
                                         IN_DELETE_SELF | IN_MOVE_SELF |
                                         IN_ATTRIB;

    for (const auto &watch_directory : BuildWatchDirectories()) {
      inotify_add_watch(inotify_fd_, watch_directory.c_str(), watch_mask);
    }
  }

  WaitStatus WaitForEvent() {
    if (stop_requested_.load(std::memory_order_acquire)) {
      return WaitStatus::Stop;
    }
    if (inotify_fd_ == -1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      return stop_requested_.load(std::memory_order_acquire)
                 ? WaitStatus::Stop
                 : WaitStatus::Timeout;
    }

    pollfd poll_descriptor{inotify_fd_, POLLIN, 0};
    const int poll_result = poll(&poll_descriptor, 1, 250);
    if (poll_result == 0) {
      return WaitStatus::Timeout;
    }
    if (poll_result < 0) {
      return stop_requested_.load(std::memory_order_acquire)
                 ? WaitStatus::Stop
                 : WaitStatus::Timeout;
    }
    if ((poll_descriptor.revents & POLLIN) == 0) {
      return WaitStatus::Timeout;
    }

    char buffer[4096];
    while (read(inotify_fd_, buffer, sizeof(buffer)) > 0) {
    }
    return WaitStatus::Event;
  }

  int inotify_fd_ = -1;
#elif defined(__APPLE__)
  struct KqueueWatch {
    std::filesystem::path path;
    int fd = -1;
  };

  void CloseWatchSet() {
    for (auto &watch : kqueue_watches_) {
      if (watch.fd != -1) {
        close(watch.fd);
      }
    }
    kqueue_watches_.clear();

    if (kqueue_fd_ != -1) {
      close(kqueue_fd_);
      kqueue_fd_ = -1;
    }
  }

  void RebuildWatchSet() {
    CloseWatchSet();

    kqueue_fd_ = kqueue();
    if (kqueue_fd_ == -1) {
      return;
    }

    constexpr std::uint32_t vnode_flags = NOTE_ATTRIB | NOTE_DELETE |
                                          NOTE_EXTEND | NOTE_LINK |
                                          NOTE_RENAME | NOTE_REVOKE |
                                          NOTE_WRITE;

    for (const auto &watch_directory : BuildWatchDirectories()) {
      const int fd = open(watch_directory.c_str(), O_EVTONLY);
      if (fd == -1) {
        continue;
      }

      struct kevent change_event;
      EV_SET(&change_event, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, vnode_flags,
             0, nullptr);
      if (kevent(kqueue_fd_, &change_event, 1, nullptr, 0, nullptr) == -1) {
        close(fd);
        continue;
      }

      kqueue_watches_.push_back(KqueueWatch{watch_directory, fd});
    }
  }

  WaitStatus WaitForEvent() {
    if (stop_requested_.load(std::memory_order_acquire)) {
      return WaitStatus::Stop;
    }
    if (kqueue_fd_ == -1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      return stop_requested_.load(std::memory_order_acquire)
                 ? WaitStatus::Stop
                 : WaitStatus::Timeout;
    }

    struct kevent triggered_event;
    timespec timeout{};
    timeout.tv_nsec = 250000000;

    const int event_count =
        kevent(kqueue_fd_, nullptr, 0, &triggered_event, 1, &timeout);
    if (event_count == 0) {
      return WaitStatus::Timeout;
    }
    if (event_count < 0) {
      return stop_requested_.load(std::memory_order_acquire)
                 ? WaitStatus::Stop
                 : WaitStatus::Timeout;
    }

    return WaitStatus::Event;
  }

  int kqueue_fd_ = -1;
  std::vector<KqueueWatch> kqueue_watches_;
#else
  void CloseWatchSet() {}

  void RebuildWatchSet() {}

  WaitStatus WaitForEvent() {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    return stop_requested_.load(std::memory_order_acquire)
               ? WaitStatus::Stop
               : WaitStatus::Timeout;
  }
#endif

  mutable std::mutex state_mutex_;
  std::string configured_mount_path_;
  std::string resolved_mount_path_;
  std::atomic<bool> stop_requested_{false};
  std::thread worker_;
};
} // namespace CppServer::Services::Files