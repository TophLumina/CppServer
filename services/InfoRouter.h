#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "RouterModule.h"

#if defined(__linux__)
#include <cstdio>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace CppServer::Routers {
template <typename TContext>
class InfoRouter final : public CppServer::Services::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "INFO"; }

  std::optional<httplib::API::CachePolicy>
  ResolveCachePolicy(const std::string &method,
                     const std::string &path) const override {
    if (method != "GET" || path != "/") {
      return std::nullopt;
    }

    httplib::API::CachePolicy policy;
    policy.ttl = std::chrono::milliseconds(100);
    policy.max_entries = 16;
    return policy;
  }

  void Register(httplib::API::Router<TContext> &router) override {
    using Clock = std::chrono::steady_clock;
    using SystemClock = std::chrono::system_clock;
    using Json = nlohmann::json;

    router.Get(
        "/", "Liveness Status",
      "Get liveness, uptime, host CPU/memory usage and transport RTT",
        "Runtime health",
        [&](const httplib::Request &req, TContext &ctx) {
          const auto handler_started = Clock::now();
          ctx.request_count.fetch_add(1, std::memory_order_relaxed);

          const auto uptime_seconds =
              std::chrono::duration_cast<std::chrono::seconds>(Clock::now() -
                                                               ctx.start_time)
                  .count();

          const auto host_cpu_usage_percent = ReadHostCpuUsagePercent();
          const auto host_memory_usage = ReadHostMemoryUsage();

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
               host_cpu_usage_percent
                   ? Json(ToDisplayPercent(*host_cpu_usage_percent))
                                      : Json(nullptr)},
              {"host_memory_usage_percent",
               host_memory_usage
                   ? Json(ToDisplayPercent(host_memory_usage->usage_percent))
                                 : Json(nullptr)},
              {"host_memory_usage_share",
               host_memory_usage ? Json(FormatMemoryShare(*host_memory_usage))
                                 : Json(nullptr)},
              {"host_memory_usage",
               host_memory_usage ? Json(FormatMemoryUsageReport(*host_memory_usage))
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
  struct MemoryUsage {
    double usage_percent = 0.0;
    unsigned long long used_bytes = 0;
    unsigned long long total_bytes = 0;
  };

  static int ToDisplayPercent(double usage_percent) {
    return static_cast<int>(std::lround(std::clamp(usage_percent, 0.0, 100.0)));
  }

  static std::optional<MemoryUsage>
  BuildMemoryUsage(unsigned long long total_bytes,
                   unsigned long long available_bytes) {
    if (total_bytes == 0 || available_bytes > total_bytes) {
      return std::nullopt;
    }

    const unsigned long long used_bytes = total_bytes - available_bytes;
    const double usage_percent =
        (static_cast<double>(used_bytes) * 100.0) /
        static_cast<double>(total_bytes);

    return MemoryUsage{std::clamp(usage_percent, 0.0, 100.0), used_bytes,
                       total_bytes};
  }

  static std::string FormatMemoryShare(const MemoryUsage &memory_usage) {
    constexpr unsigned long long bytes_per_gib = 1024ULL * 1024ULL * 1024ULL;
    const auto to_rounded_gib = [](unsigned long long bytes) {
      return (bytes + bytes_per_gib / 2ULL) / bytes_per_gib;
    };

    const unsigned long long used_gib = to_rounded_gib(memory_usage.used_bytes);
    const unsigned long long total_gib = to_rounded_gib(memory_usage.total_bytes);
    return std::to_string(used_gib) + "GB/" + std::to_string(total_gib) +
           "GB";
  }

  static std::string FormatMemoryUsageReport(const MemoryUsage &memory_usage) {
    std::ostringstream stream;
    stream << ToDisplayPercent(memory_usage.usage_percent) << "% ("
           << FormatMemoryShare(memory_usage) << ")";
    return stream.str();
  }

  // CPU usage is computed from two snapshots; the first call warms up state.
  static std::optional<double>
  ComputeUsageFromCounters(unsigned long long idle_ticks,
                           unsigned long long total_ticks) {
    static std::mutex state_mutex;
    static unsigned long long previous_idle = 0;
    static unsigned long long previous_total = 0;
    static bool initialized = false;

    std::lock_guard<std::mutex> lock(state_mutex);

    if (!initialized) {
      previous_idle = idle_ticks;
      previous_total = total_ticks;
      initialized = true;
      return 0.0;
    }

    if (idle_ticks < previous_idle || total_ticks < previous_total) {
      previous_idle = idle_ticks;
      previous_total = total_ticks;
      return 0.0;
    }

    const unsigned long long idle_delta = idle_ticks - previous_idle;
    const unsigned long long total_delta = total_ticks - previous_total;

    previous_idle = idle_ticks;
    previous_total = total_ticks;

    if (total_delta == 0 || idle_delta > total_delta) {
      return 0.0;
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

  static std::optional<MemoryUsage> ReadHostMemoryUsage() {
#ifdef _WIN32
    // Windows: physical memory totals from GlobalMemoryStatusEx.
    MEMORYSTATUSEX memory_status{};
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status)) {
      return std::nullopt;
    }

    return BuildMemoryUsage(memory_status.ullTotalPhys,
                            memory_status.ullAvailPhys);
  #elif defined(__linux__)
    // Linux: read MemTotal and MemAvailable from /proc/meminfo.
    FILE *meminfo_file = std::fopen("/proc/meminfo", "r");
    if (meminfo_file == nullptr) {
      return std::nullopt;
    }

    unsigned long long total_kib = 0;
    unsigned long long available_kib = 0;
    char line[256] = {0};

    while (std::fgets(line, sizeof(line), meminfo_file) != nullptr) {
      unsigned long long value_kib = 0;
      if (std::sscanf(line, "MemTotal: %llu kB", &value_kib) == 1) {
        total_kib = value_kib;
        continue;
      }
      if (std::sscanf(line, "MemAvailable: %llu kB", &value_kib) == 1) {
        available_kib = value_kib;
      }
    }
    std::fclose(meminfo_file);

    if (total_kib == 0 || available_kib == 0) {
      return std::nullopt;
    }

    return BuildMemoryUsage(total_kib * 1024ULL, available_kib * 1024ULL);
  #elif defined(__APPLE__)
    // macOS: total from sysctl, available approximated from VM page stats.
    std::uint64_t total_bytes = 0;
    std::size_t total_size = sizeof(total_bytes);
    if (sysctlbyname("hw.memsize", &total_bytes, &total_size, nullptr, 0) !=
            0 ||
        total_bytes == 0) {
      return std::nullopt;
    }

    vm_statistics64_data_t vm_stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stats),
                          &count) != KERN_SUCCESS) {
      return std::nullopt;
    }

    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) {
      return std::nullopt;
    }

    const unsigned long long available_pages =
        static_cast<unsigned long long>(vm_stats.free_count) +
        static_cast<unsigned long long>(vm_stats.inactive_count) +
        static_cast<unsigned long long>(vm_stats.speculative_count);
    const unsigned long long available_bytes =
        available_pages * static_cast<unsigned long long>(page_size);

    return BuildMemoryUsage(
        static_cast<unsigned long long>(total_bytes),
        std::min(static_cast<unsigned long long>(total_bytes),
                 available_bytes));
#else
    return std::nullopt;
#endif
  }

};
} // namespace CppServer::Routers
