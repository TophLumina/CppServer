# CppServer

> 一个便于二次开发的 C++ HTTP 服务骨架，带类型化路由、静态文件服务与线程池执行模型。

English README: [README.md](./README.md)

License: [MIT](LICENSE)

## 1. 项目介绍

这是一个基于 `cpp-httplib`、`nlohmann/json` 和自定义线程池适配的轻量级 C++ HTTP 服务骨架。当前默认提供两个 service：

- `api`：`/`、`/status`、`/sample/*`、`/docs`
- `file`：静态文件挂载，默认服务 `mount/`；目录请求优先返回 `index.html`，缺失时回退到模板化目录列表

核心结构：

- `Application::ConfigureApplication(...)`：应用装配入口
- `Server`：生命周期外壳
- `Compositor`：service 蓝图与 runtime 编排
- `RoutingService<TContext>` / `RouterModule<TContext>`：路由层抽象

功能特点：

- service 蓝图和 runtime 实例分离，便于理解“如何装配”和“如何运行”
- 使用 `ServiceTag + instanceId` 标识 service，避免字符串式误用
- 文件 service 支持“启动时挂载 + 运行期变化感知”
- 目录请求会先尝试目录内的 `index.html`，找不到时再使用 `resources/directory-index-template.html` 渲染目录列表
- 文件 service 的目录列表模板路径可在 `services/files/Context.h` 中按 service 实例配置
- API router 支持按 route 声明响应缓存策略，并可通过 router 构造参数做策略注入
- API 路由可自动生成 OpenAPI 与 Swagger UI
- 请求处理接入线程池，适合轻量并发服务

## 2. 如何构建、部署、测试

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
- `file_service_integration`：文件 service 基础链路，覆盖延迟挂载生效、缓存刷新、目录请求优先命中 `index.html`、缺失首页时回退目录列表、自定义目录列表模板路径，以及符号链接逃逸拒绝

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

## 3. 二次开发指南和样例

建议先对照这些现有文件再开始改：

- `Application.cpp`：应用装配入口，新增 router / service 最终都要落到这里
- `ServiceTags.h`：service 的 `Context`、`ID`、`DisplayName` 定义
- `services/simpleapi/Context.h`：默认 `api` service 的上下文结构
- `services/simpleapi/DefaultRouter.h` / `services/simpleapi/StatusRouter.h` / `services/simpleapi/SampleRouter.h`：现有 router 写法
- `services/files/Runtime.h`：不走 `AddRouter(...)`、直接配置底层 `httplib::Server` 的现成例子
- `RouterModule.h`：router 需要实现的接口定义

### 3.1 在已有 `api` service 下新增一个路由

目标：新增 `GET /time`，同时让它自动出现在 `http://127.0.0.1:8080/docs`。

步骤：

1. 新建文件 `services/simpleapi/TimeRouter.h`
2. 参考 `services/simpleapi/DefaultRouter.h` 或 `services/simpleapi/SampleRouter.h`，实现一个继承 `RouterModule<TContext>` 的 router
3. 修改 `Application.cpp`，先 `#include "services/simpleapi/TimeRouter.h"`，再在 `api` service 的装配链上追加 `.AddRouter<...>()`
4. 重新构建并访问 `/time`、`/docs`

新文件：`services/simpleapi/TimeRouter.h`

```cpp
#pragma once

#include <chrono>
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
        "Return the current server time and echo the requested timezone.",
        "Simple JSON response",
        [](const httplib::Request &req) {
          using Clock = std::chrono::system_clock;
          const auto unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                        Clock::now().time_since_epoch())
                                        .count();
          const std::string tz =
              req.has_param("tz") ? req.get_param_value("tz") : "local";
          return Json{{"ok", true},
                      {"tz", tz},
                      {"unix_seconds", unix_seconds}};
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

### 3.1.1 API 缓存策略注入

注入点不在 `ServerOptions`，而在 router 本身：重写 `RouterModule<TContext>::ResolveCachePolicy(method, path)`，按 route 返回 `std::optional<httplib::API::CachePolicy>`。如果返回 `std::nullopt`，或 `ttl <= 0`，该 route 就不会启用缓存。

当前项目里，`services/simpleapi/StatusRouter.h` 已经是一个最小示例：它只对 `GET /status` 返回缓存策略。

如果你希望把缓存策略从 `Application.cpp` 注入进 router，可以让 router 构造函数接收 `httplib::API::CachePolicy`，再通过 `.AddRouter<TRouter>(args...)` 传进去。`Compositor` 会把这些参数原样转发给 router 构造函数。

示意：

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
        "Return the current server time and echo the requested timezone.",
        "Simple JSON response",
        [](const httplib::Request &req) {
          using Clock = std::chrono::system_clock;
          const auto unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                        Clock::now().time_since_epoch())
                                        .count();
          const std::string tz =
              req.has_param("tz") ? req.get_param_value("tz") : "local";
          return Json{{"ok", true},
                      {"tz", tz},
                      {"unix_seconds", unix_seconds}};
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

常用字段：

- `ttl`：缓存有效期；小于等于 `0` 表示关闭缓存
- `query_fields`：哪些 query 参数参与缓存 key；适合按 `?tz=UTC`、`?lang=zh-CN` 区分结果
- `header_fields`：哪些请求头参与缓存 key；适合按鉴权头、地区头、版本头区分结果
- `max_entries`：该 route 的缓存条目上限
- `max_payload_bytes`：允许进入缓存的响应体大小上限
- `cache_error_response`：是否缓存错误响应；默认不缓存 `5xx`

建议：

- 优先把缓存策略看成 route 级声明，而不是全局 API 开关
- 对依赖 query/header 的接口，务必把相关字段放进 key；否则不同请求可能命中同一个缓存结果
- 对高频健康检查、只读配置接口、短周期聚合结果，这套机制最合适

验证：

```powershell
curl http://127.0.0.1:8080/time
curl http://127.0.0.1:8080/docs
```

### 3.2 新增一个新的 routing service

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

### 3.3 什么时候用 `AddRouter(...)`，什么时候用 `ConfigureHttpServer(...)`

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

如果你要给 file service 显式指定挂载目录和目录列表模板路径，可以改成：

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

行为说明：

- 请求目录路径时，server 会先尝试返回该目录下的 `index.html`
- 如果目录里没有 `index.html`，才会用模板渲染当前目录列表
- 默认目录列表模板位于 `resources/directory-index-template.html`

二开建议：

- 尽量把状态放在 runtime 自己的 `TContext` 中
- 如需共享状态，自己负责加锁或使用原子
- `ConfigureHttpServer(...)` 适合挂底层行为
- `ConfigureService(...)` 适合做 service 级装配

## 4. 项目主要类结构和模板行为

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