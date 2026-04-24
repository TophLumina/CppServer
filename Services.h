#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace CppServer::Services {
template <typename TContext>
void RegisterRoutes(httplib::Server &server, TContext &context) {
  using Clock = std::chrono::steady_clock;
  using Json = nlohmann::json;
  auto &request_count = context.request_count;
  const auto &start_time = context.start_time;

  server.Get("/", [&](const httplib::Request &, httplib::Response &res) {
    request_count.fetch_add(1, std::memory_order_relaxed);
    res.set_content("C++ status server is running. Try /status\n",
                    "text/plain; charset=utf-8");
  });

  server.Get("/status", [&](const httplib::Request &, httplib::Response &res) {
    request_count.fetch_add(1, std::memory_order_relaxed);

    const auto uptime_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(Clock::now() -
                                                         start_time)
            .count();

    Json body = {
        {"ok", true},
        {"uptime_seconds", uptime_seconds},
        {"request_count", request_count.load(std::memory_order_relaxed)}};

    res.set_content(body.dump(), "application/json; charset=utf-8");
  });

  server.Get("/request-status",
             [&](const httplib::Request &req, httplib::Response &res) {
               request_count.fetch_add(1, std::memory_order_relaxed);

               const auto uptime_seconds =
                   std::chrono::duration_cast<std::chrono::seconds>(
                       Clock::now() - start_time)
                       .count();

               Json body = {{"ok", true},
                            {"method", req.method},
                            {"path", req.path},
                            {"uptime_seconds", uptime_seconds},
                            {"request_count",
                             request_count.load(std::memory_order_relaxed)}};

               res.set_content(body.dump(), "application/json; charset=utf-8");
             });
}
} // namespace CppServer::Services