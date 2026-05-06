#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httplib.h>

#include "CoreContext.h"
#include "RoutingService.h"

namespace CppServer::Core {
class Server;

using ServiceTagId = std::uint16_t;
using ServiceInstanceId = std::uint16_t;
using ServiceKey = std::uint32_t;

inline constexpr ServiceInstanceId DEFAULT_SERVICE_INSTANCE_ID = 0;

namespace Detail {
template <typename TServiceTag> constexpr ServiceTagId ResolveServiceTagId() {
  static_assert(std::is_integral_v<decltype(TServiceTag::ID)>,
                "Service tags must define an integral static constexpr ID.");
  static_assert(TServiceTag::ID >= 0 &&
                    TServiceTag::ID <= std::numeric_limits<ServiceTagId>::max(),
                "Service tag IDs must fit in 16 bits.");
  return static_cast<ServiceTagId>(TServiceTag::ID);
}

constexpr ServiceKey ComposeServiceKey(const ServiceTagId tag_id,
                                       const ServiceInstanceId instance_id) {
  return (static_cast<ServiceKey>(tag_id) << 16u) |
         static_cast<ServiceKey>(instance_id);
}

template <typename TServiceTag>
constexpr ServiceKey ComposeServiceKey(const ServiceInstanceId instance_id) {
  return ComposeServiceKey(ResolveServiceTagId<TServiceTag>(), instance_id);
}

template <typename TServiceTag>
inline std::string ComposeServiceName(const ServiceInstanceId instance_id) {
  std::string service_name(TServiceTag::DisplayName);
  if (instance_id == DEFAULT_SERVICE_INSTANCE_ID) {
    return service_name;
  }

  service_name += '#';
  service_name += std::to_string(instance_id);
  return service_name;
}

template <typename TPool> class SharedTaskQueue : public httplib::TaskQueue {
public:
  explicit SharedTaskQueue(TPool &pool)
      : pool_(std::ref(pool)), shutting_down_(false) {}

  bool enqueue(std::function<void()> fn) override {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return false;
    }

    try {
      pool_.get().Submit([task = std::move(fn)]() mutable { task(); });
      return true;
    } catch (...) {
      return false;
    }
  }

  void shutdown() override {
    shutting_down_.store(true, std::memory_order_release);
  }

private:
  std::reference_wrapper<TPool> pool_;
  std::atomic<bool> shutting_down_;
};

template <typename TPool>
inline std::shared_ptr<httplib::Server> CreateHttpServer(TPool &pool) {
  auto server = std::make_shared<httplib::Server>();
  server->new_task_queue = [&pool] {
    auto task_queue =
        std::make_unique<SharedTaskQueue<::ThreadPool::ThreadPool>>(pool);
    return task_queue.release();
  };
  return server;
}
} // namespace Detail

class IComposedService {
public:
  virtual ~IComposedService() = default;

  virtual const std::string &Name() const = 0;
  virtual std::unique_ptr<class IServiceRuntimeSet>
  PrepareRuntimeSet(Server &server, CoreContext &context) const = 0;
};

class IServiceRuntimeSet {
public:
  struct RuntimeInfo {
    ServiceKey service_key = 0;
    ServiceTagId service_tag_id = 0;
    ServiceInstanceId service_instance_id = 0;
    int port = 0;
    std::string service_name;
  };

  virtual ~IServiceRuntimeSet() = default;

  virtual const std::string &Name() const = 0;
  virtual void
  BuildPortIndex(std::unordered_map<int, RuntimeInfo *> &runtime_index) = 0;
  virtual void Bind(const std::string &host, std::vector<int> &bound_ports) = 0;
  virtual void Start() = 0;
  virtual void Join() = 0;
  virtual void Stop() = 0;
  virtual std::size_t RuntimeCount() const = 0;
};

template <typename TContext>
class ServiceRuntimeSet final : public IServiceRuntimeSet {
public:
  ServiceRuntimeSet(ServiceKey service_key, ServiceTagId service_tag_id,
                    ServiceInstanceId service_instance_id, std::string name)
      : service_key_(service_key), service_tag_id_(service_tag_id),
        service_instance_id_(service_instance_id), name_(std::move(name)) {}

  const std::string &Name() const override { return name_; }

  void BuildPortIndex(
      std::unordered_map<int, RuntimeInfo *> &runtime_index) override {
    for (auto &runtime : runtimes_) {
      const auto [existing, inserted] =
          runtime_index.emplace(runtime->info.port, &runtime->info);
      if (!inserted) {
        throw std::logic_error("Duplicate runtime port configured for " +
                               runtime->info.service_name + " and " +
                               existing->second->service_name + ": " +
                               std::to_string(runtime->info.port));
      }
    }
  }

  void Reserve(const std::size_t runtime_count) {
    runtimes_.reserve(runtime_count);
  }

  void AddRuntime(
      const int port, std::shared_ptr<httplib::Server> http_server,
      std::unique_ptr<TContext> context,
      std::unique_ptr<CppServer::Routing::RoutingService<TContext>> service) {
    auto runtime = std::make_unique<Runtime>();
    runtime->info.service_key = service_key_;
    runtime->info.service_tag_id = service_tag_id_;
    runtime->info.service_instance_id = service_instance_id_;
    runtime->info.port = port;
    runtime->info.service_name = name_;
    runtime->http_server = std::move(http_server);
    runtime->context = std::move(context);
    runtime->service = std::move(service);
    runtimes_.push_back(std::move(runtime));
  }

  void Bind(const std::string &host, std::vector<int> &bound_ports) override {
    for (auto &runtime : runtimes_) {
      if (!runtime->http_server->bind_to_port(host, runtime->info.port)) {
        std::cerr << "Failed to bind listener for " << name_ << " on " << host
                  << ':' << runtime->info.port << ". Aborting.\n";
        std::abort();
      }

      bound_ports.push_back(runtime->info.port);
    }
  }

  void Start() override {
    for (auto &runtime : runtimes_) {
      auto http_server = runtime->http_server;
      runtime->listener = std::thread([http_server] {
        if (!http_server->listen_after_bind()) {
          std::cerr << "Server stopped unexpectedly.\n";
        }
      });
    }
  }

  void Join() override {
    for (auto &runtime : runtimes_) {
      if (runtime->listener.joinable()) {
        runtime->listener.join();
      }
    }
  }

  void Stop() override {
    for (auto &runtime : runtimes_) {
      if (runtime->http_server != nullptr) {
        runtime->http_server->stop();
      }
    }
  }

  std::size_t RuntimeCount() const override { return runtimes_.size(); }

private:
  struct Runtime {
    RuntimeInfo info;
    std::shared_ptr<httplib::Server> http_server;
    std::unique_ptr<TContext> context;
    std::unique_ptr<CppServer::Routing::RoutingService<TContext>> service;
    std::thread listener;
  };

  ServiceKey service_key_;
  ServiceTagId service_tag_id_;
  ServiceInstanceId service_instance_id_;
  std::string name_;
  std::vector<std::unique_ptr<Runtime>> runtimes_;
};

template <typename TContext>
class ComposedService final : public IComposedService {
public:
  using RegistrationStep = std::function<void(
      Server &, httplib::Server &,
      CppServer::Routing::RoutingService<TContext> &, TContext &, int)>;
  using ContextFactory = std::function<std::unique_ptr<TContext>(int)>;

  explicit ComposedService(ServiceKey service_key, ServiceTagId service_tag_id,
                           ServiceInstanceId service_instance_id,
                           std::string name, PortRange port_range,
                           ContextFactory context_factory = {})
      : service_key_(service_key), service_tag_id_(service_tag_id),
        service_instance_id_(service_instance_id), name_(std::move(name)),
        port_range_(port_range), context_factory_(std::move(context_factory)) {
    if (!context_factory_) {
      context_factory_ = [](int listening_port) {
        return std::make_unique<TContext>(listening_port);
      };
    }
  }

  const std::string &Name() const override { return name_; }

  const PortRange &PortRangeConfig() const { return port_range_; }

  ComposedService &SetPortRange(PortRange port_range) {
    port_range_ = port_range;
    return *this;
  }

  template <typename TContextFactory>
  ComposedService &SetContextFactory(TContextFactory &&context_factory) {
    context_factory_ =
        ContextFactory(std::forward<TContextFactory>(context_factory));
    if (!context_factory_) {
      throw std::logic_error(
          "Composed service context factory must not be empty: " + name_);
    }
    return *this;
  }

  template <typename TStep> ComposedService &AddStep(TStep &&step) {
    registration_steps_.emplace_back(std::forward<TStep>(step));
    return *this;
  }

  ComposedService &ClearSteps() {
    registration_steps_.clear();
    return *this;
  }

  template <typename TRouter, typename... TArgs>
  ComposedService &AddRouter(TArgs &&...args) {
    auto router_args = std::make_tuple(std::forward<TArgs>(args)...);
    return AddStep([router_args = std::move(router_args)](
                       Server &, httplib::Server &,
                       CppServer::Routing::RoutingService<TContext> &service,
                       TContext &, int) mutable {
      std::apply(
          [&service](auto &&...unpacked_args) {
            service.template RegisterRouter<TRouter>(
                std::forward<decltype(unpacked_args)>(unpacked_args)...);
          },
          std::move(router_args));
    });
  }

  template <typename... TArgs> ComposedService &AddSwaggerUI(TArgs &&...args) {
    auto swagger_args = std::make_tuple(std::forward<TArgs>(args)...);
    return AddStep([swagger_args = std::move(swagger_args)](
                       Server &, httplib::Server &,
                       CppServer::Routing::RoutingService<TContext> &service,
                       TContext &, int) mutable {
      std::apply(
          [&service](auto &&...unpacked_args) {
            service.RegisterSwaggerUI(
                std::forward<decltype(unpacked_args)>(unpacked_args)...);
          },
          std::move(swagger_args));
    });
  }

  template <typename... TArgs>
  ComposedService &AddMountDirectory(TArgs &&...args) {
    auto mount_args = std::make_tuple(std::forward<TArgs>(args)...);
    return AddStep([mount_args = std::move(mount_args)](
                       Server &, httplib::Server &,
                       CppServer::Routing::RoutingService<TContext> &service,
                       TContext &, int) mutable {
      std::apply(
          [&service](auto &&...unpacked_args) {
            if (!service.MountDirectory(
                    std::forward<decltype(unpacked_args)>(unpacked_args)...)) {
              throw std::logic_error("Failed to mount directory.");
            }
          },
          std::move(mount_args));
    });
  }

  template <typename... TArgs> ComposedService &AddMountFile(TArgs &&...args) {
    auto mount_args = std::make_tuple(std::forward<TArgs>(args)...);
    return AddStep([mount_args = std::move(mount_args)](
                       Server &, httplib::Server &,
                       CppServer::Routing::RoutingService<TContext> &service,
                       TContext &, int) mutable {
      std::apply(
          [&service](auto &&...unpacked_args) {
            if (!service.MountFile(
                    std::forward<decltype(unpacked_args)>(unpacked_args)...)) {
              throw std::logic_error("Failed to mount file.");
            }
          },
          std::move(mount_args));
    });
  }

  template <typename TConfigure>
  ComposedService &ConfigureHttpServer(TConfigure &&configure_http_server) {
    return AddStep([configure_http_server =
                        std::forward<TConfigure>(configure_http_server)](
                       Server &, httplib::Server &http_server,
                       CppServer::Routing::RoutingService<TContext> &,
                       TContext &context, const int listening_port) mutable {
      configure_http_server(http_server, context, listening_port);
    });
  }

  template <typename TConfigure>
  ComposedService &ConfigureService(TConfigure &&configure_service) {
    return AddStep(
        [configure_service = std::forward<TConfigure>(configure_service)](
            Server &, httplib::Server &,
            CppServer::Routing::RoutingService<TContext> &service,
            TContext &context, const int listening_port) mutable {
          configure_service(service, context, listening_port);
        });
  }

  std::unique_ptr<IServiceRuntimeSet>
  PrepareRuntimeSet(Server &server, CoreContext &context) const override {
    auto runtime_set = std::make_unique<ServiceRuntimeSet<TContext>>(
        service_key_, service_tag_id_, service_instance_id_, name_);

    if (port_range_.port_count == 0) {
      std::cerr << "No ports configured for " << name_ << " service.\n";
      return runtime_set;
    }

    runtime_set->Reserve(port_range_.port_count);
    for (std::size_t port_offset = 0; port_offset < port_range_.port_count;
         ++port_offset) {
      const int listening_port =
          port_range_.port_begin + static_cast<int>(port_offset);

      auto http_server = Detail::CreateHttpServer(context.worker_pool);
      auto service_context = context_factory_(listening_port);
      if (service_context == nullptr) {
        throw std::logic_error(
            "Composed service context factory returned null: " + name_);
      }

      auto service =
          std::make_unique<CppServer::Routing::RoutingService<TContext>>(
              *http_server, *service_context);

      for (auto &step : registration_steps_) {
        step(server, *http_server, *service, *service_context, listening_port);
      }

      runtime_set->AddRuntime(listening_port, std::move(http_server),
                              std::move(service_context), std::move(service));
    }

    return runtime_set;
  }

private:
  ServiceKey service_key_;
  ServiceTagId service_tag_id_;
  ServiceInstanceId service_instance_id_;
  std::string name_;
  PortRange port_range_;
  ContextFactory context_factory_;
  std::vector<RegistrationStep> registration_steps_;
};

class Compositor {
public:
  template <typename TServiceTag>
  ComposedService<typename TServiceTag::Context> &
  Compose(PortRange port_range) {
    return Compose<TServiceTag>(DEFAULT_SERVICE_INSTANCE_ID, port_range);
  }

  template <typename TServiceTag>
  ComposedService<typename TServiceTag::Context> &
  Compose(const ServiceInstanceId instance_id, PortRange port_range) {
    using TContext = typename TServiceTag::Context;
    return Compose<TServiceTag>(
        instance_id, port_range,
        typename ComposedService<TContext>::ContextFactory{});
  }

  template <typename TServiceTag, typename TContextFactory>
  ComposedService<typename TServiceTag::Context> &
  Compose(const ServiceInstanceId instance_id, PortRange port_range,
          TContextFactory &&context_factory) {
    using TContext = typename TServiceTag::Context;
    const ServiceTagId service_tag_id =
        Detail::ResolveServiceTagId<TServiceTag>();
    const ServiceKey service_key =
        Detail::ComposeServiceKey(service_tag_id, instance_id);
    auto service = std::make_unique<ComposedService<TContext>>(
        service_key, service_tag_id, instance_id,
        Detail::ComposeServiceName<TServiceTag>(instance_id), port_range,
        typename ComposedService<TContext>::ContextFactory(
            std::forward<TContextFactory>(context_factory)));
    auto *service_ptr = service.get();

    const auto existing = service_index_map_.find(service_key);
    if (existing != service_index_map_.end()) {
      services_[existing->second] = std::move(service);
      return *service_ptr;
    }

    service_index_map_.emplace(service_key, services_.size());
    services_.push_back(std::move(service));
    return *service_ptr;
  }

  template <typename TServiceTag>
  ComposedService<typename TServiceTag::Context> *
  Find(const ServiceInstanceId instance_id = DEFAULT_SERVICE_INSTANCE_ID) {
    using TContext = typename TServiceTag::Context;
    const auto existing = service_index_map_.find(
        Detail::ComposeServiceKey<TServiceTag>(instance_id));
    if (existing == service_index_map_.end()) {
      return nullptr;
    }

    return static_cast<ComposedService<TContext> *>(
        services_[existing->second].get());
  }

  template <typename TServiceTag>
  const ComposedService<typename TServiceTag::Context> *Find(
      const ServiceInstanceId instance_id = DEFAULT_SERVICE_INSTANCE_ID) const {
    using TContext = typename TServiceTag::Context;
    const auto existing = service_index_map_.find(
        Detail::ComposeServiceKey<TServiceTag>(instance_id));
    if (existing == service_index_map_.end()) {
      return nullptr;
    }

    return static_cast<const ComposedService<TContext> *>(
        services_[existing->second].get());
  }

  void Clear() {
    services_.clear();
    service_index_map_.clear();
    runtime_sets_.clear();
  }

  bool Empty() const { return services_.empty(); }

  IServiceRuntimeSet::RuntimeInfo *FindRuntime(const int port) {
    const auto existing = runtime_index_.find(port);
    if (existing == runtime_index_.end()) {
      return nullptr;
    }

    return existing->second;
  }

  const IServiceRuntimeSet::RuntimeInfo *FindRuntime(const int port) const {
    const auto existing = runtime_index_.find(port);
    if (existing == runtime_index_.end()) {
      return nullptr;
    }

    return existing->second;
  }

  void ResetRuntimes() {
    runtime_index_.clear();
    runtime_sets_.clear();
  }

  void Prepare(Server &server, CoreContext &context) {
    ResetRuntimes();
    runtime_index_.reserve(context.options.service_port_overrides.size());
    runtime_sets_.reserve(services_.size());
    for (const auto &service : services_) {
      auto runtime_set = service->PrepareRuntimeSet(server, context);
      runtime_set->BuildPortIndex(runtime_index_);
      runtime_sets_.push_back(std::move(runtime_set));
    }
  }

  void Bind(const std::string &host, std::vector<int> &bound_ports) {
    for (auto &runtime_set : runtime_sets_) {
      runtime_set->Bind(host, bound_ports);
    }
  }

  void Start() {
    for (auto &runtime_set : runtime_sets_) {
      runtime_set->Start();
    }
  }

  void Join() {
    for (auto &runtime_set : runtime_sets_) {
      runtime_set->Join();
    }
  }

  void Stop() {
    for (auto &runtime_set : runtime_sets_) {
      runtime_set->Stop();
    }
  }

  std::size_t RuntimeCount() const {
    std::size_t runtime_count = 0;
    for (const auto &runtime_set : runtime_sets_) {
      runtime_count += runtime_set->RuntimeCount();
    }
    return runtime_count;
  }

private:
  std::vector<std::unique_ptr<IComposedService>> services_;
  std::unordered_map<ServiceKey, std::size_t> service_index_map_;
  std::unordered_map<int, IServiceRuntimeSet::RuntimeInfo *> runtime_index_;
  std::vector<std::unique_ptr<IServiceRuntimeSet>> runtime_sets_;
};
} // namespace CppServer::Core