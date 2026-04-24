#include "Core.h"

#include "Services.h"

namespace CppServer::Core {
Server::Server(Utils::ServerOptions options) : context(std::move(options)) {
  servers.reserve(context.options.preferred_ports.size());
  bound_ports.reserve(context.options.preferred_ports.size());
}

void Server::Start() {
  for (const int preferred_port : context.options.preferred_ports) {
    auto server = std::make_shared<httplib::Server>();
    server->new_task_queue = [&worker_pool = context.worker_pool] {
      auto task_queue = std::make_unique<SharedTaskQueue<ThreadPool>>(worker_pool);
      return task_queue.release();
    };
    Services::RegisterRoutes(*server, context);

    int actual_port = -1;
    if (server->bind_to_port(context.options.host, preferred_port)) {
      actual_port = preferred_port;
    } else {
      actual_port = server->bind_to_any_port(context.options.host);
    }

    if (actual_port < 0) {
      std::cerr << "Failed to bind listener for preferred port "
                << preferred_port << "\n";
      continue;
    }

    bound_ports.push_back(actual_port);
    servers.push_back(std::move(server));
  }

  if (servers.empty()) {
    std::cerr << "No servers could be started. Exiting.\n";
    return;
  }

  std::cout << "Listener threads: " << servers.size()
            << ", worker threads: " << context.options.worker_threads << "\n";
  std::cout << "Listening ports:";
  for (const int port : bound_ports) {
    std::cout << ' ' << port;
  }
  std::cout << "\n";

  for (auto &server : servers) {
    listener_threads.emplace_back([server] {
      if (!server->listen_after_bind()) {
        std::cerr << "Server stopped unexpectedly.\n";
      }
    });
  }

  for (auto &th : listener_threads) {
    if (th.joinable()) {
      th.join();
    }
  }
}

void Server::Shutdown() {
  for (auto &server : servers) {
    server->stop();
  }
}
} // namespace CppServer::Core