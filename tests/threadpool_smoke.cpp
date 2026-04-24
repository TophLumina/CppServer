#include <atomic>
#include <cstdint>
#include <future>
#include <iostream>
#include <vector>

#include "ThreadPool.h"

int main() {
    ThreadPool pool(4);

    constexpr int task_count = 100;
    std::atomic<int> counter{0};
    std::vector<std::future<int64_t>> futures;
    futures.reserve(task_count);

    for (int i = 1; i <= task_count; ++i) {
        futures.emplace_back(pool.Submit([&counter, i]() -> int64_t {
            counter.fetch_add(1, std::memory_order_relaxed);
            return static_cast<int64_t>(i) * i;
        }));
    }

    int64_t sum = 0;
    for (auto &f : futures) {
        sum += f.get();
    }

    const int64_t expected_sum =
        static_cast<int64_t>(task_count) * (task_count + 1) * (2 * task_count + 1) / 6;

    if (sum != expected_sum) {
        std::cerr << "sum mismatch: got=" << sum << " expected=" << expected_sum << '\n';
        return 1;
    }

    if (counter.load(std::memory_order_relaxed) != task_count) {
        std::cerr << "counter mismatch: got=" << counter.load(std::memory_order_relaxed)
                  << " expected=" << task_count << '\n';
        return 1;
    }

    return 0;
}
