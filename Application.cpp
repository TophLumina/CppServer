#include "Application.h"

#include "ServiceTags.h"
#include "services/files/Runtime.h"
#include "services/simpleapi/DefaultRouter.h"
#include "services/simpleapi/SampleRouter.h"
#include "services/simpleapi/StatusRouter.h"

namespace CppServer::Application {
namespace {
template <typename TServiceContext>
CppServer::Core::PortRange
ResolveServicePortRange(const CppServer::Core::ServerOptions &options,
                        const std::size_t range_index) {
  if (range_index < options.service_port_overrides.size()) {
    return options.service_port_overrides[range_index];
  }

  return CppServer::Core::PortRange{TServiceContext::DEFAULT_PORT_BEGIN,
                                    TServiceContext::DEFAULT_PORT_COUNT};
}
} // namespace

void ConfigureApplication(CppServer::Core::Server &server,
                          const CppServer::Core::ServerOptions &options) {
  using ApiServiceTag = CppServer::Core::ServiceTags::Api;
  using FileServiceTag = CppServer::Core::ServiceTags::File;
  using ApiServiceContext = ApiServiceTag::Context;
  using FileServiceContext = FileServiceTag::Context;

  auto &compositor = server.Composition();
  compositor.Clear();

  compositor
      .Compose<ApiServiceTag>(
          CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
        ResolveServicePortRange<ApiServiceContext>(options, 0))
      .AddRouter<CppServer::Routers::DefaultRouter<ApiServiceContext>>()
      .AddRouter<CppServer::Routers::StatusRouter<ApiServiceContext>>()
      .AddRouter<CppServer::Routers::SampleRouter<ApiServiceContext>>()
      .AddSwaggerUI();

  compositor
      .Compose<FileServiceTag>(
          CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
          ResolveServicePortRange<FileServiceContext>(options, 1))
      .ConfigureHttpServer([](httplib::Server &http_server,
                              FileServiceContext &file_context, int) {
        CppServer::Services::Files::ConfigureRuntime(http_server, file_context);
      });
}
} // namespace CppServer::Application
