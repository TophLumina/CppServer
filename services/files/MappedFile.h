#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace CppServer::Services::Files {
class MappedFile {
public:
  MappedFile() = default;

  explicit MappedFile(const std::filesystem::path &path) { open(path); }

  ~MappedFile() { close(); }

  MappedFile(const MappedFile &) = delete;
  MappedFile &operator=(const MappedFile &) = delete;
  MappedFile(MappedFile &&) = delete;
  MappedFile &operator=(MappedFile &&) = delete;

  bool open(const std::filesystem::path &path) {
    close();

#ifdef _WIN32
    const HANDLE file_handle =
        CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
      return false;
    }

    struct ScopedHandle {
      explicit ScopedHandle(HANDLE handle) : handle(handle) {}
      ~ScopedHandle() {
        if (handle != INVALID_HANDLE_VALUE) {
          CloseHandle(handle);
        }
      }
      HANDLE handle = INVALID_HANDLE_VALUE;
    } scoped_handle(file_handle);

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file_handle, &file_size) || file_size.QuadPart < 0) {
      return false;
    }

    size_ = static_cast<std::size_t>(file_size.QuadPart);
    is_open_ = true;
    if (size_ == 0) {
      return true;
    }

    buffer_.resize(size_);
    DWORD bytes_read = 0;
    if (!ReadFile(file_handle, buffer_.data(), static_cast<DWORD>(size_),
                  &bytes_read, nullptr) ||
        static_cast<std::size_t>(bytes_read) != size_) {
      close();
      return false;
    }

    data_ = buffer_.data();
    return true;
#else
    file_descriptor_ = ::open(path.c_str(), O_RDONLY);
    if (file_descriptor_ == -1) {
      return false;
    }

    struct stat status{};
    if (fstat(file_descriptor_, &status) == -1 || status.st_size < 0) {
      close();
      return false;
    }

    size_ = static_cast<std::size_t>(status.st_size);
    is_open_ = true;
    if (size_ == 0) {
      return true;
    }

    data_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, file_descriptor_, 0);
    if (data_ == MAP_FAILED) {
      data_ = nullptr;
      close();
      return false;
    }
#endif

    return true;
  }

  void close() {
#ifdef _WIN32
    buffer_.clear();
    data_ = nullptr;
#else
    if (data_ != nullptr) {
      munmap(data_, size_);
      data_ = nullptr;
    }
    if (file_descriptor_ != -1) {
      ::close(file_descriptor_);
      file_descriptor_ = -1;
    }
#endif

    is_open_ = false;
    size_ = 0;
  }

  bool is_open() const { return is_open_; }

  std::size_t size() const { return size_; }

  const char *data() const {
    return data_ != nullptr ? static_cast<const char *>(data_) : "";
  }

private:
  bool is_open_ = false;
  std::size_t size_ = 0;
  void *data_ = nullptr;

#ifdef _WIN32
  std::string buffer_;
#else
  int file_descriptor_ = -1;
#endif
};
} // namespace CppServer::Services::Files