#pragma once

#include <string>

#include "RouterModule.h"

namespace CppServer::Routers {
template <typename TContext>
class DefaultRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "DEFAULT"; }

  void Register(httplib::API::Router<TContext> &router) override {
    router.Get(
        "/", "Default Endpoint",
        "This is the default router. You can add endpoints here or create "
        "additional routers for better organization.",
        "Confirmation message", [](const httplib::Request &) { return "OK"; },
        httplib::API::RouteOptions{.content_type = "text/plain; charset=utf-8"});
  }
};
} // namespace CppServer::Routers