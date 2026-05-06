#pragma once

#include <vector>

#include "Compositor.h"
#include "CoreContext.h"

namespace CppServer::Core {
class Server {
public:
  explicit Server(ServerOptions options);

  CppServer::Core::Compositor &Composition() { return compositor_; }
  const CppServer::Core::Compositor &Composition() const { return compositor_; }
  void Start();
  void Shutdown();

private:
  void ResetRuntimeState();
  void PrepareServiceRuntimes();
  void BindServiceRuntimes();
  void PrintRuntimeSummary() const;

  CoreContext context_;
  CppServer::Core::Compositor compositor_;
  std::vector<int> bound_ports_;
};
} // namespace CppServer::Core
