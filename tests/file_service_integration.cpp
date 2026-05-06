#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include <httplib.h>

#include "Compositor.h"
#include "Server.h"
#include "ServiceTags.h"
#include "services/files/Runtime.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
using namespace std::chrono_literals;

class WinsockSession {
public:
#ifdef _WIN32
  WinsockSession() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  }

  ~WinsockSession() { WSACleanup(); }
#else
  WinsockSession() = default;
  ~WinsockSession() = default;
#endif
};

int AllocateLoopbackPort() {
#ifdef _WIN32
  const SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    throw std::runtime_error("socket() failed");
  }
#else
  const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    throw std::runtime_error("socket() failed");
  }
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;

  const int bind_result = ::bind(
      sock, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
  if (bind_result != 0) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    throw std::runtime_error("bind() failed");
  }

  socklen_t address_length = sizeof(address);
  if (::getsockname(sock, reinterpret_cast<sockaddr *>(&address),
                    &address_length) != 0) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    throw std::runtime_error("getsockname() failed");
  }

#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  return ntohs(address.sin_port);
}

bool WriteTextFile(const std::filesystem::path &file_path,
                   const std::string &content) {
  std::ofstream stream(file_path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return false;
  }

  stream << content;
  stream.flush();
  return stream.good();
}

bool WaitUntil(std::chrono::milliseconds timeout,
               const std::function<bool()> &predicate) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(50ms);
  }
  return predicate();
}

std::optional<std::string> FetchBody(int port, const std::string &path,
                                     int expected_status) {
  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(200ms);
  client.set_read_timeout(200ms);
  client.set_write_timeout(200ms);

  auto result = client.Get(path);
  if (!result || result->status != expected_status) {
    return std::nullopt;
  }

  return result->body;
}

bool WaitForBody(int port, const std::string &path,
                 const std::string &expected_body,
                 std::chrono::milliseconds timeout) {
  return WaitUntil(timeout, [&] {
    const auto body = FetchBody(port, path, 200);
    return body.has_value() && *body == expected_body;
  });
}

bool WaitForStatus(int port, const std::string &path, int expected_status,
                   std::chrono::milliseconds timeout) {
  return WaitUntil(timeout, [&] {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(200ms);
    client.set_read_timeout(200ms);
    client.set_write_timeout(200ms);
    auto result = client.Get(path);
    return result && result->status == expected_status;
  });
}

class ScopedFileServer {
public:
  ScopedFileServer(std::filesystem::path mount_path, int port)
      : mount_path_(std::move(mount_path)), port_(port),
        server_(MakeOptions()) {
    using FileServiceTag = CppServer::Core::ServiceTags::File;
    using FileContext = FileServiceTag::Context;

    auto &compositor = server_.Composition();
    compositor.Clear();
    compositor
        .Compose<FileServiceTag>(
            CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
            CppServer::Core::PortRange{port_, 1},
            [mount_path = mount_path_.string()](int listening_port) {
              return std::make_unique<FileContext>(listening_port, mount_path);
            })
        .ConfigureHttpServer(
            [](httplib::Server &http_server, FileContext &file_context, int) {
              CppServer::Services::Files::ConfigureRuntime(http_server,
                                                           file_context);
            });

    thread_ = std::thread([this] { server_.Start(); });
    if (!WaitUntilReady()) {
      server_.Shutdown();
      if (thread_.joinable()) {
        thread_.join();
      }
      throw std::runtime_error("file service did not start in time");
    }
  }

  ~ScopedFileServer() {
    server_.Shutdown();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int Port() const { return port_; }

private:
  static CppServer::Core::ServerOptions MakeOptions() {
    CppServer::Core::ServerOptions options;
    options.host = "127.0.0.1";
    options.worker_threads = 2;
    return options;
  }

  bool WaitUntilReady() {
    return WaitUntil(5s, [&] {
      httplib::Client client("127.0.0.1", port_);
      client.set_connection_timeout(200ms);
      client.set_read_timeout(200ms);
      client.set_write_timeout(200ms);
      auto result = client.Get("/__startup_probe__");
      return static_cast<bool>(result);
    });
  }

  std::filesystem::path mount_path_;
  int port_;
  CppServer::Core::Server server_;
  std::thread thread_;
};

std::filesystem::path CreateTestRoot(const std::string &name) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("cppserver_" + name + "_" + std::to_string(now));
  std::filesystem::create_directories(root);
  return root;
}

bool TestMountAppearsAfterStartup() {
  const auto test_root = CreateTestRoot("mount_appears");
  const auto mount_path = test_root / "mount";
  const int port = AllocateLoopbackPort();
  bool ok = false;
  {
    ScopedFileServer server(mount_path, port);

    if (!WaitForStatus(port, "/late.txt", 404, 2s)) {
      std::cerr << "expected 404 before mount path exists\n";
      std::filesystem::remove_all(test_root);
      return false;
    }

    std::filesystem::create_directories(mount_path);
    if (!WriteTextFile(mount_path / "late.txt", "late mount")) {
      std::cerr << "failed to create late.txt after startup\n";
      std::filesystem::remove_all(test_root);
      return false;
    }

    ok = WaitForBody(port, "/late.txt", "late mount", 5s);
    if (!ok) {
      std::cerr << "late mount file was not served after directory appeared\n";
    }
  }

  std::filesystem::remove_all(test_root);
  return ok;
}

bool TestCachedFileUpdatesAfterRewrite() {
  const auto test_root = CreateTestRoot("cached_rewrite");
  const auto mount_path = test_root / "mount";
  std::filesystem::create_directories(mount_path);
  const auto hello_file = mount_path / "hello.txt";
  if (!WriteTextFile(hello_file, "version-one")) {
    std::cerr << "failed to create initial hello.txt\n";
    std::filesystem::remove_all(test_root);
    return false;
  }

  const int port = AllocateLoopbackPort();
  bool ok = false;
  {
    ScopedFileServer server(mount_path, port);

    if (!WaitForBody(port, "/hello.txt", "version-one", 5s)) {
      std::cerr << "failed to read initial cached file response\n";
      std::filesystem::remove_all(test_root);
      return false;
    }

    if (!WriteTextFile(hello_file, "version-two-updated")) {
      std::cerr << "failed to rewrite cached hello.txt\n";
      std::filesystem::remove_all(test_root);
      return false;
    }

    ok = WaitForBody(port, "/hello.txt", "version-two-updated", 5s);
    if (!ok) {
      std::cerr << "cached file did not refresh after rewrite\n";
    }
  }

  std::filesystem::remove_all(test_root);
  return ok;
}

bool TestSymlinkEscapeIsRejected() {
  const auto test_root = CreateTestRoot("symlink_escape");
  const auto mount_path = test_root / "mount";
  const auto outside_root = test_root / "outside";
  std::filesystem::create_directories(mount_path);
  std::filesystem::create_directories(outside_root);

  const auto secret_file = outside_root / "secret.txt";
  if (!WriteTextFile(secret_file, "outside-secret")) {
    std::cerr << "failed to create secret.txt outside mount\n";
    std::filesystem::remove_all(test_root);
    return false;
  }

  const auto symlink_path = mount_path / "escape.txt";
  std::error_code symlink_error;
  std::filesystem::create_symlink(secret_file, symlink_path, symlink_error);
  if (symlink_error) {
#ifdef _WIN32
    if (symlink_error.value() == ERROR_PRIVILEGE_NOT_HELD ||
        symlink_error.value() == ERROR_ACCESS_DENIED) {
      std::cerr
          << "skipping symlink escape test: symlink privilege unavailable\n";
      std::filesystem::remove_all(test_root);
      return true;
    }
#else
    if (symlink_error == std::errc::operation_not_permitted ||
        symlink_error == std::errc::permission_denied) {
      std::cerr
          << "skipping symlink escape test: symlink privilege unavailable\n";
      std::filesystem::remove_all(test_root);
      return true;
    }
#endif

    std::cerr << "failed to create symlink escape fixture: "
              << symlink_error.message() << '\n';
    std::filesystem::remove_all(test_root);
    return false;
  }

  const int port = AllocateLoopbackPort();
  bool ok = false;
  {
    ScopedFileServer server(mount_path, port);
    ok = WaitForStatus(port, "/escape.txt", 404, 5s);
    if (!ok) {
      std::cerr << "symlink escape path should not be served\n";
    }
  }

  std::filesystem::remove_all(test_root);
  return ok;
}
} // namespace

int main() {
  try {
    WinsockSession winsock_session;
    (void)winsock_session;

    if (!TestMountAppearsAfterStartup()) {
      return 1;
    }
    if (!TestCachedFileUpdatesAfterRewrite()) {
      return 1;
    }
    if (!TestSymlinkEscapeIsRejected()) {
      return 1;
    }
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}