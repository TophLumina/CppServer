#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <utility>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "Utils.h"

namespace CppServer::Core {
class Server {
private:
  std::vector<std::shared_ptr<httplib::Server>> servers;
  std::vector<int> bound_ports;
  std::vector<std::thread> listener_threads;
  Utils::AppContext context;

  template <typename TPool> class SharedTaskQueue : public httplib::TaskQueue {
  public:
    explicit SharedTaskQueue(TPool &pool)
        : pool(std::ref(pool)), shutting_down(false) {}

    bool enqueue(std::function<void()> fn) override {
      if (shutting_down.load(std::memory_order_acquire)) {
        return false;
      }

      try {
        pool.get().Submit([task = std::move(fn)]() mutable { task(); });
        return true;
      } catch (...) {
        return false;
      }
    }

    void shutdown() override {
      shutting_down.store(true, std::memory_order_release);
    }

  private:
    std::reference_wrapper<TPool> pool;
    std::atomic<bool> shutting_down;
  };

public:
  explicit Server(Utils::ServerOptions options);
  void Start();
  void Shutdown();
};
} // namespace CppServer::Core