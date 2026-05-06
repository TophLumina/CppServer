#pragma once

#include <cstdint>
#include <string_view>

#include "services/files/Context.h"
#include "services/simpleapi/Context.h"

namespace CppServer::Core::ServiceTags {
struct Api {
  using Context = CppServer::Services::SimpleApi::Context;
  static constexpr std::uint16_t ID = 1;
  inline static constexpr std::string_view DisplayName = "api";
};

struct File {
  using Context = CppServer::Services::Files::Context;
  static constexpr std::uint16_t ID = 2;
  inline static constexpr std::string_view DisplayName = "file";
};
} // namespace CppServer::Core::ServiceTags