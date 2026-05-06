#pragma once

#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ThreadPool.h"

namespace CppServer::Core {
inline const std::size_t
    WORKER_THREADS = std::thread::hardware_concurrency() > 0
                         ? std::thread::hardware_concurrency()
                         : 4;

struct PortRange {
  int port_begin = 0;
  std::size_t port_count = 0;
};

struct ServerOptions {
  std::string host = "0.0.0.0";
  std::vector<PortRange> service_port_overrides;
  int worker_threads = WORKER_THREADS;
};

struct CoreContext {
  explicit CoreContext(ServerOptions opts)
      : options(std::move(opts)), worker_pool(options.worker_threads) {}

  ServerOptions options;
  ::ThreadPool::ThreadPool worker_pool;
};
} // namespace CppServer::Core