#include "Application.h"

int main() {
  CppServer::Core::ServerOptions options;
  CppServer::Core::Server server(options);
  CppServer::Application::ConfigureApplication(server, options);
  server.Start();
  return 0;
}