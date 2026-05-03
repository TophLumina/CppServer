#include "ThreadPool.h"

#include <optional>
#include <stdexcept>

namespace ThreadPool {

thread_local std::optional<std::size_t> tls_queue_index;

void ThreadPool::EnqueueReal(std::function<void()> task) {
  if (terminate) {
    throw std::runtime_error("Submit on stopped ThreadPool");
  }

  const std::size_t queue_index =
      tls_queue_index.has_value()
          ? *tls_queue_index
          : submit_index.fetch_add(1, std::memory_order_relaxed) %
                queues.size();

  {
    std::unique_lock<std::mutex> lock(queues[queue_index].mtx);
    queues[queue_index].tasks.emplace_back(std::move(task));
  }

  queues[queue_index].count.fetch_add(1, std::memory_order_release);
  sleep_cv.notify_one();
}

ThreadPool::ThreadPool(std::size_t numThreads)
    : terminate(false), submit_index(0) {
  if (numThreads == 0)
    numThreads = 1;

  for (std::size_t i = 0; i < numThreads; ++i) {
    queues.emplace_back();
  }

  for (std::size_t i = 0; i < numThreads; ++i) {
    workers.emplace_back([this, i] { WorkerRoutine(i); });
  }
}

ThreadPool::~ThreadPool() {
  terminate = true;
  sleep_cv.notify_all();
  for (std::thread &worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::WorkerRoutine(std::size_t index) {
  tls_queue_index = index;
  const std::size_t numQueues = queues.size();

  while (true) {
    std::function<void()> task;
    std::optional<std::size_t> popped_queue_index;

    // 1. 尝试从本地队列 (LIFO 模式有助于缓存热度) 弹出
    {
      std::unique_lock<std::mutex> lock(queues[index].mtx);
      if (!queues[index].tasks.empty()) {
        task = std::move(queues[index].tasks.back());
        queues[index].tasks.pop_back();
        popped_queue_index = index;
      }
    }

    // 2. 本地队列为空，尝试进行 Work-Stealing
    if (!task) {
      for (std::size_t i = 1; i < numQueues; ++i) {
        const std::size_t steal_index = (index + i) % numQueues;
        std::unique_lock<std::mutex> lock(queues[steal_index].mtx,
                                          std::try_to_lock);
        if (lock.owns_lock() && !queues[steal_index].tasks.empty()) {
          // 从别人队列的前端偷取 (FIFO，偷取那些久未执行的数据)
          task = std::move(queues[steal_index].tasks.front());
          queues[steal_index].tasks.pop_front();
          popped_queue_index = steal_index;
          break;
        }
      }
    }

    // 3. 执行任务与休眠机制
    if (task) {
      queues[*popped_queue_index].count.fetch_sub(1, std::memory_order_release);
      task();
    } else {
      auto has_tasks = [&]() {
        for (std::size_t i = 0; i < numQueues; ++i) {
          if (queues[i].count.load(std::memory_order_acquire) > 0)
            return true;
        }
        return false;
      };

      // 没有任何任务可做时，检查是否应当退出
      if (terminate.load(std::memory_order_acquire) && !has_tasks()) {
        break;
      }
      // 否则进入休眠等待
      std::unique_lock<std::mutex> sleep_lock(sleep_mtx);
      sleep_cv.wait(sleep_lock, [&] {
        return terminate.load(std::memory_order_acquire) || has_tasks();
      });
    }
  }
}

} // namespace ThreadPool
