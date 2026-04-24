#include <httplib.h>
#include <nlohmann/json.hpp>

#include "Core.h"
#include "Utils.h"

int main() {
  CppServer::Core::Server server(CppServer::Utils::ServerOptions{});
  server.Start();
  return 0;
}
