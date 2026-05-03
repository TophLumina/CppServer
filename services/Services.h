#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httplib.h>

#include "RouterModule.h"

namespace CppServer::Services {
template <typename TContext> class Services {
public:
  explicit Services(httplib::Server &server, TContext &context,
                    std::shared_ptr<httplib::API::Registry> api_registry =
                        std::make_shared<httplib::API::Registry>())
      : server_(server), context_(context),
        api_registry_(std::move(api_registry)) {}

  template <typename TRouter, typename... TArgs>
  TRouter &AddRouter(TArgs &&...args) {
    static_assert(std::is_base_of_v<RouterModule<TContext>, TRouter>,
                  "TRouter must derive from RouterModule<TContext>");

    const std::type_index router_type = std::type_index(typeid(TRouter));
    if (router_index_map_.find(router_type) != router_index_map_.end()) {
      throw std::logic_error("Router already added: " +
                             std::string(typeid(TRouter).name()));
    }

    auto instance = std::make_unique<TRouter>(std::forward<TArgs>(args)...);
    const std::size_t index = router_vector_.size();
    router_index_map_.emplace(router_type, index);
    router_vector_.push_back(std::move(instance));
    return static_cast<TRouter &>(*router_vector_.back());
  }

  template <typename TRouter> TRouter *FindRouter() {
    const auto found = router_index_map_.find(std::type_index(typeid(TRouter)));
    if (found == router_index_map_.end()) {
      return nullptr;
    }

    return static_cast<TRouter *>(router_vector_[found->second].get());
  }

  void RegisterAllRoutes() {
    for (const auto &router_module : router_vector_) {
      httplib::API::Router<TContext> router(server_, context_, api_registry_,
                                            router_module->RouterName());
      router_module->Register(router);
    }
  }

  std::shared_ptr<httplib::API::Registry> ApiRegistry() const {
    return api_registry_;
  }

private:
  httplib::Server &server_;
  TContext &context_;
  std::shared_ptr<httplib::API::Registry> api_registry_;
  std::vector<std::unique_ptr<RouterModule<TContext>>> router_vector_;
  std::unordered_map<std::type_index, std::size_t> router_index_map_;
};
} // namespace CppServer::Services