#pragma once

#include "CoreContext.h"
#include "Server.h"

namespace CppServer::Application {
void ConfigureApplication(CppServer::Core::Server &server,
                          const CppServer::Core::ServerOptions &options);
} // namespace CppServer::Application
