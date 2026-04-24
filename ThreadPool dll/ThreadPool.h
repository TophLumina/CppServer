#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifdef ThreadPool_EXPORTS
#define THREADPOOL_API __declspec(dllexport)
#else
#define THREADPOOL_API __declspec(dllimport)
#endif
#else
#ifdef ThreadPool_EXPORTS
#define THREADPOOL_API __attribute__((visibility("default")))
#else
#define THREADPOOL_API
#endif
#endif

class THREADPOOL_API ThreadPool {
public:
  ThreadPool(std::size_t numThreads = std::thread::hardware_concurrency());
  ~ThreadPool();

  template <typename Func, typename... Args>
  auto Submit(Func &&func, Args &&...args)
      -> std::future<std::invoke_result_t<std::remove_cvref_t<Func>,
                                          std::remove_cvref_t<Args>...>> {
    using return_type = std::invoke_result_t<std::remove_cvref_t<Func>,
                                             std::remove_cvref_t<Args>...>;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [f = std::forward<Func>(func),
         args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
          return std::apply(std::move(f), std::move(args));
        });
    std::future<return_type> result = task->get_future();

    auto wrapper = [task]() { (*task)(); };

    EnqueueReal(std::move(wrapper));
    return result;
  }

private:
  void EnqueueReal(std::function<void()> task);

  struct alignas(64) WorkQueue {
    std::deque<std::function<void()>> tasks;
    std::mutex mtx;
    std::atomic<std::size_t> count{0};
  };

  std::deque<WorkQueue> queues;
  std::vector<std::thread> workers;

  std::atomic<bool> terminate;
  std::atomic<std::size_t> submit_index;

  // 用于在没有任务时休眠的工作机制
  std::mutex sleep_mtx;
  std::condition_variable sleep_cv;

  void WorkerRoutine(std::size_t index);

public:
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
};
