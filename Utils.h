#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <string>

#include "ThreadPool.h"

namespace CppServer::Utils {
constexpr std::size_t PORT_COUNT = 2;
inline const std::size_t WORKER_THREADS = std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 4;
struct ServerOptions {
  std::string host = "0.0.0.0";
  std::array<int, PORT_COUNT> preferred_ports = {8080, 8081};
  int worker_threads = WORKER_THREADS;
};

struct AppContext {
  using Clock = std::chrono::steady_clock;

  explicit AppContext(ServerOptions opts)
      : options(std::move(opts)), start_time(Clock::now()), request_count(0),
        worker_pool(options.worker_threads) {}

  ServerOptions options;
  Clock::time_point start_time;
  std::atomic<unsigned long long> request_count;
  ::ThreadPool::ThreadPool worker_pool;
};
} // namespace CppServer::Utils