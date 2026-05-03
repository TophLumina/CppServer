#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "RouterModule.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace CppServer::Routers {
template <typename TContext>
class InfoRouter final : public CppServer::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "INFO"; }

  void Register(httplib::API::Router<TContext> &router) override {
    using Clock = std::chrono::steady_clock;
    using SystemClock = std::chrono::system_clock;
    using Json = nlohmann::json;

    router.Get(
        "/", "Liveness Status",
        "Get liveness, uptime, host CPU/GPU usage and transport RTT",
        "Runtime health",
        [&](const httplib::Request &req, TContext &ctx) {
          const auto handler_started = Clock::now();
          ctx.request_count.fetch_add(1, std::memory_order_relaxed);

          const auto uptime_seconds =
              std::chrono::duration_cast<std::chrono::seconds>(Clock::now() -
                                                               ctx.start_time)
                  .count();

          const auto host_cpu_usage_percent = ReadHostCpuUsagePercent();
          const auto host_gpu_usage_percent = ReadHostGpuUsagePercent();

          const auto now_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        SystemClock::now().time_since_epoch())
                                        .count();

          long long rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 Clock::now() - handler_started)
                                 .count();
          std::string rtt_source = "server_processing_fallback";
          if (const auto client_sent_ms = ParseEpochMsHeader(req, "x-client-send-ms")) {
            rtt_ms = std::max<long long>(0, now_epoch_ms - *client_sent_ms);
            rtt_source = "x-client-send-ms";
          }

          return Json{
              {"alive", true},
              {"uptime_seconds", uptime_seconds},
              {"host_cpu_usage_percent",
               host_cpu_usage_percent ? Json(*host_cpu_usage_percent)
                                      : Json(nullptr)},
              {"host_gpu_usage_percent",
               host_gpu_usage_percent ? Json(*host_gpu_usage_percent)
                                      : Json(nullptr)},
              {"rtt_ms", rtt_ms},
              {"rtt_source", std::move(rtt_source)},
          };
        });

    router.RegisterSwaggerUI("CppServer API", "1.0.0",
                             "Auto-generated routes and response metadata docs",
                             "/docs", "/docs/openapi.json");
  }

private:
  // CPU usage is computed from two snapshots; the first call warms up state.
  static std::optional<double>
  ComputeUsageFromCounters(unsigned long long idle_ticks,
                           unsigned long long total_ticks) {
    thread_local unsigned long long previous_idle = 0;
    thread_local unsigned long long previous_total = 0;
    thread_local bool initialized = false;

    if (!initialized) {
      previous_idle = idle_ticks;
      previous_total = total_ticks;
      initialized = true;
      return std::nullopt;
    }

    const unsigned long long idle_delta = idle_ticks - previous_idle;
    const unsigned long long total_delta = total_ticks - previous_total;

    previous_idle = idle_ticks;
    previous_total = total_ticks;

    if (total_delta == 0 || idle_delta > total_delta) {
      return std::nullopt;
    }

    const double usage =
        (static_cast<double>(total_delta - idle_delta) * 100.0) /
        static_cast<double>(total_delta);
    return std::clamp(usage, 0.0, 100.0);
  }

  static std::optional<long long> ParseEpochMsHeader(const httplib::Request &req,
                                                     const char *header_name) {
    if (!req.has_header(header_name)) {
      return std::nullopt;
    }

    const std::string &value = req.get_header_value(header_name);
    if (value.empty()) {
      return std::nullopt;
    }

    char *end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
      return std::nullopt;
    }

    return parsed;
  }

  static std::optional<double> ReadHostCpuUsagePercent() {
#ifdef _WIN32
    // Windows: collect kernel/user/idle ticks from GetSystemTimes.
    FILETIME idle_time{};
    FILETIME kernel_time{};
    FILETIME user_time{};
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
      return std::nullopt;
    }

    const auto to_u64 = [](const FILETIME &ft) -> unsigned long long {
      ULARGE_INTEGER value;
      value.LowPart = ft.dwLowDateTime;
      value.HighPart = ft.dwHighDateTime;
      return value.QuadPart;
    };

    const unsigned long long idle = to_u64(idle_time);
    const unsigned long long total = to_u64(kernel_time) + to_u64(user_time);

    return ComputeUsageFromCounters(idle, total);
  #elif defined(__linux__)
    // Linux: read cumulative CPU counters from /proc/stat.
    FILE *stat_file = std::fopen("/proc/stat", "r");
    if (stat_file == nullptr) {
      return std::nullopt;
    }

    char label[16] = {0};
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle_ticks = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    const int scanned = std::fscanf(stat_file,
                    "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                    label, &user, &nice, &system, &idle_ticks,
                    &iowait, &irq, &softirq, &steal);
    std::fclose(stat_file);
    if (scanned < 5 || std::string(label) != "cpu") {
      return std::nullopt;
    }

    const unsigned long long idle = idle_ticks + iowait;
    const unsigned long long total =
      user + nice + system + idle_ticks + iowait + irq + softirq + steal;
    return ComputeUsageFromCounters(idle, total);
  #elif defined(__APPLE__)
    // macOS: use Mach host statistics CPU ticks.
    host_cpu_load_info_data_t cpu_load{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    const kern_return_t result =
      host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
              reinterpret_cast<host_info_t>(&cpu_load), &count);
    if (result != KERN_SUCCESS) {
      return std::nullopt;
    }

    const unsigned long long user =
      static_cast<unsigned long long>(cpu_load.cpu_ticks[CPU_STATE_USER]);
    const unsigned long long system =
      static_cast<unsigned long long>(cpu_load.cpu_ticks[CPU_STATE_SYSTEM]);
    const unsigned long long idle =
      static_cast<unsigned long long>(cpu_load.cpu_ticks[CPU_STATE_IDLE]);
    const unsigned long long nice =
      static_cast<unsigned long long>(cpu_load.cpu_ticks[CPU_STATE_NICE]);

    return ComputeUsageFromCounters(idle, user + system + idle + nice);
#else
    return std::nullopt;
#endif
  }

  static std::optional<double> ReadHostGpuUsagePercent() {
    // GPU utilization: use nvidia-smi when present; return null when unavailable.
#ifdef _WIN32
    const char *command =
        "nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>NUL";
    FILE *pipe = _popen(command, "r");
#else
    const char *command =
      "nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null";
    FILE *pipe = popen(command, "r");
#endif
    if (pipe == nullptr) {
      return std::nullopt;
    }

    char buffer[128] = {0};
    const bool has_line = std::fgets(buffer, sizeof(buffer), pipe) != nullptr;
#ifdef _WIN32
    _pclose(pipe);
#else
  pclose(pipe);
#endif
    if (!has_line) {
      return std::nullopt;
    }

    char *end = nullptr;
    const double parsed = std::strtod(buffer, &end);
    if (end == buffer) {
      return std::nullopt;
    }

    return std::clamp(parsed, 0.0, 100.0);
  }
};
} // namespace CppServer::Routers
