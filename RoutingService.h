#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httplib.h>

#include "RouterModule.h"

namespace CppServer::Routing {
template <typename TContext> class RoutingService {
public:
  explicit RoutingService(httplib::Server &server, TContext &context,
                          std::shared_ptr<httplib::API::Registry> api_registry =
                              std::make_shared<httplib::API::Registry>())
      : server_(server), context_(context),
        api_registry_(std::move(api_registry)) {}

  template <typename TRouter, typename... TArgs>
  TRouter &RegisterRouter(TArgs &&...args) {
    static_assert(std::is_base_of_v<RouterModule<TContext>, TRouter>,
                  "TRouter must derive from RouterModule<TContext>");

    const std::type_index router_type = std::type_index(typeid(TRouter));
    if (router_index_map_.find(router_type) != router_index_map_.end()) {
      throw std::logic_error("Router already added: " +
                             std::string(typeid(TRouter).name()));
    }

    auto instance = std::make_unique<TRouter>(std::forward<TArgs>(args)...);
    auto *router_module = instance.get();
    const std::size_t index = router_vector_.size();
    router_index_map_.emplace(router_type, index);
    router_vector_.push_back(std::move(instance));

    auto cache_policy_resolver = [router_module](const std::string &method,
                                                 const std::string &path) {
      return router_module->ResolveCachePolicy(method, path);
    };

    httplib::API::Router<TContext> router(server_, context_, api_registry_,
                                          router_module->RouterName(),
                                          std::move(cache_policy_resolver));
    router_module->Register(router);

    return static_cast<TRouter &>(*router_vector_.back());
  }

  void RegisterSwaggerUI(
      const std::string &title = "CppServer API",
      const std::string &version = "1.0.0",
      const std::string &description =
          "Auto-generated routes and response metadata docs",
      const std::string &docs_path = "/docs",
      const std::string &openapi_path = "/docs/openapi.json",
      const std::string &swagger_ui_endpoint = "",
      const std::string &docs_html_path = "docs/swagger.html") {
    httplib::API::Router<TContext> docs_router(server_, context_, api_registry_);
    docs_router.RegisterSwaggerUI(title, version, description, docs_path,
                                  openapi_path, swagger_ui_endpoint,
                                  docs_html_path);
  }

  bool MountDirectory(const std::string &mount_path,
                      const std::string &directory_path,
                      const std::string &entry_file = "") {
    httplib::API::Router<TContext> router(server_, context_, api_registry_);
    return router.MountDirectory(mount_path, directory_path, entry_file);
  }

  bool MountFile(const std::string &mount_path,
                 const std::string &file_path) {
    httplib::API::Router<TContext> router(server_, context_, api_registry_);
    return router.MountFile(mount_path, file_path);
  }

  template <typename TRouter> TRouter *FindRouter() {
    const auto found = router_index_map_.find(std::type_index(typeid(TRouter)));
    if (found == router_index_map_.end()) {
      return nullptr;
    }

    return static_cast<TRouter *>(router_vector_[found->second].get());
  }

private:
  httplib::Server &server_;
  TContext &context_;
  std::shared_ptr<httplib::API::Registry> api_registry_;
  std::vector<std::unique_ptr<RouterModule<TContext>>> router_vector_;
  std::unordered_map<std::type_index, std::size_t> router_index_map_;
};
} // namespace CppServer::Routing