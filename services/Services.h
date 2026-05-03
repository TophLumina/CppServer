#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "API.h"

namespace CppServer::Services {
template <typename TContext>
void RegisterRoutes(httplib::Server &server, TContext &context) {
  using Clock = std::chrono::steady_clock;
  using Json = nlohmann::json;
  auto api_registry = std::make_shared<httplib::API::Registry>();
  httplib::API::Router<TContext> router(server, context,
                                        std::move(api_registry), "INFO");

  router.Get(
      "/", "Runtime Status",
      "Get current uptime and total request count", "Runtime counters",
      [&](const httplib::Request &, TContext &ctx) {
        ctx.request_count.fetch_add(1, std::memory_order_relaxed);

        const auto uptime_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() -
                                                             ctx.start_time)
                .count();

        return Json{{"ok", true},
                    {"uptime_seconds", uptime_seconds},
                    {"request_count",
                     ctx.request_count.load(std::memory_order_relaxed)}};
      });

  router.Get(
      "/request-status", "Request Echo", "Echo request meta with server runtime",
      "Echo payload",
      [&](const httplib::Request &req, TContext &ctx) {
        ctx.request_count.fetch_add(1, std::memory_order_relaxed);

        const auto uptime_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() -
                                                             ctx.start_time)
                .count();

        return Json{{"ok", true},
                    {"method", req.method},
                    {"path", req.path},
                    {"uptime_seconds", uptime_seconds},
                    {"request_count",
                     ctx.request_count.load(std::memory_order_relaxed)}};
      });

  router.RegisterSwaggerUI("CppServer API", "1.0.0",
                           "Auto-generated routes and response metadata docs",
                           "/docs", "/docs/openapi.json");
}
} // namespace CppServer::Services