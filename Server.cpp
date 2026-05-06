#include "Server.h"

#include <algorithm>

namespace CppServer::Core {
Server::Server(ServerOptions options) : context_(std::move(options)) {}

void Server::ResetRuntimeState() {
  bound_ports_.clear();
  compositor_.ResetRuntimes();
}

void Server::PrepareServiceRuntimes() { compositor_.Prepare(*this, context_); }

void Server::BindServiceRuntimes() {
  compositor_.Bind(context_.options.host, bound_ports_);
  std::sort(bound_ports_.begin(), bound_ports_.end());
}

void Server::PrintRuntimeSummary() const {
  std::cout << "Listener threads: " << bound_ports_.size()
            << ", worker threads: " << context_.options.worker_threads << "\n";
  std::cout << "Listening ports:";
  for (const int port : bound_ports_) {
    std::cout << ' ' << port;
  }
  std::cout << "\n";
}

void Server::Start() {
  ResetRuntimeState();

  PrepareServiceRuntimes();
  if (compositor_.RuntimeCount() == 0) {
    std::cerr << "No servers could be started. Exiting.\n";
    return;
  }

  BindServiceRuntimes();
  PrintRuntimeSummary();
  compositor_.Start();
  compositor_.Join();
}

void Server::Shutdown() { compositor_.Stop(); }
} // namespace CppServer::Core