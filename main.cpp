#include <atomic>
#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "ThreadPool.h"

namespace {
struct ServerOptions {
    const char* host = "0.0.0.0";
    std::array<int, 4> preferred_ports = {8080, 8081, 8082, 8083};
    int worker_threads = 12;
};

struct AppContext {
    using Clock = std::chrono::steady_clock;

    explicit AppContext(ServerOptions opts)
        : options(std::move(opts)), start_time(Clock::now()), request_count(0), worker_pool(options.worker_threads) {}

    ServerOptions options;
    Clock::time_point start_time;
    std::atomic<unsigned long long> request_count;
    ThreadPool worker_pool;
};

template <typename TContext>
void RegisterRoutes(httplib::Server& server, TContext& context) {
    using Clock = std::chrono::steady_clock;
    using Json = nlohmann::json;
    auto& request_count = context.request_count;
    const auto& start_time = context.start_time;

    server.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        request_count.fetch_add(1, std::memory_order_relaxed);
        res.set_content("C++ status server is running. Try /status\n", "text/plain; charset=utf-8");
    });

    server.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
        request_count.fetch_add(1, std::memory_order_relaxed);

        const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - start_time).count();

        Json body = {
            {"ok", true},
            {"uptime_seconds", uptime_seconds},
            {"request_count", request_count.load(std::memory_order_relaxed)}
        };

        res.set_content(body.dump(), "application/json; charset=utf-8");
    });

    server.Get("/request-status", [&](const httplib::Request& req, httplib::Response& res) {
        request_count.fetch_add(1, std::memory_order_relaxed);

        const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - start_time).count();

        Json body = {
            {"ok", true},
            {"method", req.method},
            {"path", req.path},
            {"uptime_seconds", uptime_seconds},
            {"request_count", request_count.load(std::memory_order_relaxed)}
        };

        res.set_content(body.dump(), "application/json; charset=utf-8");
    });
}
} // namespace

template <typename TPool>
class SharedTaskQueue : public httplib::TaskQueue {
public:
    explicit SharedTaskQueue(TPool& pool)
        : pool_(pool), shutting_down_(false) {}

    bool enqueue(std::function<void()> fn) override {
        if (shutting_down_.load(std::memory_order_acquire)) {
            return false;
        }

        try {
            pool_.Submit([task = std::move(fn)]() mutable { task(); });
            return true;
        } catch (...) {
            return false;
        }
    }

    void shutdown() override {
        shutting_down_.store(true, std::memory_order_release);
    }

private:
    TPool& pool_;
    std::atomic<bool> shutting_down_;
};

int main() {
    AppContext context{ServerOptions{}};

    std::vector<std::unique_ptr<httplib::Server>> servers;
    servers.reserve(context.options.preferred_ports.size());

    std::vector<int> bound_ports;
    bound_ports.reserve(context.options.preferred_ports.size());

    for (const int preferred_port : context.options.preferred_ports) {
        auto server = std::make_unique<httplib::Server>();
        server->new_task_queue = [&context] { return new SharedTaskQueue<ThreadPool>(context.worker_pool); };
        RegisterRoutes(*server, context);

        int actual_port = -1;
        if (server->bind_to_port(context.options.host, preferred_port)) {
            actual_port = preferred_port;
        } else {
            actual_port = server->bind_to_any_port(context.options.host);
        }

        if (actual_port < 0) {
            std::cerr << "Failed to bind listener for preferred port " << preferred_port << "\n";
            return 1;
        }

        bound_ports.push_back(actual_port);
        servers.push_back(std::move(server));
    }

    std::cout << "Listener threads: " << servers.size()
              << ", worker threads: " << context.options.worker_threads << "\n";
    std::cout << "Listening ports:";
    for (const int port : bound_ports) {
        std::cout << ' ' << port;
    }
    std::cout << "\n";

    std::vector<std::thread> listener_threads;
    listener_threads.reserve(servers.size());
    for (auto& server : servers) {
        listener_threads.emplace_back([&server] {
            server->listen_after_bind();
        });
    }

    for (auto& th : listener_threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    return 0;
}
