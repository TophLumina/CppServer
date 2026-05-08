# CppServer

> An extension-friendly C++ HTTP service skeleton with typed routing, static file serving, and thread-pool-backed execution.

中文文档: [README.zh_CN.md](./README.zh_CN.md)

License: [MIT](LICENSE)

## 1. Overview

This is a lightweight C++ HTTP service skeleton built on `cpp-httplib`, `nlohmann/json`, and a custom thread-pool adapter. It ships with two default services:

- `api`: `/`, `/status`, `/sample/*`, `/docs`
- `file`: static file mount from `mount/`; directory requests serve `index.html` first and fall back to a templated directory listing when absent

Core structure:

- `Application::ConfigureApplication(...)`: application composition entry
- `Server`: lifecycle shell
- `Compositor`: service blueprint and runtime orchestration
- `RoutingService<TContext>` / `RouterModule<TContext>`: routing abstraction

Highlights:

- clear separation between service blueprints and runtime instances
- `ServiceTag + instanceId` identity model instead of string-only lookup
- file service supports startup mount plus runtime change awareness
- directory requests try the directory-local `index.html` first, then fall back to `resources/directory-index-template.html`
- the file service directory-listing template path is configurable per service instance through `services/files/Context.h`
- API routers can declare route-level response cache policies and inject them through router constructor arguments
- API routes can generate OpenAPI and Swagger UI automatically
- request execution is backed by a thread pool

## 2. Build, Deploy, Test

Build locally:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run locally:

```powershell
.\build\server.exe
```

Or:

```powershell
Set-Location build
.\server.exe
```

Docker:

```powershell
docker build -t cpp-server .
docker run -d --rm --name cpp-server -p 8080:8080 -p 8081:8081 cpp-server
```

Platform note:

- Verified: local Windows, Linux via Docker
- macOS / Apple Silicon branches are still kept in the codebase, but there is currently no macOS CI or Apple hardware validation, so treat this as retained but unverified support

Test:

- `threadpool_smoke`: basic thread-pool availability
- `api_service_integration`: default `api` service wiring and basic request paths covering `/`, `/status`, `/docs/openapi.json`, and `/sample/randomint`
- `file_service_integration`: file service basics covering late mount activation, cached file refresh, directory requests preferring `index.html`, directory-listing fallback when `index.html` is absent, custom directory-listing template paths, and symlink escape rejection

```powershell
ctest --test-dir build --output-on-failure
ctest --test-dir build -R api_service_integration --output-on-failure
ctest --test-dir build -R file_service_integration --output-on-failure
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8081/hello.txt
```

Port override example:

```cpp
CppServer::Core::ServerOptions options;
options.service_port_overrides = {
    {9000, 1},
    {9001, 1},
};

CppServer::Core::Server server(options);
CppServer::Application::ConfigureApplication(server, options);
server.Start();
```

Concurrency budget example:

```cpp
CppServer::Core::ServerOptions options;
options.worker_threads = 16;
options.max_inflight_connection_tasks = 64;

CppServer::Core::Server server(options);
CppServer::Application::ConfigureApplication(server, options);
server.Start();
```

Notes:

- `max_inflight_connection_tasks` is passed into `TaskQueueBudget` as a global connection-task budget shared by all services
- the limit applies to total inflight connection tasks, meaning queued plus running connection tasks, not HTTP request count
- setting it to `0` disables the limit; the current default is `4 * WORKER_THREADS`
- once the limit is reached, new connection tasks are rejected at the task-queue boundary instead of being queued without bound

## 3. Extension Guide and Examples

Start from these files before extending the project:

- `Application.cpp`: composition entry where new routers and services are wired
- `ServiceTags.h`: defines `Context`, `ID`, and `DisplayName` for each service
- `services/simpleapi/Context.h`: runtime context of the default `api` service
- `services/simpleapi/DefaultRouter.h` / `services/simpleapi/StatusRouter.h` / `services/simpleapi/SampleRouter.h`: concrete router examples
- `services/files/Runtime.h`: example of configuring raw `httplib::Server` behavior instead of adding routers
- `RouterModule.h`: router interface definition

#### 3.1 Add a router to the existing `api` service

Goal: add `GET /time` and have it appear automatically in `http://127.0.0.1:8080/docs`.

Steps:

1. create `services/simpleapi/TimeRouter.h`
2. implement a router derived from `RouterModule<TContext>` by following `services/simpleapi/DefaultRouter.h` or `services/simpleapi/SampleRouter.h`
3. update `Application.cpp`: add `#include "services/simpleapi/TimeRouter.h"` and append `.AddRouter<...>()` to the `api` composition chain
4. rebuild and verify `/time` and `/docs`

New file: `services/simpleapi/TimeRouter.h`

```cpp
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "RouterModule.h"

namespace CppServer::Routers {
template <typename TContext>
class TimeRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "TIME"; }

  void Register(httplib::API::Router<TContext> &router) override {
    using Json = nlohmann::json;

    router.Get(
        "/time", "Server Time",
        "Return the current service port to verify router wiring.",
        "Simple JSON response",
        [](const httplib::Request &, TContext &ctx) {
          return Json{{"ok", true}, {"port", ctx.port}};
        },
        httplib::API::RouteOptions{});
  }
};
} // namespace CppServer::Routers
```

Updated file: `Application.cpp`

```cpp
#include "services/simpleapi/TimeRouter.h"

compositor
    .Compose<ApiServiceTag>(
        CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
        ResolveServicePortRange<ApiServiceContext>(options, 0))
    .AddRouter<CppServer::Routers::DefaultRouter<ApiServiceContext>>()
    .AddRouter<CppServer::Routers::StatusRouter<ApiServiceContext>>()
    .AddRouter<CppServer::Routers::SampleRouter<ApiServiceContext>>()
    .AddRouter<CppServer::Routers::TimeRouter<ApiServiceContext>>()
    .AddSwaggerUI();
```

Notes:

- `RouterModule<TContext>` only requires `RouterName()` and `Register(...)`; see `RouterModule.h`
- handlers can accept only `const httplib::Request &`, or accept `TContext &ctx` as shown above when runtime state is needed
- the existing `api` service already calls `.AddSwaggerUI()` in `Application.cpp`, so newly registered routers are included in `/docs`
- if a new route needs shared runtime state, prefer extending `services/simpleapi/Context.h` over introducing global variables

#### 3.1.1 API Cache Policy Injection

The injection point is the router, not `ServerOptions`: override `RouterModule<TContext>::ResolveCachePolicy(method, path)` and return `std::optional<httplib::API::CachePolicy>` per route. Returning `std::nullopt`, or a policy with `ttl <= 0`, disables caching for that route.

The current repository already includes a minimal example in `services/simpleapi/StatusRouter.h`: it only enables caching for `GET /status`.

If you want to inject cache policy from `Application.cpp`, let the router constructor accept `httplib::API::CachePolicy`, then pass it through `.AddRouter<TRouter>(args...)`. The `Compositor` forwards those arguments to the router constructor unchanged.

Example:

```cpp
template <typename TContext>
class TimeRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  explicit TimeRouter(httplib::API::CachePolicy cache_policy = {})
      : cache_policy_(std::move(cache_policy)) {}

  std::string RouterName() const override { return "TIME"; }

  std::optional<httplib::API::CachePolicy>
  ResolveCachePolicy(const std::string &method,
                     const std::string &path) const override {
    if (method == "GET" && path == "/time" && cache_policy_.ttl.count() > 0) {
      return cache_policy_;
    }
    return std::nullopt;
  }

  void Register(httplib::API::Router<TContext> &router) override {
    using Json = nlohmann::json;

    router.Get(
        "/time", "Server Time",
        "Return the current service port to verify router wiring.",
        "Simple JSON response",
        [](const httplib::Request &, TContext &ctx) {
          return Json{{"ok", true}, {"port", ctx.port}};
        },
        httplib::API::RouteOptions{});
  }

private:
  httplib::API::CachePolicy cache_policy_;
};

httplib::API::CachePolicy time_cache;
time_cache.ttl = std::chrono::seconds(1);
time_cache.query_fields = {"tz"};
time_cache.max_entries = 64;

compositor
    .Compose<ApiServiceTag>(
        CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
        ResolveServicePortRange<ApiServiceContext>(options, 0))
    .AddRouter<CppServer::Routers::DefaultRouter<ApiServiceContext>>()
    .AddRouter<CppServer::Routers::StatusRouter<ApiServiceContext>>()
    .AddRouter<CppServer::Routers::SampleRouter<ApiServiceContext>>()
    .AddRouter<TimeRouter<ApiServiceContext>>(time_cache)
    .AddSwaggerUI();
```

Key fields:

- `ttl`: cache lifetime; values less than or equal to `0` disable caching
- `query_fields`: query parameters that participate in the cache key
- `header_fields`: request headers that participate in the cache key
- `max_entries`: per-route cache entry limit
- `max_payload_bytes`: maximum response body size eligible for caching
- `cache_error_response`: whether to cache error responses; `5xx` is not cached by default

Guidance:

- treat this as a route-level declaration, not a global API cache switch
- if a route varies by query or header, include those fields in the cache key
- this mechanism is best suited for frequent health checks, read-only configuration endpoints, and short-lived aggregate responses

Validation:

```powershell
curl http://127.0.0.1:8080/time
curl http://127.0.0.1:8080/docs
```

#### 3.2 Add a new routing service

Goal: add a dedicated `admin` service on port `8090` with `GET /admin/ping`.

Steps:

1. create `services/admin/Context.h` for the runtime context and default port
2. create `services/admin/AdminRouter.h` for the routes
3. update `ServiceTags.h` to expose the new service as a `ServiceTag`
4. update `Application.cpp` to include the new router and compose the new service with `.Compose<AdminServiceTag>(...)`
5. if you use `ServerOptions::service_port_overrides`, reserve a new index for the service; with the current composition order in `Application.cpp`, `api -> file -> admin` maps to `0 / 1 / 2`

New file: `services/admin/Context.h`

```cpp
#pragma once

#include <cstddef>

namespace CppServer::Services::Admin {
struct Context {
  static constexpr int DEFAULT_PORT_BEGIN = 8090;
  static constexpr std::size_t DEFAULT_PORT_COUNT = 1;

  explicit Context(int listening_port = DEFAULT_PORT_BEGIN)
      : port(listening_port) {}

  int port;
};
} // namespace CppServer::Services::Admin
```

New file: `services/admin/AdminRouter.h`

```cpp
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "RouterModule.h"

namespace CppServer::Routers {
template <typename TContext>
class AdminRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "ADMIN"; }

  void Register(httplib::API::Router<TContext> &router) override {
    using Json = nlohmann::json;

    router.Get(
        "/admin/ping", "Admin Ping", "Health check for admin service.",
        "Simple JSON response",
        [](const httplib::Request &, TContext &ctx) {
          return Json{{"service", "admin"}, {"port", ctx.port}};
        },
        httplib::API::RouteOptions{});
  }
};
} // namespace CppServer::Routers
```

Updated file: `ServiceTags.h`

```cpp
#include "services/admin/Context.h"

struct Admin {
  using Context = CppServer::Services::Admin::Context;
  static constexpr std::uint16_t ID = 10;
  inline static constexpr std::string_view DisplayName = "admin";
};
```

Updated file: `Application.cpp`

```cpp
#include "services/admin/AdminRouter.h"

using AdminServiceTag = CppServer::Core::ServiceTags::Admin;
using AdminServiceContext = AdminServiceTag::Context;

compositor
    .Compose<AdminServiceTag>(
        CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
        ResolveServicePortRange<AdminServiceContext>(options, 2))
    .AddRouter<CppServer::Routers::AdminRouter<AdminServiceContext>>()
    .AddSwaggerUI();
```

Validation:

```powershell
curl http://127.0.0.1:8090/admin/ping
curl http://127.0.0.1:8090/docs
```

#### 3.3 When to use `AddRouter(...)` vs `ConfigureHttpServer(...)`

- Regular JSON or text endpoints: prefer `RouterModule<TContext>` plus `.AddRouter<...>()`
- Static directory serving, `pre_routing_handler`, or other raw `httplib::Server` behavior: follow `services/files/Runtime.h` and wire it in `Application.cpp` with `.ConfigureHttpServer(...)`

Minimal pattern:

```cpp
compositor
    .Compose<FileServiceTag>(
        CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
        ResolveServicePortRange<FileServiceContext>(options, 1))
    .ConfigureHttpServer([](httplib::Server &http_server,
                            FileServiceContext &file_context, int) {
      CppServer::Services::Files::ConfigureRuntime(http_server, file_context);
    });
```

If you want to explicitly set both the mount path and the directory-listing template path for the file service, use:

```cpp
compositor
  .Compose<FileServiceTag>(
    CppServer::Core::DEFAULT_SERVICE_INSTANCE_ID,
    ResolveServicePortRange<FileServiceContext>(options, 1),
    [](int listening_port) {
      return std::make_unique<FileServiceContext>(
        listening_port,
        "mount",
        "resources/directory-index-template.html");
    })
  .ConfigureHttpServer([](httplib::Server &http_server,
              FileServiceContext &file_context, int) {
    CppServer::Services::Files::ConfigureRuntime(http_server, file_context);
  });
```

Behavior summary:

- directory requests first try to serve the directory-local `index.html`
- if `index.html` is absent, the server renders the current directory through the listing template instead
- the default directory-listing template lives at `resources/directory-index-template.html`

Extension advice:

- keep state runtime-local in `TContext` when possible
- synchronize shared mutable state yourself
- use `ConfigureHttpServer(...)` for low-level server behavior
- use `ConfigureService(...)` for service-level composition

## 4. Main Structure and Template Behavior

Runtime flow:

```text
main.cpp
  -> Server
  -> Application::ConfigureApplication(...)
  -> Compositor.Compose<TServiceTag>(...)
  -> ComposedService<TContext>  // blueprint
  -> Server.Start()
  -> ServiceRuntimeSet<TContext> // runtime set
```

Key points:

- `ComposedService<TContext>` is a blueprint, not a live listener
- `ServiceRuntimeSet<TContext>` is the realized runtime set and may contain multiple port runtimes
- `TServiceTag` should provide `Context`, `ID`, and `DisplayName`
- `TContext` is per-runtime state
- `ServiceKey = (ServiceTagId << 16) | ServiceInstanceId`

Suggested reading order: `main.cpp` -> `Application.cpp` -> `Server.*` -> `Compositor.h` -> `RoutingService.h` -> `services/*`
