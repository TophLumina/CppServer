# CppServer

> 一个便于二次开发的 C++ HTTP 服务骨架，带类型化路由、静态文件服务与线程池执行模型。  
> An extension-friendly C++ HTTP service skeleton with typed routing, static file serving, and thread-pool-backed execution.

License: [MIT](LICENSE)

## 中文

### 1. 项目介绍

这是一个基于 `cpp-httplib`、`nlohmann/json` 和自定义线程池适配的轻量级 C++ HTTP 服务骨架。当前默认提供两个 service：

- `api`：`/`、`/status`、`/sample/*`、`/docs`
- `file`：静态文件挂载，默认服务 `mount/`

核心结构：

- `Application::ConfigureApplication(...)`：应用装配入口
- `Server`：生命周期外壳
- `Compositor`：service 蓝图与 runtime 编排
- `RoutingService<TContext>` / `RouterModule<TContext>`：路由层抽象

功能特点：

- service 蓝图和 runtime 实例分离，便于理解“如何装配”和“如何运行”
- 使用 `ServiceTag + instanceId` 标识 service，避免字符串式误用
- 文件 service 支持“启动时挂载 + 运行期变化感知”
- API 路由可自动生成 OpenAPI 与 Swagger UI
- 请求处理接入线程池，适合轻量并发服务

### 2. 如何构建、部署、测试

本地构建：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

本地运行：

```powershell
.\build\server.exe
```

或：

```powershell
Set-Location build
.\server.exe
```

Docker：

```powershell
docker build -t cpp-server .
docker run -d --rm --name cpp-server -p 8080:8080 -p 8081:8081 cpp-server
```

平台说明：

- 已验证：Windows 本地、Linux Docker
- 保留 macOS / Apple Silicon 对应分支，但当前没有 macOS CI 或 Apple 实机验证，现阶段属于“源码保留、未验证支持”

测试：

- `threadpool_smoke`：线程池基础可用性
- `api_service_integration`：默认 `api` service 装配与基础链路，覆盖 `/`、`/status`、`/docs/openapi.json`、`/sample/randomint`
- `file_service_integration`：文件 service 基础链路，覆盖延迟挂载生效、缓存刷新、符号链接逃逸拒绝

```powershell
ctest --test-dir build --output-on-failure
ctest --test-dir build -R api_service_integration --output-on-failure
ctest --test-dir build -R file_service_integration --output-on-failure
curl http://127.0.0.1:8080/
curl http://127.0.0.1:8080/status
curl http://127.0.0.1:8080/docs
curl http://127.0.0.1:8081/hello.txt
```

端口覆盖示例：

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

并发预算示例：

```cpp
CppServer::Core::ServerOptions options;
options.worker_threads = 16;
options.max_inflight_connection_tasks = 64;

CppServer::Core::Server server(options);
CppServer::Application::ConfigureApplication(server, options);
server.Start();
```

说明：

- `max_inflight_connection_tasks` 会传入 `TaskQueueBudget`，作为全局连接任务预算，被所有 service 共享
- 这个值限制的是 inflight connection task 总数，也就是“排队中 + 运行中”的连接任务，不是 HTTP request 数量
- 设为 `0` 表示不限制；当前默认值是 `4 * WORKER_THREADS`
- 达到上限后，新连接任务会在 task queue 边界被拒绝，而不是继续无限排队

### 3. 二次开发指南和样例

建议先对照这些现有文件再开始改：

- `Application.cpp`：应用装配入口，新增 router / service 最终都要落到这里
- `ServiceTags.h`：service 的 `Context`、`ID`、`DisplayName` 定义
- `services/simpleapi/Context.h`：默认 `api` service 的上下文结构
- `services/simpleapi/DefaultRouter.h` / `services/simpleapi/StatusRouter.h` / `services/simpleapi/SampleRouter.h`：现有 router 写法
- `services/files/Runtime.h`：不走 `AddRouter(...)`、直接配置底层 `httplib::Server` 的现成例子
- `RouterModule.h`：router 需要实现的接口定义

#### 3.1 在已有 `api` service 下新增一个路由

目标：新增 `GET /time`，同时让它自动出现在 `http://127.0.0.1:8080/docs`。

步骤：

1. 新建文件 `services/simpleapi/TimeRouter.h`
2. 参考 `services/simpleapi/DefaultRouter.h` 或 `services/simpleapi/SampleRouter.h`，实现一个继承 `RouterModule<TContext>` 的 router
3. 修改 `Application.cpp`，先 `#include "services/simpleapi/TimeRouter.h"`，再在 `api` service 的装配链上追加 `.AddRouter<...>()`
4. 重新构建并访问 `/time`、`/docs`

新文件：`services/simpleapi/TimeRouter.h`

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

修改文件：`Application.cpp`

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

说明：

- `RouterModule<TContext>` 的最小接口只有 `RouterName()` 和 `Register(...)`，定义见 `RouterModule.h`
- handler 可以只接收 `const httplib::Request &`，也可以像上例一样再接收 `TContext &ctx` 读取运行时上下文
- 现有 `api` service 已在 `Application.cpp` 调用了 `.AddSwaggerUI()`，所以新 router 注册成功后会自动进入 `/docs`
- 如果这个新接口要共享状态，优先把状态放到 `services/simpleapi/Context.h` 的 `Context` 里，而不是放全局变量

验证：

```powershell
curl http://127.0.0.1:8080/time
curl http://127.0.0.1:8080/docs
```

#### 3.2 新增一个新的 routing service

目标：新增一个独立的 `admin` service，监听 `8090`，提供 `GET /admin/ping`。

步骤：

1. 新建 `services/admin/Context.h`，定义这个 service 自己的 runtime 上下文和默认端口
2. 新建 `services/admin/AdminRouter.h`，写该 service 的路由
3. 修改 `ServiceTags.h`，把新 service 暴露成一个新的 `ServiceTag`
4. 修改 `Application.cpp`，先包含新 router 头文件，再通过 `.Compose<AdminServiceTag>(...)` 把它装配进去
5. 如果你使用 `ServerOptions::service_port_overrides`，记得给新 service 预留新的下标；按当前 `Application.cpp` 的装配顺序，`api -> file -> admin` 分别对应 `0 / 1 / 2`

新文件：`services/admin/Context.h`

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

新文件：`services/admin/AdminRouter.h`

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

修改文件：`ServiceTags.h`

```cpp
#include "services/admin/Context.h"

struct Admin {
  using Context = CppServer::Services::Admin::Context;
  static constexpr std::uint16_t ID = 10;
  inline static constexpr std::string_view DisplayName = "admin";
};
```

修改文件：`Application.cpp`

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

验证：

```powershell
curl http://127.0.0.1:8090/admin/ping
curl http://127.0.0.1:8090/docs
```

#### 3.3 什么时候用 `AddRouter(...)`，什么时候用 `ConfigureHttpServer(...)`

- 普通 JSON / 文本接口：优先走 `RouterModule<TContext>` + `.AddRouter<...>()`
- 想直接挂静态目录、`pre_routing_handler`、底层 `httplib::Server` 行为：参考 `services/files/Runtime.h`，在 `Application.cpp` 里使用 `.ConfigureHttpServer(...)`

最小示意：

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

二开建议：

- 尽量把状态放在 runtime 自己的 `TContext` 中
- 如需共享状态，自己负责加锁或使用原子
- `ConfigureHttpServer(...)` 适合挂底层行为
- `ConfigureService(...)` 适合做 service 级装配

### 4. 项目主要类结构和模板行为

启动链路：

```text
main.cpp
  -> Server
  -> Application::ConfigureApplication(...)
  -> Compositor.Compose<TServiceTag>(...)
  -> ComposedService<TContext>  // 蓝图
  -> Server.Start()
  -> ServiceRuntimeSet<TContext> // 运行集合
```

关键点：

- `ComposedService<TContext>` 是蓝图，不直接监听端口
- `ServiceRuntimeSet<TContext>` 是蓝图实例化后的运行集合，一个 service 可对应多个端口 runtime
- `TServiceTag` 至少需要提供 `Context`、`ID`、`DisplayName`
- `TContext` 是每个 runtime 的上下文，不同 service 可有不同结构
- `ServiceKey = (ServiceTagId << 16) | ServiceInstanceId`

建议阅读顺序：`main.cpp` -> `Application.cpp` -> `Server.*` -> `Compositor.h` -> `RoutingService.h` -> `services/*`

---

## English

### 1. Overview

This is a lightweight C++ HTTP service skeleton built on `cpp-httplib`, `nlohmann/json`, and a custom thread-pool adapter. It ships with two default services:

- `api`: `/`, `/status`, `/sample/*`, `/docs`
- `file`: static file mount from `mount/`

Core structure:

- `Application::ConfigureApplication(...)`: application composition entry
- `Server`: lifecycle shell
- `Compositor`: service blueprint and runtime orchestration
- `RoutingService<TContext>` / `RouterModule<TContext>`: routing abstraction

Highlights:

- clear separation between service blueprints and runtime instances
- `ServiceTag + instanceId` identity model instead of string-only lookup
- file service supports startup mount plus runtime change awareness
- API routes can generate OpenAPI and Swagger UI automatically
- request execution is backed by a thread pool

### 2. Build, Deploy, Test

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
- `file_service_integration`: file service basics covering late mount activation, cached file refresh, and symlink escape rejection

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

### 3. Extension Guide and Examples

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

Extension advice:

- keep state runtime-local in `TContext` when possible
- synchronize shared mutable state yourself
- use `ConfigureHttpServer(...)` for low-level server behavior
- use `ConfigureService(...)` for service-level composition

### 4. Main Structure and Template Behavior

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
