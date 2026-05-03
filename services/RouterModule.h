#pragma once

#include <string>

#include <httplib.h>

#include "API.h"

namespace CppServer {
template <typename TContext> class RouterModule {
public:
  virtual ~RouterModule() = default;

  virtual std::string RouterName() const = 0;
  virtual void Register(httplib::API::Router<TContext> &router) = 0;
};
} // namespace CppServer
