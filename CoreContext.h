#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
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

struct TaskQueueBudget {
  explicit TaskQueueBudget(std::size_t max_inflight_connection_tasks = 0)
      : max_inflight_connection_tasks(max_inflight_connection_tasks) {}

  bool TryEnqueue() {
    if (max_inflight_connection_tasks == 0) {
      inflight_connection_tasks.fetch_add(1, std::memory_order_relaxed);
      queued_connection_tasks.fetch_add(1, std::memory_order_relaxed);
      return true;
    }

    auto current = inflight_connection_tasks.load(std::memory_order_relaxed);
    while (current < max_inflight_connection_tasks) {
      if (inflight_connection_tasks.compare_exchange_weak(
              current, current + 1, std::memory_order_relaxed,
              std::memory_order_relaxed)) {
        queued_connection_tasks.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
    }

    rejected_connection_tasks.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  void OnSubmitAccepted() {
    accepted_connection_tasks.fetch_add(1, std::memory_order_relaxed);
  }

  void OnSubmitRejected() {
    queued_connection_tasks.fetch_sub(1, std::memory_order_relaxed);
    inflight_connection_tasks.fetch_sub(1, std::memory_order_relaxed);
    rejected_connection_tasks.fetch_add(1, std::memory_order_relaxed);
  }

  void OnExecutionStarted() {
    queued_connection_tasks.fetch_sub(1, std::memory_order_relaxed);
    running_connection_tasks.fetch_add(1, std::memory_order_relaxed);
  }

  void OnExecutionFinished() {
    running_connection_tasks.fetch_sub(1, std::memory_order_relaxed);
    inflight_connection_tasks.fetch_sub(1, std::memory_order_relaxed);
  }

  const std::size_t max_inflight_connection_tasks = 0;
  std::atomic<std::size_t> inflight_connection_tasks{0};
  std::atomic<std::size_t> queued_connection_tasks{0};
  std::atomic<std::size_t> running_connection_tasks{0};
  std::atomic<std::size_t> accepted_connection_tasks{0};
  std::atomic<std::size_t> rejected_connection_tasks{0};
};

struct ServerOptions {
  std::string host = "0.0.0.0";
  std::vector<PortRange> service_port_overrides;
  int worker_threads = WORKER_THREADS;
  // If zero, no limit is applied to the number of inflight connection tasks. Otherwise, new connection tasks will be rejected once the number of inflight connection tasks reaches this limit.
  std::size_t max_inflight_connection_tasks = 4 * WORKER_THREADS;
};

struct CoreContext {
  explicit CoreContext(ServerOptions opts)
      : options(std::move(opts)), worker_pool(options.worker_threads),
        task_queue_budget(std::make_shared<TaskQueueBudget>(
            options.max_inflight_connection_tasks)) {}

  ServerOptions options;
  ::ThreadPool::ThreadPool worker_pool;
  std::shared_ptr<TaskQueueBudget> task_queue_budget;
};
} // namespace CppServer::Core