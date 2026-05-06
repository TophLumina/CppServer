#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "Application.h"
#include "Server.h"

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

void ConfigureClient(httplib::Client &client) {
  client.set_connection_timeout(200ms);
  client.set_read_timeout(500ms);
  client.set_write_timeout(500ms);
}

std::pair<int, int> AllocateServicePorts() {
  const int api_port = AllocateLoopbackPort();
  int file_port = AllocateLoopbackPort();
  while (file_port == api_port) {
    file_port = AllocateLoopbackPort();
  }
  return {api_port, file_port};
}

class ScopedApiServer {
public:
  ScopedApiServer(int api_port, int file_port)
      : options_(MakeOptions(api_port, file_port)), server_(options_),
        api_port_(api_port) {
    CppServer::Application::ConfigureApplication(server_, options_);

    thread_ = std::thread([this] { server_.Start(); });
    if (!WaitUntilReady()) {
      server_.Shutdown();
      if (thread_.joinable()) {
        thread_.join();
      }
      throw std::runtime_error("api service did not start in time");
    }
  }

  ~ScopedApiServer() {
    server_.Shutdown();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  int Port() const { return api_port_; }

private:
  static CppServer::Core::ServerOptions MakeOptions(int api_port,
                                                    int file_port) {
    CppServer::Core::ServerOptions options;
    options.host = "127.0.0.1";
    options.worker_threads = 2;
    options.service_port_overrides = {
        CppServer::Core::PortRange{api_port, 1},
        CppServer::Core::PortRange{file_port, 1},
    };
    return options;
  }

  bool WaitUntilReady() {
    return WaitUntil(5s, [&] {
      httplib::Client client("127.0.0.1", api_port_);
      ConfigureClient(client);
      auto result = client.Get("/");
      return static_cast<bool>(result);
    });
  }

  CppServer::Core::ServerOptions options_;
  CppServer::Core::Server server_;
  int api_port_;
  std::thread thread_;
};

bool TestDefaultRoute(int port) {
  httplib::Client client("127.0.0.1", port);
  ConfigureClient(client);

  auto result = client.Get("/");
  if (!result || result->status != 200) {
    std::cerr << "expected GET / to return 200\n";
    return false;
  }

  if (result->body.find("OK") == std::string::npos) {
    std::cerr << "expected GET / body to contain OK\n";
    return false;
  }

  return true;
}

bool TestStatusRoute(int port) {
  httplib::Client client("127.0.0.1", port);
  ConfigureClient(client);

  auto result = client.Get("/status");
  if (!result || result->status != 200) {
    std::cerr << "expected GET /status to return 200\n";
    return false;
  }

  const auto json = nlohmann::json::parse(result->body, nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    std::cerr << "expected GET /status to return a JSON object\n";
    return false;
  }

  if (!json.contains("alive") || !json["alive"].is_boolean() ||
      !json["alive"].get<bool>()) {
    std::cerr << "expected GET /status to report alive=true\n";
    return false;
  }

  if (!json.contains("uptime_seconds") || !json["uptime_seconds"].is_number()) {
    std::cerr << "expected GET /status to contain uptime_seconds\n";
    return false;
  }

  return true;
}

bool TestOpenApiRoute(int port) {
  httplib::Client client("127.0.0.1", port);
  ConfigureClient(client);

  auto result = client.Get("/docs/openapi.json");
  if (!result || result->status != 200) {
    std::cerr << "expected GET /docs/openapi.json to return 200\n";
    return false;
  }

  const auto json = nlohmann::json::parse(result->body, nullptr, false);
  if (json.is_discarded() || !json.is_object()) {
    std::cerr << "expected OpenAPI endpoint to return a JSON object\n";
    return false;
  }

  if (!json.contains("openapi") || !json["openapi"].is_string()) {
    std::cerr << "expected OpenAPI document to contain openapi version\n";
    return false;
  }

  if (!json.contains("paths") || !json["paths"].is_object() ||
      !json["paths"].contains("/status")) {
    std::cerr << "expected OpenAPI document to include /status path\n";
    return false;
  }

  return true;
}

bool TestSampleRandomIntRoute(int port) {
  httplib::Client client("127.0.0.1", port);
  ConfigureClient(client);

  auto result = client.Get("/sample/randomint?min=3&max=7");
  if (!result || result->status != 200) {
    std::cerr << "expected GET /sample/randomint to return 200\n";
    return false;
  }

  const auto json = nlohmann::json::parse(result->body, nullptr, false);
  if (json.is_discarded() || !json.is_number_integer()) {
    std::cerr << "expected randomint endpoint to return an integer payload\n";
    return false;
  }

  const int value = json.get<int>();
  if (value < 3 || value > 7) {
    std::cerr << "expected randomint result to stay within requested bounds\n";
    return false;
  }

  return true;
}
} // namespace

int main() {
  try {
    WinsockSession winsock_session;
    (void)winsock_session;

    const auto [api_port, file_port] = AllocateServicePorts();
    ScopedApiServer server(api_port, file_port);

    if (!TestDefaultRoute(server.Port())) {
      return 1;
    }
    if (!TestStatusRoute(server.Port())) {
      return 1;
    }
    if (!TestOpenApiRoute(server.Port())) {
      return 1;
    }
    if (!TestSampleRandomIntRoute(server.Port())) {
      return 1;
    }
  } catch (const std::exception &ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}