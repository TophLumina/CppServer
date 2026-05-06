#pragma once

#include <chrono>
#include <cstddef>


namespace CppServer::Services::SimpleApi {
struct Context {
  using Clock = std::chrono::steady_clock;

  static constexpr int DEFAULT_PORT_BEGIN = 8080;
  static constexpr std::size_t DEFAULT_PORT_COUNT = 1;

  explicit Context(int listening_port = DEFAULT_PORT_BEGIN)
      : start_time(Clock::now()), port(listening_port) {}

  Clock::time_point start_time;
  int port;
};
} // namespace CppServer::Services::SimpleApi
