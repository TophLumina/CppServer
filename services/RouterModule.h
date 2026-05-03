#pragma once

#include <optional>
#include <string>

#include <httplib.h>

#include "API.h"

namespace CppServer {
template <typename TContext> class RouterModule {
public:
  virtual ~RouterModule() = default;

  virtual std::string RouterName() const = 0;
  virtual void Register(httplib::API::Router<TContext> &router) = 0;
  virtual std::optional<httplib::API::CachePolicy>
  ResolveCachePolicy(const std::string &method, const std::string &path) const {
    (void)method;
    (void)path;
    return std::nullopt;
  }
};
} // namespace CppServer
